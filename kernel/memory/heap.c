#include <kilons/heap.h>
#include <kilons/vm.h>
#include <kilons/memory.h>
#include <kilons/irq_guard.h>
#include <stdint.h>
#include <stddef.h>

#define HEAP_PAGES 16U
#define HEAP_SIZE (HEAP_PAGES * PAGE_SIZE)
#define HEAP_ALIGN 16U
#define FREE_MAGIC 0xFEE0C0DEU
#define USED_MAGIC 0xA110CA7EU
/* A bounded first-fit allocator over the existing 64 KiB bootstrap heap.
 * Block headers live in the heap. There are no hidden host allocations, and
 * no physical frame is released until the entire heap is destroyed (not yet
 * supported). Metadata is checked on every walk; invalid/double frees fail
 * without changing the free list. UP IRQ exclusion is NOT an SMP lock. */
struct heap_block {
    size_t size;          /* total size, including this 16-byte header */
    uint32_t magic;
    uint32_t reserved;
};
_Static_assert(sizeof(struct heap_block)==16, "heap header alignment");
static size_t used;
static bool ready;
static struct heap_block *heap_head(void) {
    return (struct heap_block *)(uintptr_t)VM_HEAP_BASE;
}
static bool valid_header(const struct heap_block *b,size_t pos) {
    return (b->magic==FREE_MAGIC || b->magic==USED_MAGIC) &&
           b->reserved==0 && b->size>=sizeof(*b)+HEAP_ALIGN &&
           !(b->size&(HEAP_ALIGN-1)) && b->size<=HEAP_SIZE-pos;
}
/* Scan the ENTIRE arena before modifying it. Detect corrupted metadata
 * rather than continuing past a damaged header into arbitrary memory. */
static bool heap_walk(void *target,struct heap_block **out) {
    struct heap_block *found=NULL;
    size_t observed=0;
    for(size_t pos=0;pos<HEAP_SIZE;) {
        struct heap_block *b=(struct heap_block *)(uintptr_t)(VM_HEAP_BASE+pos);
        if(!valid_header(b,pos))return false;
        if((void *)(b+1)==target)found=b;
        if(b->magic==USED_MAGIC) observed+=b->size-sizeof(*b);
        pos+=b->size;
    }
    if(observed!=used)return false;
    if(out)*out=found;
    return true;
}
static bool coalesce(void) {
    for(size_t pos=0;pos<HEAP_SIZE;) {
        struct heap_block *a=(struct heap_block *)(uintptr_t)(VM_HEAP_BASE+pos);
        if(!valid_header(a,pos))return false;
        size_t next=pos+a->size;
        if(next<HEAP_SIZE) {
            struct heap_block *b=(struct heap_block *)(uintptr_t)(VM_HEAP_BASE+next);
            if(!valid_header(b,next))return false;
            if(a->magic==FREE_MAGIC && b->magic==FREE_MAGIC) {
                a->size+=b->size;
                /* Poison only the obsolete header, not user allocations. */
                b->magic=0;
                continue;
            }
        }
        pos=next;
    }
    return true;
}
/* Initialization is transactional, including a failed VM mapping. */
bool heap_init(void) {
    if(ready)return false;
    unsigned mapped=0;
    for(unsigned i=0;i<HEAP_PAGES;i++) {
        uint64_t phys=page_alloc();
        if(!phys)goto rollback;
        if(!vm_map(VM_HEAP_BASE+(uint64_t)i*PAGE_SIZE,phys,VM_WRITABLE)) {
            (void)page_free(phys);goto rollback;
        }
        mapped++;
        uint8_t *p=(uint8_t *)(uintptr_t)(VM_HEAP_BASE+(uint64_t)i*PAGE_SIZE);
        for(unsigned j=0;j<PAGE_SIZE;j++)p[j]=0;
    }
    struct heap_block *head=heap_head();
    *head=(struct heap_block){HEAP_SIZE,FREE_MAGIC,0};
    used=0;ready=true;
    return true;
rollback:
    while(mapped) {
        --mapped;
        uint64_t va=VM_HEAP_BASE+(uint64_t)mapped*PAGE_SIZE;
        uint64_t pa=vm_translate(va);
        if(pa && vm_unmap(va))(void)page_free(pa);
    }
    used=0;
    return false;
}
void *kmalloc(size_t bytes) {
    if(!ready || !bytes || bytes>SIZE_MAX-HEAP_ALIGN-sizeof(struct heap_block))return NULL;
    size_t payload=(bytes+(HEAP_ALIGN-1))&~(size_t)(HEAP_ALIGN-1);
    size_t want=payload+sizeof(struct heap_block);
    uint64_t irq=kilons_irq_save();
    if(!heap_walk(NULL,NULL)) {kilons_irq_restore(irq);return NULL;}
    for(size_t pos=0;pos<HEAP_SIZE;) {
        struct heap_block *b=(struct heap_block *)(uintptr_t)(VM_HEAP_BASE+pos);
        if(!valid_header(b,pos))break;
        if(b->magic==FREE_MAGIC && b->size>=want) {
            size_t extra=b->size-want;
            if(extra>=sizeof(struct heap_block)+HEAP_ALIGN) {
                struct heap_block *tail=(struct heap_block *)(uintptr_t)(VM_HEAP_BASE+pos+want);
                *tail=(struct heap_block){extra,FREE_MAGIC,0};
                b->size=want;
            }
            b->magic=USED_MAGIC;
            used+=b->size-sizeof(*b);
            void *answer=b+1;
            kilons_irq_restore(irq);
            return answer;
        }
        pos+=b->size;
    }
    kilons_irq_restore(irq);
    return NULL;
}
bool kfree(void *ptr) {
    if(!ready || !ptr || (uintptr_t)ptr<VM_HEAP_BASE+sizeof(struct heap_block) ||
       (uintptr_t)ptr>=VM_HEAP_BASE+HEAP_SIZE || ((uintptr_t)ptr&(HEAP_ALIGN-1)))return false;
    uint64_t irq=kilons_irq_save();
    struct heap_block *b=NULL;
    if(!heap_walk(ptr,&b) || !b || b->magic!=USED_MAGIC) {
        kilons_irq_restore(irq);return false;
    }
    used-=b->size-sizeof(*b);
    b->magic=FREE_MAGIC;
    bool ok=coalesce();
    kilons_irq_restore(irq);
    return ok;
}
size_t heap_used_bytes(void) {
    uint64_t irq=kilons_irq_save();
    size_t result=used;
    kilons_irq_restore(irq);
    return result;
}
bool heap_validate(void) {
    uint64_t flags=kilons_irq_save();
    bool result=ready && heap_walk(NULL,NULL);
    kilons_irq_restore(flags);
    return result;
}
size_t heap_capacity_bytes(void) {return HEAP_SIZE;}

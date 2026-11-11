/* Heap rollback, reclaim, coalescing and malformed-pointer rejection. */
#define _GNU_SOURCE
#include <kilons/heap.h>
#include <kilons/vm.h>
#include <kilons/memory.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#define PAGES 16U
static uint64_t mapped[PAGES];
static bool busy[PAGES];
static unsigned allocations, free_count;
static unsigned fail_at;
uint64_t page_alloc(void){
    if(++allocations==fail_at)return 0;
    for(unsigned i=0;i<PAGES;i++)if(!busy[i]) {
        busy[i]=true;return 0x400000ULL+(uint64_t)i*4096;
    }
    return 0;
}
bool page_free(uint64_t pa){
    if(pa<0x400000ULL || pa>=0x410000ULL || (pa&4095))return false;
    unsigned i=(unsigned)((pa-0x400000ULL)/4096);
    if(!busy[i])return false;
    busy[i]=false;free_count++;return true;
}
bool vm_map(uint64_t va,uint64_t pa,uint64_t flags){
    if(va<VM_HEAP_BASE || va>=VM_HEAP_BASE+65536 || (va&4095) ||
       flags!=VM_WRITABLE || !pa)return false;
    unsigned i=(unsigned)((va-VM_HEAP_BASE)/4096);
    if(mapped[i])return false;
    mapped[i]=pa;return true;
}
bool vm_unmap(uint64_t va){
    if(va<VM_HEAP_BASE || va>=VM_HEAP_BASE+65536 || (va&4095))return false;
    unsigned i=(unsigned)((va-VM_HEAP_BASE)/4096);
    if(!mapped[i])return false;
    mapped[i]=0;return true;
}
uint64_t vm_translate(uint64_t va){
    if(va<VM_HEAP_BASE || va>=VM_HEAP_BASE+65536)return 0;
    return mapped[(va-VM_HEAP_BASE)/4096]+(va&4095);
}
int main(void){
    void *mapping=mmap((void *)(uintptr_t)VM_HEAP_BASE,65536,PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if(mapping==MAP_FAILED){puts("fixed test heap VA unavailable");return 77;}
    fail_at=6;
    assert(!heap_init());
    assert(!heap_validate());
    for(unsigned i=0;i<PAGES;i++)assert(!mapped[i] && !busy[i]);
    assert(free_count==5 && heap_used_bytes()==0);
    fail_at=0;
    assert(heap_init());
    assert(heap_validate());
    assert(!heap_init());
    for(unsigned i=0;i<PAGES;i++)assert(mapped[i] && busy[i]);
    assert(heap_capacity_bytes()==65536);
    assert(kmalloc(0)==NULL && heap_used_bytes()==0);
    void *p=kmalloc(1);
    assert(p==(void *)(uintptr_t)(VM_HEAP_BASE+16) && heap_used_bytes()==16);
    assert(!kfree(NULL) && !kfree((void *)(uintptr_t)VM_HEAP_BASE));
    assert(!kfree((char *)p+1) && heap_used_bytes()==16);
    void *q=kmalloc(100),*r=kmalloc(4096);
    assert(q && r && (uintptr_t)q%16==0 && (uintptr_t)r%16==0);
    unsigned char *bytes=p;
    *bytes=0x5a;
    assert(kfree(q) && kfree(r) && *bytes==0x5a);
    assert(!kfree(q) && !kfree(r));
    assert(kfree(p) && heap_used_bytes()==0);
    assert(!kfree(p));
    /* Adjacent frees must combine into one maximum allocation. */
    void *large=kmalloc(65520);
    assert(large==(void *)(uintptr_t)(VM_HEAP_BASE+16));
    assert(heap_used_bytes()==65520 && !kmalloc(16));
    assert(kfree(large) && heap_used_bytes()==0);
    assert(kmalloc((size_t)-1)==NULL);
    /* Repeated fragmentation without leaks or user-buffer corruption. */
    void *slots[64]={0};
    for(unsigned round=0;round<500;round++) {
        for(unsigned j=0;j<64;j++) {
            size_t n=(j*13+round*7)%160+1;
            slots[j]=kmalloc(n);
            assert(slots[j]);
            ((unsigned char *)slots[j])[0]=(unsigned char)j;
        }
        for(unsigned j=0;j<64;j++)
            assert(((unsigned char *)slots[j])[0]==(unsigned char)j);
        for(unsigned j=0;j<64;j+=2)assert(kfree(slots[j]));
        for(unsigned j=1;j<64;j+=2)assert(kfree(slots[j]));
        assert(heap_used_bytes()==0 && heap_validate());
    }
    /* Interior addresses must never be accepted as a block start. */
    p=kmalloc(48);
    assert(p && !kfree((char *)p+16));
    assert(kfree(p) && heap_validate());
    assert(munmap(mapping,65536)==0);
    puts("P0 heap init rollback / alloc-free / coalescing / fragmentation PASS");
    return 0;
}

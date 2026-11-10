#include <kilons/vm.h>
#include <kilons/memory.h>
#include <stddef.h>
/* CR3 still points at bootstrap tables; first GiB is identity-mapped with 2 MiB pages.
 * Reserve two previously unused 2 MiB slots and replace their huge PDEs with PTs.
 * No general-purpose address-space or SMP support yet. */
#define P 1ULL
#define W 2ULL
#define HUGE (1ULL << 7)
#define ADDR 0x000ffffffffff000ULL
static uint64_t *test_pt, *heap_pt;
static uint64_t test_pt_phys, heap_pt_phys;
static bool ready;
static uint64_t cr3(void) { uint64_t v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v & ADDR; }
static void flush(uint64_t va) { __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory"); }
static uint64_t *directory(void) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)cr3();
    if (!(pml4[0] & P)) return NULL;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[0] & ADDR);
    if (!(pdpt[0] & P)) return NULL;
    return (uint64_t *)(uintptr_t)(pdpt[0] & ADDR);
}
static void zero_pt(uint64_t *pt) { for (unsigned i=0;i<512;i++) pt[i]=0; }
bool vm_init(void) {
    uint64_t *pd=directory();
    if (!pd || ready) return false;
    unsigned a=(unsigned)(VM_TEST_BASE >> 21), b=(unsigned)(VM_HEAP_BASE >> 21);
    if ((pd[a] & (P|HUGE)) != (P|HUGE) || (pd[b] & (P|HUGE)) != (P|HUGE)) return false;
    test_pt_phys=page_alloc(); heap_pt_phys=page_alloc();
    if (!test_pt_phys || !heap_pt_phys) return false;
    test_pt=(uint64_t *)(uintptr_t)test_pt_phys;
    heap_pt=(uint64_t *)(uintptr_t)heap_pt_phys;
    zero_pt(test_pt); zero_pt(heap_pt);
    /* These virtual ranges are now unmapped. They must not be used as physical aliases. */
    pd[a]=test_pt_phys|P|W; pd[b]=heap_pt_phys|P|W;
    for (uint64_t va=VM_TEST_BASE;va<VM_TEST_BASE+0x200000;va+=4096) flush(va);
    for (uint64_t va=VM_HEAP_BASE;va<VM_HEAP_BASE+0x200000;va+=4096) flush(va);
    ready=true;
    return true;
}
static uint64_t *entry(uint64_t va) {
    if (!ready || (va & 4095)) return NULL;
    if (va>=VM_TEST_BASE && va<VM_TEST_BASE+0x200000) return &test_pt[(va-VM_TEST_BASE)>>12];
    if (va>=VM_HEAP_BASE && va<VM_HEAP_BASE+0x200000) return &heap_pt[(va-VM_HEAP_BASE)>>12];
    return NULL;
}
bool vm_map(uint64_t va,uint64_t pa,uint64_t flags) {
    uint64_t *e=entry(va);
    if (!e || (pa & 4095) || !pa || (flags & ~(W)) || (*e & P)) return false;
    /* Supervisor-only; NX requires EFER.NXE, so not enabled here. */
    *e=pa|P|flags; flush(va); return true;
}
bool vm_unmap(uint64_t va) { uint64_t *e=entry(va); if (!e || !(*e&P)) return false; *e=0;flush(va);return true; }
uint64_t vm_translate(uint64_t va) { uint64_t *e=entry(va&~4095ULL); return e&&(*e&P) ? (*e&ADDR)|(va&4095) : 0; }

/* A process has private PML4/PDPT/PD/PT pages. Kernel identity mappings
 * remain supervisor-only; only the dedicated user PDE has U/S set. The
 * bootstrap kernel page tables are never used as user page tables. */
#define USER_BASE 0x32000000ULL
#define USER_PAGES 32U
#define U 4ULL
static uint64_t kernel_cr3;
static struct vm_space *active_space;
static bool nx_available;
static bool user_ready;
static bool cpu_has_nx(void) {
    uint32_t max, d;
    __asm__ volatile("cpuid" : "=a"(max) : "a"(0x80000000U) : "rbx", "rcx", "rdx");
    if(max<0x80000001U) return false;
    __asm__ volatile("cpuid" : "=d"(d) : "a"(0x80000001U) : "rbx", "rcx");
    return (d&(1U<<20))!=0;
}
static void enable_nx(void) {
    if(!cpu_has_nx()) return;
    uint32_t lo,hi;
    __asm__ volatile("rdmsr" : "=a"(lo),"=d"(hi) : "c"(0xc0000080U));
    lo|=(1U<<11);
    __asm__ volatile("wrmsr" :: "c"(0xc0000080U),"a"(lo),"d"(hi));
    nx_available=true;
}
bool vm_user_init(void) {
    if(!ready) return false;
    if(!user_ready) { kernel_cr3=cr3(); enable_nx(); user_ready=true; }
    return true;
}
static uint64_t *pt_phys(uint64_t pa) { return (uint64_t *)(uintptr_t)pa; }
void vm_space_destroy(struct vm_space *space) {
    if(!space) return;
    if(active_space==space) vm_space_activate(NULL);
    if(space->pt) {
        uint64_t *pt=pt_phys(space->pt);
        for(unsigned i=0;i<USER_PAGES;i++) if(pt[i]&P) {
            uint64_t pa=pt[i]&ADDR; pt[i]=0; (void)page_free(pa);
        }
    }
    if(space->pt) (void)page_free(space->pt);
    if(space->pd) (void)page_free(space->pd);
    if(space->pdpt) (void)page_free(space->pdpt);
    if(space->pml4) (void)page_free(space->pml4);
    *space=(struct vm_space){0};
}
bool vm_space_create(struct vm_space *space) {
    if(!space || !user_ready) return false;
    *space=(struct vm_space){0};
    space->pml4=page_alloc();space->pdpt=page_alloc();
    space->pd=page_alloc();space->pt=page_alloc();
    if(!space->pml4 || !space->pdpt || !space->pd || !space->pt) {
        vm_space_destroy(space);return false;
    }
    uint64_t *original=pt_phys(kernel_cr3);
    uint64_t *original_pdpt=pt_phys(original[0]&ADDR);
    uint64_t *original_pd=pt_phys(original_pdpt[0]&ADDR);
    uint64_t *pml4=pt_phys(space->pml4),*pdpt=pt_phys(space->pdpt);
    uint64_t *pd=pt_phys(space->pd),*pt=pt_phys(space->pt);
    zero_pt(pml4);zero_pt(pdpt);zero_pt(pt);
    for(unsigned i=0;i<512;i++) pd[i]=original_pd[i];
    for(unsigned i=1;i<512;i++) pdpt[i]=original_pdpt[i];
    /* User-accessible walk at levels 4,3,2. All other leaf entries
     * copied from the kernel have U/S=0, so remain inaccessible. */
    pml4[0]=space->pdpt|P|W|U;
    pdpt[0]=space->pd|P|W|U;
    pd[USER_BASE>>21]=space->pt|P|W|U;
    return true;
}
void vm_space_activate(struct vm_space *space) {
    uint64_t value=space?space->pml4:kernel_cr3;
    if(!value) return;
    __asm__ volatile("mov %0,%%cr3" :: "r"(value) : "memory");
    active_space=space;
}
uint64_t vm_space_cr3(const struct vm_space *space) { return space?space->pml4:0; }
bool vm_space_map(struct vm_space *space,unsigned index,uint64_t pa,bool writable,bool executable) {
    if(!space || !space->pt || index>=USER_PAGES || !pa || (pa&4095) || (writable&&executable))return false;
    uint64_t *pt=pt_phys(space->pt);
    if(pt[index]&P)return false;
    pt[index]=pa|P|U|(writable?W:0)|((nx_available&&!executable)?(1ULL<<63):0);
    if(active_space==space)flush(USER_BASE+(uint64_t)index*4096);
    return true;
}
bool vm_space_check(const struct vm_space *space,uint64_t address,uint64_t length,bool writable) {
    if(!space || !space->pt || address<USER_BASE || address>=USER_BASE+USER_PAGES*4096ULL ||
       length>USER_BASE+USER_PAGES*4096ULL-address) return false;
    if(!length)return true;
    unsigned first=(unsigned)((address-USER_BASE)>>12);
    unsigned last=(unsigned)((address+length-1-USER_BASE)>>12);
    uint64_t *pt=pt_phys(space->pt);
    for(unsigned i=first;i<=last;i++)
        if(!(pt[i]&P) || !(pt[i]&U) || (writable && !(pt[i]&W)))return false;
    return true;
}
bool vm_user_check(uint64_t address,uint64_t length,bool writable) {
    return vm_space_check(active_space,address,length,writable);
}

/* Check two independent page tables can map the same user VA to distinct
 * physical frames without changing either the kernel CR3 or the other space. */
bool vm_space_selftest(void) {
    struct vm_space first={0},second={0};
    uint32_t before=memory_free_pages();
    bool ok=vm_space_create(&first) && vm_space_create(&second);
    uint64_t a=0,b=0;
    if(ok) {
        a=page_alloc();b=page_alloc();
        ok=a && b && a!=b;
    }
    if(ok) {
        ok=vm_space_map(&first,0,a,true,false);
        if(ok)a=0;
        if(ok) {ok=vm_space_map(&second,0,b,true,false);if(ok)b=0;}
    }
    if(ok) {
        uint64_t *pta=pt_phys(first.pt),*ptb=pt_phys(second.pt);
        uint64_t *pda=pt_phys(first.pd),*pdb=pt_phys(second.pd);
        ok=first.pml4!=second.pml4 &&
           (pta[0]&ADDR)!=(ptb[0]&ADDR) &&
           (pda[0]&U)==0 && (pdb[0]&U)==0 &&
           vm_space_check(&first,VM_USER_BASE,8,true) &&
           vm_space_check(&second,VM_USER_BASE,8,true) &&
           !vm_space_check(&first,0x100000,8,false);
    }
    if(a)(void)page_free(a);
    if(b)(void)page_free(b);
    vm_space_destroy(&first);vm_space_destroy(&second);
    return ok && before==memory_free_pages();
}

/* A fully eager fork clone: no shared writable frames, hence no COW races.
 * This is deliberately bounded by the 32-page user layout. */
bool vm_space_clone(struct vm_space *dst,const struct vm_space *src) {
    if(!dst || !src || !src->pt || !vm_space_create(dst))return false;
    uint64_t *from=pt_phys(src->pt),*to=pt_phys(dst->pt);
    for(unsigned i=0;i<USER_PAGES;i++) if(from[i]&P) {
        uint64_t pa=page_alloc();
        if(!pa) {vm_space_destroy(dst);return false;}
        uint64_t *a=pt_phys(from[i]&ADDR),*b=pt_phys(pa);
        for(unsigned j=0;j<512;j++)b[j]=a[j];
        to[i]=pa|(from[i]&~ADDR);
    }
    dst->anon_pages=src->anon_pages;
    return true;
}
/* Private anonymous mapping region: pages 16..23, just below the stack. */
#define ANON_FIRST 16U
#define ANON_COUNT 8U
uint64_t vm_space_anonymous(struct vm_space *space,unsigned pages) {
    if(!space || !space->pt || !pages || pages>ANON_COUNT)return 0;
    for(unsigned start=0;start+pages<=ANON_COUNT;start++) {
        unsigned mask=((1U<<pages)-1U)<<start;
        if(space->anon_pages&mask)continue;
        unsigned mapped=0;
        for(;mapped<pages;mapped++) {
            uint64_t pa=page_alloc();
            if(!pa)break;
            uint64_t *dst=pt_phys(pa);
            zero_pt(dst);
            if(!vm_space_map(space,ANON_FIRST+start+mapped,pa,true,false)) {
                (void)page_free(pa);break;
            }
        }
        if(mapped==pages) {space->anon_pages|=(uint16_t)mask;return USER_BASE+(ANON_FIRST+start)*4096ULL;}
        uint64_t *pt=pt_phys(space->pt);
        for(unsigned i=0;i<mapped;i++) {
            unsigned index=ANON_FIRST+start+i;
            uint64_t old=pt[index];pt[index]=0;
            if(active_space==space)flush(USER_BASE+index*4096ULL);
            (void)page_free(old&ADDR);
        }
        return 0;
    }
    return 0;
}
static bool anon_range(struct vm_space *space,uint64_t addr,unsigned pages,unsigned *first) {
    if(!space || !space->pt || !pages || (addr&4095ULL) ||
       addr<USER_BASE+ANON_FIRST*4096ULL ||
       addr>=USER_BASE+(ANON_FIRST+ANON_COUNT)*4096ULL)return false;
    unsigned index=(unsigned)((addr-USER_BASE)/4096ULL)-ANON_FIRST;
    if(index+pages>ANON_COUNT)return false;
    unsigned mask=((1U<<pages)-1U)<<index;
    if((space->anon_pages&mask)!=mask)return false;
    *first=index;return true;
}
bool vm_space_unmap_anonymous(struct vm_space *space,uint64_t addr,unsigned pages) {
    unsigned first;if(!anon_range(space,addr,pages,&first))return false;
    uint64_t *pt=pt_phys(space->pt);
    for(unsigned i=0;i<pages;i++) {
        unsigned index=ANON_FIRST+first+i;
        uint64_t pa=pt[index]&ADDR;pt[index]=0;
        if(active_space==space)flush(USER_BASE+index*4096ULL);
        (void)page_free(pa);
    }
    space->anon_pages&=(uint16_t)~(((1U<<pages)-1U)<<first);
    return true;
}
bool vm_space_protect_anonymous(struct vm_space *space,uint64_t addr,unsigned pages,bool writable) {
    unsigned first;if(!anon_range(space,addr,pages,&first))return false;
    uint64_t *pt=pt_phys(space->pt);
    for(unsigned i=0;i<pages;i++) {
        unsigned index=ANON_FIRST+first+i;
        if(writable)pt[index]|=W;
        else pt[index]&=~W;
        if(active_space==space)flush(USER_BASE+index*4096ULL);
    }
    return true;
}

static bool vm_space_transfer(const struct vm_space *space,uint64_t user,void *buffer,
                              size_t length,bool to_user) {
    if(!vm_space_check(space,user,length,to_user))return false;
    uint64_t *pt=pt_phys(space->pt);
    uint8_t *bytes=buffer;
    size_t done=0;
    while(done<length) {
        uint64_t addr=user+done;
        unsigned page=(unsigned)((addr-USER_BASE)>>12);
        size_t offset=(size_t)(addr&4095ULL),n=4096-offset;
        if(n>length-done)n=length-done;
        uint8_t *mapped=(uint8_t *)(uintptr_t)((pt[page]&ADDR)+offset);
        for(size_t i=0;i<n;i++) {
            if(to_user)mapped[i]=bytes[done+i];
            else bytes[done+i]=mapped[i];
        }
        done+=n;
    }
    return true;
}
bool vm_space_copyin(const struct vm_space *space,void *dst,uint64_t src,size_t n) {
    return dst && vm_space_transfer(space,src,dst,n,false);
}
bool vm_space_copyout(const struct vm_space *space,uint64_t dst,const void *src,size_t n) {
    return src && vm_space_transfer(space,dst,(void *)src,n,true);
}

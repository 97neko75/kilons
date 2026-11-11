#include <kilons/vm.h>
#include <kilons/memory.h>
#include <stddef.h>
/* CR3 still points at bootstrap tables; first GiB is identity-mapped with 2 MiB pages.
 * Reserve two previously unused 2 MiB slots and replace their huge PDEs with PTs.
 * No general-purpose address-space or SMP support yet. */
#define P 1ULL
#define W 2ULL
#define HUGE (1ULL << 7)
/* x86-64 hardware sets Accessed in every walked paging entry and Dirty
 * on writable leaf entries. They are not evidence of table corruption. */
#define ACCESSED (1ULL << 5)
#define DIRTY (1ULL << 6)
#define ADDR 0x000ffffffffff000ULL
static uint64_t *test_pt, *heap_pt;
static uint64_t test_pt_phys, heap_pt_phys;
static bool ready;
#ifdef KILONS_HOST_TEST
/* Test-only CR3 and TLB stand-ins. Hosted page-table operations still use
 * the same creation, mapping, cloning and copy paths as the real kernel. */
static uint64_t host_cr3;
void vm_host_set_cr3(uint64_t phys) { host_cr3=phys; }
static uint64_t cr3(void) { return host_cr3; }
static void flush(uint64_t va) { (void)va; }
#else
static uint64_t cr3(void) { uint64_t v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v & ADDR; }
static void flush(uint64_t va) { __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory"); }
#endif
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
    test_pt_phys=page_alloc();
    if (!test_pt_phys) return false;
    heap_pt_phys=page_alloc();
    if (!heap_pt_phys) {
        (void)page_free(test_pt_phys);
        test_pt_phys=0;
        return false;
    }
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
    if (!e || (pa & 4095) || !pa || (flags & ~(W)) || (*e & P) ||
        !page_is_allocated(pa)) return false;
    /* Both test and bootstrap heap maps own a frame. Never map one frame
     * twice into these independently-destroyable virtual regions. */
    for(unsigned i=0;i<512;i++)if(((test_pt[i]&P) && (test_pt[i]&ADDR)==pa) ||
                                    ((heap_pt[i]&P) && (heap_pt[i]&ADDR)==pa))return false;
    /* Supervisor-only; NX requires EFER.NXE, so not enabled here. */
    if(!page_claim_mapping(pa))return false;
    *e=pa|P|flags; flush(va); return true;
}
bool vm_unmap(uint64_t va) {
    uint64_t *e=entry(va);
    if(!e || !(*e&P) || !page_release_mapping(*e&ADDR))return false;
    *e=0;flush(va);return true;
}
uint64_t vm_translate(uint64_t va) { uint64_t *e=entry(va&~4095ULL); return e&&(*e&P) ? (*e&ADDR)|(va&4095) : 0; }

/* A process has private PML4/PDPT/PD/PT pages. Kernel identity mappings
 * remain supervisor-only; only the dedicated user PDE has U/S set. The
 * bootstrap kernel page tables are never used as user page tables. */
#define USER_BASE 0x32000000ULL
#define USER_PAGES 32U
#define ANON_FIRST 16U
#define ANON_COUNT 8U
#define U 4ULL
static uint64_t kernel_cr3;
static struct vm_space *active_space;
static bool nx_available;
static bool user_ready;
#ifndef KILONS_HOST_TEST
static bool cpu_has_nx(void) {
    uint32_t max, d;
    __asm__ volatile("cpuid" : "=a"(max) : "a"(0x80000000U) : "rbx", "rcx", "rdx");
    if(max<0x80000001U) return false;
    __asm__ volatile("cpuid" : "=d"(d) : "a"(0x80000001U) : "rbx", "rcx");
    return (d&(1U<<20))!=0;
}
#endif
static void enable_nx(void) {
#ifdef KILONS_HOST_TEST
    /* The hosted fixture models an NX-capable CPU, no privileged MSR. */
    nx_available=true;
    return;
#else
    if(!cpu_has_nx()) return;
    uint32_t lo,hi;
    __asm__ volatile("rdmsr" : "=a"(lo),"=d"(hi) : "c"(0xc0000080U));
    lo|=(1U<<11);
    __asm__ volatile("wrmsr" :: "c"(0xc0000080U),"a"(lo),"d"(hi));
    nx_available=true;
#endif
}
bool vm_user_init(void) {
    if(!ready) return false;
    if(!user_ready) {
        kernel_cr3=cr3();
        enable_nx();
        /* W^X cannot be enforced in ring3 without EFER.NXE. Refuse the
         * user subsystem instead of silently providing executable stacks. */
        if(!nx_available)return false;
        user_ready=true;
    }
    return true;
}
static uint64_t *pt_phys(uint64_t pa) { return (uint64_t *)(uintptr_t)pa; }
void vm_space_destroy(struct vm_space *space) {
    if(!space) return;
    if(active_space==space) vm_space_activate(NULL);
    /* The owner ledger, rather than potentially corrupted PTE contents,
     * determines which physical frames this address space may release.
     * A damaged leaf can no longer free another process's page. */
    for(unsigned i=0;i<USER_PAGES;i++) if(space->owned[i]) {
        uint64_t pa=space->owned[i];
        if(space->pt)pt_phys(space->pt)[i]=0;
        space->owned[i]=0;
        if(page_release_mapping(pa))(void)page_free(pa);
    }
    if(space->pt) (void)page_free(space->pt);
    if(space->pd) (void)page_free(space->pd);
    if(space->pdpt) (void)page_free(space->pdpt);
    if(space->pml4) (void)page_free(space->pml4);
    *space=(struct vm_space){0};
}
/* Page-table pages are ordinary recycled physical pages, never implicitly
 * zero. Initialize each table immediately after allocation: on failure,
 * destroy() must not interpret stale leaf bits as owned user mappings. */
static bool vm_new_table(uint64_t *out) {
    uint64_t pa=page_alloc();
    if(!pa)return false;
    zero_pt(pt_phys(pa));
    *out=pa;
    return true;
}
bool vm_space_create(struct vm_space *space) {
    if(!space || !user_ready) return false;
    if(space->pml4 || space->pdpt || space->pd || space->pt)return false;
    *space=(struct vm_space){0};
    if(!vm_new_table(&space->pml4) || !vm_new_table(&space->pdpt) ||
       !vm_new_table(&space->pd) || !vm_new_table(&space->pt)) {
        vm_space_destroy(space);
        return false;
    }
    uint64_t *original=pt_phys(kernel_cr3);
    /* Fail closed if the saved bootstrap directory is malformed. */
    if(!(original[0]&P) || (original[0]&HUGE) ||
       !original[0] || !(original[0]&ADDR))goto fail;
    uint64_t *original_pdpt=pt_phys(original[0]&ADDR);
    if(!(original_pdpt[0]&P) || (original_pdpt[0]&HUGE) ||
       !(original_pdpt[0]&ADDR))goto fail;
    uint64_t *original_pd=pt_phys(original_pdpt[0]&ADDR);
    uint64_t *pml4=pt_phys(space->pml4),*pdpt=pt_phys(space->pdpt);
    uint64_t *pd=pt_phys(space->pd);
    for(unsigned i=0;i<512;i++)pd[i]=original_pd[i];
    for(unsigned i=1;i<512;i++)pdpt[i]=original_pdpt[i];
    /* The private user PDE may only be installed over a supervisor mapping. */
    if((pd[USER_BASE>>21]&U) || !(pd[USER_BASE>>21]&P))goto fail;
    pml4[0]=space->pdpt|P|W|U;
    pdpt[0]=space->pd|P|W|U;
    pd[USER_BASE>>21]=space->pt|P|W|U;
    return true;
fail:
    vm_space_destroy(space);
    return false;
}
void vm_space_activate(struct vm_space *space) {
    uint64_t value=space?space->pml4:kernel_cr3;
    if(!value) return;
#ifdef KILONS_HOST_TEST
    host_cr3=value;
#else
    __asm__ volatile("mov %0,%%cr3" :: "r"(value) : "memory");
#endif
    active_space=space;
}
uint64_t vm_space_cr3(const struct vm_space *space) { return space?space->pml4:0; }
/* Validate a private address space before a destructive operation or fork.
 * The page-table chain itself must be intact; leaf PTEs must be owned,
 * non-aliased, and W^X. This is a bounded 512-entry audit, NOT performed on
 * every byte copied through a user pointer. */
static bool vm_space_chain_ok(const struct vm_space *s) {
    if (!s || !s->pml4 || !s->pdpt || !s->pd || !s->pt ||
        s->pml4==s->pdpt || s->pml4==s->pd || s->pml4==s->pt ||
        s->pdpt==s->pd || s->pdpt==s->pt || s->pd==s->pt ||
        !page_is_allocated(s->pml4) || !page_is_allocated(s->pdpt) ||
        !page_is_allocated(s->pd) || !page_is_allocated(s->pt) ||
        page_is_mapped(s->pml4) || page_is_mapped(s->pdpt) ||
        page_is_mapped(s->pd) || page_is_mapped(s->pt))return false;
    const uint64_t *pml4=pt_phys(s->pml4);
    const uint64_t *pdpt=pt_phys(s->pdpt);
    const uint64_t *pd=pt_phys(s->pd);
    /* Hardware can set A on all three upper-level entries during iret
     * and user execution. Preserve exact validation of all other bits. */
    return (pml4[0]&~ACCESSED)==(s->pdpt|P|W|U) &&
           (pdpt[0]&~ACCESSED)==(s->pd|P|W|U) &&
           (pd[USER_BASE>>21]&~ACCESSED)==(s->pt|P|W|U);
}
bool vm_space_validate(const struct vm_space *s) {
    if (!vm_space_chain_ok(s) || (s->anon_pages&~0xffU))return false;
    /* A stray U/S bit in a different PDE would expose supervisor identity
     * mappings directly to Ring 3 even though syscall ranges are checked.
     * Audit the entire private hierarchy before first dispatch / fork. */
    const uint64_t *pml4=pt_phys(s->pml4);
    const uint64_t *pdpt=pt_phys(s->pdpt);
    const uint64_t *pd=pt_phys(s->pd);
    for(unsigned i=1;i<512;i++) {
        if(pml4[i] || (pdpt[i]&U))return false;
    }
    for(unsigned i=0;i<512;i++) {
        if(i!=(USER_BASE>>21) && (pd[i]&U))return false;
    }
    const uint64_t *pt=pt_phys(s->pt);
    const uint64_t leaf_flags=P|W|U|ACCESSED|DIRTY|(1ULL<<63);
    for(unsigned i=0;i<512;i++) {
        uint64_t e=pt[i];
        if(i>=USER_PAGES) {if(e)return false;continue;}
        if(!(e&P)) {if(e || s->owned[i])return false;continue;}
        if(!(e&U) || (e&~(ADDR|leaf_flags)) ||
           ((e&W) && !(e&(1ULL<<63))) ||
           !s->owned[i] || s->owned[i]!=(e&ADDR) ||
           !page_is_mapped(e&ADDR))return false;
        if(i>=ANON_FIRST && i<ANON_FIRST+ANON_COUNT &&
           (s->anon_pages & (1U<<(i-ANON_FIRST)))==0)return false;
        for(unsigned j=0;j<i;j++)
            if((pt[j]&P) && (pt[j]&ADDR)==(e&ADDR))return false;
    }
    for(unsigned j=0;j<ANON_COUNT;j++)
        if((s->anon_pages&(1U<<j)) && !(pt[ANON_FIRST+j]&P))return false;
    return true;
}
bool vm_space_map(struct vm_space *space,unsigned index,uint64_t pa,bool writable,bool executable) {
    if(!vm_space_chain_ok(space) || index>=USER_PAGES || !pa || (pa&4095) ||
       (writable&&executable) || !page_is_allocated(pa) ||
       pa==space->pml4 || pa==space->pdpt || pa==space->pd || pa==space->pt)return false;
    uint64_t *pt=pt_phys(space->pt);
    if(pt[index] || space->owned[index])return false; /* Reject stale non-present PTE metadata. */
    /* Disallow a second alias: destroy() owns each mapped frame once. */
    for(unsigned i=0;i<USER_PAGES;i++)
        if((pt[i]&P) && (pt[i]&ADDR)==pa)return false;
    if(!page_claim_mapping(pa))return false;
    space->owned[index]=pa;
    pt[index]=pa|P|U|(writable?W:0)|((nx_available&&!executable)?(1ULL<<63):0);
    if(active_space==space)flush(USER_BASE+(uint64_t)index*4096);
    return true;
}
bool vm_space_check(const struct vm_space *space,uint64_t address,uint64_t length,bool writable) {
    if(!vm_space_chain_ok(space) || address<USER_BASE || address>=USER_BASE+USER_PAGES*4096ULL ||
       length>USER_BASE+USER_PAGES*4096ULL-address) return false;
    if(!length)return true;
    unsigned first=(unsigned)((address-USER_BASE)>>12);
    unsigned last=(unsigned)((address+length-1-USER_BASE)>>12);
    uint64_t *pt=pt_phys(space->pt);
    const uint64_t allowed=P|W|U|ACCESSED|DIRTY|(1ULL<<63);
    for(unsigned i=first;i<=last;i++) {
        uint64_t e=pt[i];
        if(!(e&P) || !(e&U) || (e&~(ADDR|allowed)) ||
           space->owned[i]!=(e&ADDR) ||
           !page_is_mapped(e&ADDR) ||
           ((e&W) && !(e&(1ULL<<63))) ||
           (writable && !(e&W)))return false;
    }
    return true;
}
bool vm_user_check(uint64_t address,uint64_t length,bool writable) {
    return vm_space_check(active_space,address,length,writable);
}
/* Inspect the same active address space as vm_user_check(). This is only
 * diagnostic reporting: the actual read/write path still uses vm_user_check.
 * No physical addresses or privileged mappings are copied to user mode. */
int vm_user_check_reason(uint64_t address,uint64_t length,bool writable) {
    const struct vm_space *s=active_space;
    if(address<USER_BASE || address>=USER_BASE+USER_PAGES*4096ULL ||
       length>USER_BASE+USER_PAGES*4096ULL-address)return 1;
    if(!vm_space_chain_ok(s))return 2;
    if(!length)return 0;
    const uint64_t *pt=pt_phys(s->pt);
    unsigned first=(unsigned)((address-USER_BASE)>>12);
    unsigned last=(unsigned)((address+length-1-USER_BASE)>>12);
    for(unsigned i=first;i<=last;i++) {
        uint64_t e=pt[i];
        if(!(e&P))return 3;
        if(e&~(ADDR|P|W|U|ACCESSED|DIRTY|(1ULL<<63)))return 2;
        if(!(e&U))return 4;
        if(!page_is_mapped(e&ADDR))return 5;
        if((e&W) && !(e&(1ULL<<63)))return 6;
        if(writable && !(e&W))return 7;
        if(!s->owned[i] || s->owned[i]!=(e&ADDR))return 9;
    }
    return 0;
}

/* Boot-time address-space regression. Each test must restore free-page
 * accounting even on failure. This is a deterministic smoke test, not a
 * substitute for stress/fault-injection testing on a running system. */
bool vm_space_selftest(void) {
    struct vm_space first={0},second={0},clone={0};
    uint32_t before=memory_free_pages();
    uint64_t a=0,b=0,anon=0;
    bool ok=false;
    if(!vm_space_create(&first) || !vm_space_create(&second))goto out;
    a=page_alloc();b=page_alloc();
    if(!a || !b || a==b)goto out;
    if(!vm_space_map(&first,0,a,true,false))goto out;
    a=0; /* ownership transferred to the address space */
    if(!vm_space_map(&second,0,b,true,false))goto out;
    b=0;
    uint64_t *fa=pt_phys(first.pt),*fb=pt_phys(second.pt);
    uint64_t *pda=pt_phys(first.pd),*pdb=pt_phys(second.pd);
    if(first.pml4==second.pml4 || (fa[0]&ADDR)==(fb[0]&ADDR) ||
       (pda[0]&U) || (pdb[0]&U) ||
       !vm_space_check(&first,VM_USER_BASE,8,true) ||
       !vm_space_check(&second,VM_USER_BASE,8,true) ||
       vm_space_check(&first,VM_USER_BASE-1,2,false) ||
       vm_space_check(&first,VM_USER_BASE+4095,2,false) ||
       vm_space_check(&first,0x100000,8,false))goto out;
    if(vm_space_map(&first,1,fa[0]&ADDR,true,false))goto out;
    uint64_t sample=0x5a39b42ce7296810ULL,read_back=0;
    if(!vm_space_copyout(&first,VM_USER_BASE+64,&sample,sizeof sample) ||
       !vm_space_copyin(&first,&read_back,VM_USER_BASE+64,sizeof read_back) ||
       read_back!=sample)goto out;
    if(!vm_space_clone(&clone,&first))goto out;
    if(vm_space_clone(&clone,&first) || vm_space_clone(&first,&first))goto out;
    read_back=0;
    if(!vm_space_copyin(&clone,&read_back,VM_USER_BASE+64,sizeof read_back) ||
       read_back!=sample || (pt_phys(clone.pt)[0]&ADDR)==(fa[0]&ADDR))goto out;
    read_back=~sample;
    if(!vm_space_copyout(&clone,VM_USER_BASE+64,&read_back,sizeof read_back))goto out;
    read_back=0;
    if(!vm_space_copyin(&first,&read_back,VM_USER_BASE+64,sizeof read_back) ||
       read_back!=sample)goto out;
    anon=vm_space_anonymous(&first,2);
    if(!anon || !vm_space_check(&first,anon,8192,true) ||
       !vm_space_protect_anonymous(&first,anon,1,false) ||
       vm_space_check(&first,anon,1,true) ||
       !vm_space_check(&first,anon,1,false) ||
       !vm_space_protect_anonymous(&first,anon,1,true) ||
       !vm_space_unmap_anonymous(&first,anon,2) ||
       vm_space_check(&first,anon,1,false) ||
       vm_space_unmap_anonymous(&first,anon,2))goto out;
    ok=true;
out:
    if(a)(void)page_free(a);
    if(b)(void)page_free(b);
    vm_space_destroy(&clone);
    vm_space_destroy(&first);
    vm_space_destroy(&second);
    return ok && before==memory_free_pages();
}

/* A fully eager fork clone: no shared writable frames, hence no COW races.
 * This is deliberately bounded by the 32-page user layout. */
bool vm_space_clone(struct vm_space *dst,const struct vm_space *src) {
    if(!dst || !src || dst==src || !vm_space_validate(src) ||
       !vm_space_create(dst))return false;
    uint64_t *from=pt_phys(src->pt),*to=pt_phys(dst->pt);
    for(unsigned i=0;i<USER_PAGES;i++) if(from[i]&P) {
        if(!(from[i]&U) || !page_is_allocated(from[i]&ADDR)) {
            vm_space_destroy(dst);return false;
        }
        uint64_t pa=page_alloc();
        if(!pa) {vm_space_destroy(dst);return false;}
        uint64_t *a=pt_phys(from[i]&ADDR),*b=pt_phys(pa);
        for(unsigned j=0;j<512;j++)b[j]=a[j];
        if(!page_claim_mapping(pa)) {
            (void)page_free(pa);vm_space_destroy(dst);return false;
        }
        dst->owned[i]=pa;
        to[i]=pa|(from[i]&~ADDR);
    }
    dst->anon_pages=src->anon_pages;
    return true;
}
/* Private anonymous mapping region: pages 16..23, just below the stack. */
uint64_t vm_space_anonymous(struct vm_space *space,unsigned pages) {
    if(!space || !vm_space_validate(space) || !pages || pages>ANON_COUNT)return 0;
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
            uint64_t old=pt[index];
            (void)page_release_mapping(space->owned[index]);
            space->owned[index]=0;
            pt[index]=0;
            if(active_space==space)flush(USER_BASE+index*4096ULL);
            (void)page_free(old&ADDR);
        }
        return 0;
    }
    return 0;
}
static bool anon_range(struct vm_space *space,uint64_t addr,unsigned pages,unsigned *first) {
    if(!space || !vm_space_validate(space) || !pages || (addr&4095ULL) ||
       addr<USER_BASE+ANON_FIRST*4096ULL ||
       addr>=USER_BASE+(ANON_FIRST+ANON_COUNT)*4096ULL)return false;
    unsigned index=(unsigned)((addr-USER_BASE)/4096ULL)-ANON_FIRST;
    if(pages>ANON_COUNT-index)return false;
    unsigned mask=((1U<<pages)-1U)<<index;
    if((space->anon_pages&mask)!=mask)return false;
    *first=index;return true;
}
bool vm_space_unmap_anonymous(struct vm_space *space,uint64_t addr,unsigned pages) {
    unsigned first;
    if(!anon_range(space,addr,pages,&first))return false;
    uint64_t *pt=pt_phys(space->pt);
    /* Check every leaf before starting a destructive operation. */
    for(unsigned i=0;i<pages;i++) {
        unsigned index=ANON_FIRST+first+i;
        if(!(pt[index]&P) || !(pt[index]&U) ||
           !page_is_mapped(pt[index]&ADDR))return false;
    }
    for(unsigned i=0;i<pages;i++) {
        unsigned index=ANON_FIRST+first+i;
        uint64_t pa=pt[index]&ADDR;
        if(!page_release_mapping(pa))return false;
        space->owned[index]=0;
        pt[index]=0;
        if(active_space==space)flush(USER_BASE+index*4096ULL);
        (void)page_free(pa);
    }
    space->anon_pages&=(uint16_t)~(((1U<<pages)-1U)<<first);
    return true;
}
bool vm_space_protect_anonymous(struct vm_space *space,uint64_t addr,unsigned pages,bool writable) {
    unsigned first;
    if(!anon_range(space,addr,pages,&first))return false;
    uint64_t *pt=pt_phys(space->pt);
    for(unsigned i=0;i<pages;i++) {
        unsigned index=ANON_FIRST+first+i;
        if(!(pt[index]&P) || !(pt[index]&U) ||
           !page_is_mapped(pt[index]&ADDR))return false;
    }
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

/* Host execution of the REAL VM implementation using synthetic physical
 * memory and a test-only CR3 substitute. This covers error rollback without
 * making host tests execute privileged MOV CR3 / INVLPG / RDMSR instructions. */
#define _GNU_SOURCE
#include <kilons/vm.h>
#include <kilons/memory.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define POOL_BASE 0x08000000ULL
#define POOL_FRAMES 2048U
#define PAGE 4096ULL
static uint8_t taken[POOL_FRAMES];
static uint8_t mapped[POOL_FRAMES];
static unsigned remaining=POOL_FRAMES;
static unsigned alloc_calls;
static unsigned fail_nth;
static void fail_after(unsigned nth) {fail_nth=nth;alloc_calls=0;}
uint64_t page_alloc(void) {
    if(fail_nth && ++alloc_calls==fail_nth)return 0;
    for(unsigned i=0;i<POOL_FRAMES;i++)if(!taken[i]) {
        taken[i]=1;remaining--;
        /* Real recycled physical frames retain stale page-table entries. */
        memset((void *)(uintptr_t)(POOL_BASE+(uint64_t)i*PAGE),0xa5,PAGE);
        return POOL_BASE+(uint64_t)i*PAGE;
    }
    return 0;
}
bool page_free(uint64_t pa) {
    if(pa<POOL_BASE || pa>=POOL_BASE+POOL_FRAMES*PAGE || (pa&(PAGE-1)))return false;
    unsigned i=(unsigned)((pa-POOL_BASE)/PAGE);
    if(!taken[i] || mapped[i])return false;
    taken[i]=0;remaining++;
    return true;
}
bool page_is_allocated(uint64_t pa) {
    return pa>=POOL_BASE && pa<POOL_BASE+POOL_FRAMES*PAGE && !(pa&(PAGE-1)) &&
           taken[(pa-POOL_BASE)/PAGE];
}
bool page_claim_mapping(uint64_t pa) {
    if(!page_is_allocated(pa))return false;
    unsigned i=(unsigned)((pa-POOL_BASE)/PAGE);
    if(mapped[i])return false;
    mapped[i]=1;return true;
}
bool page_release_mapping(uint64_t pa) {
    if(!page_is_allocated(pa))return false;
    unsigned i=(unsigned)((pa-POOL_BASE)/PAGE);
    if(!mapped[i])return false;
    mapped[i]=0;return true;
}
bool page_is_mapped(uint64_t pa) {
    return page_is_allocated(pa) && mapped[(pa-POOL_BASE)/PAGE];
}
uint32_t memory_free_pages(void){return remaining;}
static uint64_t *words(uint64_t pa){return (uint64_t *)(uintptr_t)pa;}
static void boot_tables(void){
    uint64_t a=page_alloc(),b=page_alloc(),c=page_alloc();
    assert(a && b && c);
    memset(words(a),0,4096);memset(words(b),0,4096);
    for(unsigned i=0;i<512;i++)words(c)[i]=(uint64_t)i*0x200000ULL|0x83ULL;
    words(a)[0]=b|3ULL;words(b)[0]=c|3ULL;
    vm_host_set_cr3(a);
}
int main(void){
    void *pool=mmap((void *)(uintptr_t)POOL_BASE,POOL_FRAMES*PAGE,PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if(pool==MAP_FAILED){puts("low-address test memory unavailable");return 77;}
    boot_tables();
    assert(vm_init() && vm_user_init());
    assert(vm_space_selftest()); /* Includes clone/private copy and anonymous mapping */
    /* Every step of address-space creation must unwind partial allocations. */
    for(unsigned failure=1;failure<=4;failure++) {
        struct vm_space s={0};unsigned before=remaining;
        fail_after(failure);
        assert(!vm_space_create(&s));
        assert(!s.pml4 && !s.pdpt && !s.pd && !s.pt);
        assert(remaining==before);
    }
    fail_after(0);
    struct vm_space source={0};
    assert(vm_space_create(&source));
    for(unsigned i=0;i<2;i++) {
        uint64_t pa=page_alloc();assert(pa && vm_space_map(&source,i,pa,true,false));
        memset((void *)(uintptr_t)pa,(int)(i+1),4096);
    }
    /* Simulate the CPU's A/D updates after the first user-mode instruction.
     * The original P0 chain equality incorrectly treated valid hardware
     * state as corruption and caused write(1, ...) to return EFAULT. */
    uint64_t *user_pml4=words(source.pml4);
    uint64_t *user_pdpt=words(source.pdpt);
    uint64_t *user_pd=words(source.pd);
    uint64_t *user_pt=words(source.pt);
    user_pml4[0]|=1ULL<<5;
    user_pdpt[0]|=1ULL<<5;
    user_pd[VM_USER_BASE>>21]|=1ULL<<5;
    user_pt[0]|=(1ULL<<5)|(1ULL<<6);
    user_pt[1]|=(1ULL<<5)|(1ULL<<6);
    assert(vm_space_validate(&source));
    assert(vm_space_check(&source,VM_USER_BASE+8,32,false));
    vm_space_activate(&source);
    assert(vm_user_check(VM_USER_BASE+8,32,false));
    assert(vm_user_check_reason(VM_USER_BASE+8,32,false)==0);
    vm_space_activate(NULL);
    /* Unsupported flag changes still fail closed. */
    user_pml4[0]|=1ULL<<4;
    assert(!vm_space_validate(&source));
    assert(!vm_space_check(&source,VM_USER_BASE,1,false));
    user_pml4[0]&=~(1ULL<<4);
    user_pt[0]|=1ULL<<4;
    assert(!vm_space_validate(&source));
    user_pt[0]&=~(1ULL<<4);
    assert(vm_space_validate(&source));
    /* A malformed but present leaf must never pass a syscall pointer check. */
    user_pt[0]|=1ULL<<4;
    assert(!vm_space_check(&source,VM_USER_BASE+8,1,false));
    vm_space_activate(&source);
    assert(!vm_user_check(VM_USER_BASE+8,1,false));
    assert(vm_user_check_reason(VM_USER_BASE+8,1,false)==2);
    vm_space_activate(NULL);
    user_pt[0]&=~(1ULL<<4);
    /* Reject a stale non-present PTE rather than overwriting metadata. */
    user_pt[3]=1ULL<<5;
    uint64_t unused=page_alloc();assert(unused);
    assert(!vm_space_map(&source,3,unused,true,false));
    assert(page_free(unused));
    user_pt[3]=0;
    /* Privilege escalation in a second PDE or PML4 slot is not part of the
     * user region, but still grants the CPU access if it goes unnoticed. */
    {
        uint64_t old_pd_other=user_pd[17];
        user_pd[17]|=4ULL;
        assert(!vm_space_validate(&source));
        user_pd[17]=old_pd_other;
        user_pml4[1]=user_pml4[0];
        assert(!vm_space_validate(&source));
        user_pml4[1]=0;
        assert(vm_space_validate(&source));
    }
    struct vm_space post_access_clone={0};
    assert(vm_space_clone(&post_access_clone,&source));
    assert(vm_space_validate(&post_access_clone));
    vm_space_destroy(&post_access_clone);
    struct vm_space alias={0};
    assert(vm_space_create(&alias));
    uint64_t owned_alias=words(source.pt)[0]&0x000ffffffffff000ULL;
    assert(!vm_space_map(&alias,0,owned_alias,true,false));
    assert(!page_free(owned_alias));
    vm_space_destroy(&alias);
    uint64_t owned=words(source.pt)[0]&0x000ffffffffff000ULL;
    assert(vm_space_map(&source,2,owned,true,false)==false); /* no aliases */
    assert(!vm_space_map(&source,2,POOL_BASE+5000*PAGE,true,false));
    for(unsigned failure=1;failure<=6;failure++) {
        struct vm_space dst={0};unsigned before=remaining;
        fail_after(failure);
        assert(!vm_space_clone(&dst,&source));
        assert(!dst.pml4 && !dst.pt && remaining==before);
        uint8_t value=0;
        assert(vm_space_copyin(&source,&value,VM_USER_BASE+100,1) && value==1);
    }
    fail_after(0);
    struct vm_space cloned={0};
    assert(vm_space_clone(&cloned,&source));
    uint8_t change=99,read_back=0;
    assert(vm_space_copyout(&cloned,VM_USER_BASE+1,&change,1));
    assert(vm_space_copyin(&source,&read_back,VM_USER_BASE+1,1) && read_back==1);
    vm_space_destroy(&cloned);
    /* Anonymous mapping failure must not leak any partial mappings/frames. */
    for(unsigned failure=1;failure<=2;failure++) {
        unsigned before=remaining;
        fail_after(failure);
        assert(!vm_space_anonymous(&source,2));
        assert(source.anon_pages==0 && remaining==before);
    }
    fail_after(0);
    uint64_t anon=vm_space_anonymous(&source,2);
    assert(anon);
    uint64_t *leaves=words(source.pt);
    unsigned first=(unsigned)((anon-VM_USER_BASE)/PAGE);
    uint64_t saved=leaves[first+1];
    leaves[first+1]=0; /* deliberately corrupt one leaf in the test fixture */
    assert(!vm_space_unmap_anonymous(&source,anon,2));
    assert(!vm_space_protect_anonymous(&source,anon,2,false));
    assert(leaves[first]!=0); /* no partial destructive operation */
    leaves[first+1]=saved;
    assert(vm_space_protect_anonymous(&source,anon,2,false));
    assert(!vm_space_copyout(&source,anon,&change,1));
    assert(vm_space_copyin(&source,&read_back,anon,1) && read_back==0);
    assert(vm_space_unmap_anonymous(&source,anon,2));
    /* A partially invalid cross-page range must not modify its first page.
     * The copy routine validates the COMPLETE destination before copying. */
    uint64_t a=page_alloc();assert(a && vm_space_map(&source,4,a,true,false));
    assert(vm_space_validate(&source));
    uint8_t cross[32];memset(cross,0xe7,sizeof cross);
    uint8_t read_one=0;
    assert(!vm_space_copyout(&source,VM_USER_BASE+5*PAGE-8,cross,sizeof cross));
    assert(vm_space_copyin(&source,&read_one,VM_USER_BASE+5*PAGE-8,1) && read_one==0xa5);
    /* A page-table page cannot be aliased into userspace. */
    assert(!vm_space_map(&source,5,source.pt,true,false));
    assert(!vm_space_map(&source,5,source.pml4,true,false));
    uint64_t *source_pt=words(source.pt);
    uint64_t saved_pte=source_pt[0];
    /* W+X after corrupting an NX bit is rejected even for read-only access. */
    source_pt[0]&=~(1ULL<<63);
    assert(!vm_space_validate(&source));
    assert(!vm_space_check(&source,VM_USER_BASE,1,true));
    source_pt[0]=saved_pte;
    assert(vm_space_validate(&source));
    /* An ownership-preserving alias nevertheless violates the 1-owner PTE
     * invariant. It must not be cloned or silently accepted. */
    saved_pte=source_pt[4];
    source_pt[5]=saved_pte;
    unsigned before_clone_fail=remaining;
    struct vm_space invalid_clone={0};
    assert(!vm_space_validate(&source));
    assert(!vm_space_clone(&invalid_clone,&source));
    assert(remaining==before_clone_fail);
    source_pt[5]=0;
    assert(vm_space_validate(&source));
    /* Damage a page-table link; user-pointer checks must fail closed. */
    uint64_t *pd=words(source.pd),old_pd=pd[VM_USER_BASE>>21];
    pd[VM_USER_BASE>>21]=0;
    assert(!vm_space_validate(&source));
    assert(!vm_space_check(&source,VM_USER_BASE,1,false));
    pd[VM_USER_BASE>>21]=old_pd;
    assert(vm_space_validate(&source));
    /* Repeat eager fork and anonymous map/release and verify no frames are
     * lost. This remains a deterministic host stress, not real CR3/IRQ. */
    for(unsigned repeat=0;repeat<300;repeat++) {
        struct vm_space attempt={0};
        unsigned before_cycle=remaining;
        assert(vm_space_clone(&attempt,&source));
        assert(vm_space_validate(&attempt));
        uint64_t slot=vm_space_anonymous(&attempt,3);
        assert(slot);
        assert(vm_space_validate(&attempt));
        assert(vm_space_protect_anonymous(&attempt,slot,3,false));
        assert(!vm_space_copyout(&attempt,slot,cross,1));
        assert(vm_space_unmap_anonymous(&attempt,slot,3));
        vm_space_destroy(&attempt);
        assert(remaining==before_cycle);
    }
    vm_space_destroy(&source);
    /* A PTE pointing to another process's perfectly valid mapped frame
     * used to pass the global bitmap test. It must now fail OWNER checks.
     * Destruction must free only its original frame, not the victim's. */
    {
        unsigned before=remaining;
        struct vm_space attacker={0},victim={0};
        assert(vm_space_create(&attacker) && vm_space_create(&victim));
        uint64_t left=page_alloc(),right=page_alloc();
        assert(left && right && left!=right);
        assert(vm_space_map(&attacker,0,left,true,false));
        assert(vm_space_map(&victim,0,right,true,false));
        assert(vm_space_validate(&attacker) && vm_space_validate(&victim));
        uint64_t old=words(attacker.pt)[0];
        words(attacker.pt)[0]=(old&~0x000ffffffffff000ULL)|right;
        assert(!vm_space_validate(&attacker));
        assert(!vm_space_check(&attacker,VM_USER_BASE,1,false));
        vm_space_activate(&attacker);
        assert(!vm_user_check(VM_USER_BASE,1,false));
        assert(vm_user_check_reason(VM_USER_BASE,1,false)==9);
        vm_space_activate(NULL);
        vm_space_destroy(&attacker);
        assert(!page_is_allocated(left));
        assert(page_is_allocated(right) && page_is_mapped(right));
        assert(vm_space_validate(&victim));
        uint64_t byte=0x7f,got=0;
        assert(vm_space_copyout(&victim,VM_USER_BASE,&byte,1));
        assert(vm_space_copyin(&victim,&got,VM_USER_BASE,1) && got==byte);
        vm_space_destroy(&victim);
        assert(remaining==before);
    }
    /* Non-present but nonzero leaf flags are corruption, not a free slot. */
    {
        unsigned before=remaining;
        struct vm_space target={0};
        assert(vm_space_create(&target));
        uint64_t *leaves=words(target.pt);
        leaves[0]=2;
        uint64_t frame=page_alloc();assert(frame);
        assert(!vm_space_map(&target,0,frame,true,false));
        assert(!vm_space_validate(&target));
        assert(!page_is_mapped(frame));
        leaves[0]=0;
        assert(page_free(frame));
        vm_space_destroy(&target);
        assert(remaining==before);
    }
    /* A corrupted anonymous mapping is refused before any page is freed. */
    {
        unsigned before=remaining;
        struct vm_space target={0};
        assert(vm_space_create(&target));
        uint64_t start=vm_space_anonymous(&target,2);
        assert(start);
        assert(!vm_space_unmap_anonymous(&target,start+PAGE,UINT32_MAX));
        assert(!vm_space_protect_anonymous(&target,start+PAGE,UINT32_MAX,false));
        unsigned first=(unsigned)((start-VM_USER_BASE)/PAGE);
        uint64_t saved=target.owned[first+1];
        target.owned[first+1]=0;
        assert(!vm_space_protect_anonymous(&target,start,2,false));
        assert(!vm_space_unmap_anonymous(&target,start,2));
        target.owned[first+1]=saved;
        assert(vm_space_validate(&target));
        assert(vm_space_unmap_anonymous(&target,start,2));
        vm_space_destroy(&target);
        assert(remaining==before);
    }
    /* A corrupted source PTE must not turn into an invalid copied mapping. */
    struct vm_space bad={0},badcopy={0};
    assert(vm_space_create(&bad));
    uint64_t phys=page_alloc();assert(phys);
    assert(vm_space_map(&bad,0,phys,true,false));
    uint64_t pte=words(bad.pt)[0];
    words(bad.pt)[0]=(pte&~0x000ffffffffff000ULL)|(POOL_BASE+5000*PAGE);
    unsigned before_bad=remaining;
    assert(!vm_space_clone(&badcopy,&bad) && remaining==before_bad);
    assert(!vm_space_check(&bad,VM_USER_BASE,1,false));
    words(bad.pt)[0]=pte;
    vm_space_destroy(&bad);
    /* 3 firmware bootstrap pages + 2 kernel PT pages remain intentionally. */
    assert(remaining==POOL_FRAMES-5);
    assert(munmap(pool,POOL_FRAMES*PAGE)==0);
    puts("P0 VM poison-frame / table failure / clone rollback / ownership PASS");
    return 0;
}

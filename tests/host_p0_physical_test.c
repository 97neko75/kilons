/* Hosted validation of the real Multiboot2 parser and physical-frame bitmap.
 * Synthetic firmware data lives in a mapped low address; allocated frame
 * numbers are NEVER dereferenced by the test. No hardware privileges needed. */
#define _GNU_SOURCE
#include <kilons/memory.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

char __kernel_end[1];
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));
}
static void put64(uint8_t *p,uint64_t n) {
    put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));
}
static void entry(uint8_t *p,uint64_t start,uint64_t length,uint32_t kind) {
    put64(p,start);put64(p+8,length);put32(p+16,kind);put32(p+20,0);
}
int main(void) {
    void *mbi=MAP_FAILED;
    const uintptr_t places[]={0x10000000U,0x18000000U,0x08000000U,0x30000000U};
    for(size_t i=0;i<sizeof places/sizeof places[0];++i) {
        mbi=mmap((void *)places[i],4096,PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        if(mbi!=MAP_FAILED)break;
    }
    if(mbi==MAP_FAILED){fputs("cannot allocate low-address fixture\n",stderr);return 77;}
    uint8_t *p=mbi;
    memset(p,0,4096);
    /* Header (8), mmap tag (16 + 2 entries * 24), module tag (16), end (8). */
    put32(p,96);put32(p+4,0);
    put32(p+8,6);put32(p+12,64);put32(p+16,24);put32(p+20,0);
    entry(p+24,0x20000000ULL,128ULL*4096,1);
    entry(p+48,0x20022000ULL,4096,2);
    put32(p+72,3);put32(p+76,16);
    put32(p+80,0x20012001U);put32(p+84,0x20014fffU);
    put32(p+88,0);put32(p+92,8);
    assert(memory_init((uint32_t)(uintptr_t)mbi));
    assert(memory_free_pages()==124 && memory_validate());
    uint64_t allocated[128]={0};
    for(unsigned i=0;i<124;i++) {
        uint64_t pa=page_alloc();
        assert(pa>=0x20000000ULL && pa<0x20080000ULL);
        assert(pa!=0x20012000ULL && pa!=0x20013000ULL &&
               pa!=0x20014000ULL && pa!=0x20022000ULL);
        assert(page_is_allocated(pa));
        for(unsigned j=0;j<i;j++)assert(pa!=allocated[j]);
        allocated[i]=pa;
    }
    assert(memory_free_pages()==0 && page_alloc()==0 && memory_validate());
    assert(!page_free(0) && !page_free(0x20000001ULL));
    assert(!page_free(0x20012000ULL) && !page_is_allocated(0x20022000ULL));
    assert(page_claim_mapping(allocated[17]));
    assert(page_is_mapped(allocated[17]));
    assert(!page_claim_mapping(allocated[17]));
    assert(!page_free(allocated[17]));
    assert(page_release_mapping(allocated[17]));
    assert(!page_release_mapping(allocated[17]));
    assert(page_free(allocated[17]));
    assert(!page_free(allocated[17]));
    assert(memory_free_pages()==1);
    uint64_t again=page_alloc();
    assert(again==allocated[17] && memory_free_pages()==0);
    allocated[17]=again;
    for(unsigned i=0;i<124;i++)assert(page_free(allocated[i]));
    assert(memory_free_pages()==124 && memory_validate());
    /* Invalid entry geometry is rejected before trusting firmware entries. */
    put32(p+16,23); /* Multiboot2 mmap entry size must be >= 24 */
    assert(!memory_init((uint32_t)(uintptr_t)mbi));
    assert(page_alloc()==0 && memory_free_pages()==0 && !memory_validate());
    put32(p+16,24);
    /* The module must have a nonnegative physical size. */
    put32(p+84,0x20000000U);
    assert(!memory_init((uint32_t)(uintptr_t)mbi));
    put32(p+84,0x20014fffU);
    /* Missing memory map is rejected and cannot reuse previous state. */
    put32(p+8,99);
    assert(!memory_init((uint32_t)(uintptr_t)mbi));
    assert(page_alloc()==0 && memory_free_pages()==0 && !memory_validate());
    assert(munmap(mbi,4096)==0);
    puts("P0 physical ownership / no-free-while-mapped / double-free PASS");
    return 0;
}

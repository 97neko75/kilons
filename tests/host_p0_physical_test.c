/* Hosted validation of the real Limine memory-map parser and
 * physical-frame bitmap. Synthetic entries are never dereferenced. */
#define _GNU_SOURCE
#include <kilons/memory.h>
#include <kilons/limine.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int main(void) {
    struct limine_memmap_entry usable={0x20000000ULL,128ULL*4096,
                                       LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry reserved={0x20022000ULL,4096,
                                         LIMINE_MEMMAP_ACPI_RECLAIMABLE};
    struct limine_memmap_entry *entries[]={&usable,&reserved};
    struct limine_memmap_response map={0,2,entries};
    struct limine_file module={0,(void *)(uintptr_t)0x20012001ULL,0x2ffe,0,0,0,0};
    struct limine_file *modules[]={&module};
    struct limine_module_response module_map={0,1,modules};

    assert(memory_init(&map,&module_map,0,0,0));
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

    /* A zero-length firmware range is malformed and cannot be trusted. */
    usable.length=0;
    assert(!memory_init(&map,&module_map,0,0,0));
    assert(page_alloc()==0 && memory_free_pages()==0 && !memory_validate());
    usable.length=128ULL*4096;

    /* A module without a mapped address is also rejected before allocation. */
    module.address=0;
    assert(!memory_init(&map,&module_map,0,0,0));
    assert(page_alloc()==0 && memory_free_pages()==0 && !memory_validate());
    module.address=(void *)(uintptr_t)0x20012001ULL;

    /* Missing memory-map data cannot reuse the previous allocator state. */
    map.entries=0;
    assert(!memory_init(&map,&module_map,0,0,0));
    assert(page_alloc()==0 && memory_free_pages()==0 && !memory_validate());

    puts("P0 physical ownership / no-free-while-mapped / double-free PASS");
    return 0;
}

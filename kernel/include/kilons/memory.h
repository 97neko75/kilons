#ifndef KILONS_MEMORY_H
#define KILONS_MEMORY_H
#include <stdint.h>
#include <stdbool.h>
#define PAGE_SIZE 4096ULL
bool memory_init(uint32_t mbi_phys);
uint64_t page_alloc(void); /* physical address, zero on failure */
bool page_free(uint64_t phys);
/* Single-owner VM mapping claims. A mapped frame cannot be freed or mapped
 * a second time until the claim is explicitly released. UP only. */
bool page_claim_mapping(uint64_t phys);
bool page_release_mapping(uint64_t phys);
bool page_is_mapped(uint64_t phys);
/* Tests whether an aligned physical frame is currently allocator-owned. */
bool page_is_allocated(uint64_t phys);
uint32_t memory_free_pages(void);
/* Expensive full-frame allocator invariant audit; use at startup/tests. */
bool memory_validate(void);
#endif

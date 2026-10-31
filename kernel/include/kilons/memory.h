#ifndef KILONS_MEMORY_H
#define KILONS_MEMORY_H
#include <stdint.h>
#include <stdbool.h>
#define PAGE_SIZE 4096ULL
bool memory_init(uint32_t mbi_phys);
uint64_t page_alloc(void); /* physical address, zero on failure */
bool page_free(uint64_t phys);
uint32_t memory_free_pages(void);
#endif

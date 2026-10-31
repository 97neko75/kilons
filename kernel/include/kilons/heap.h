#ifndef KILONS_HEAP_H
#define KILONS_HEAP_H
#include <stddef.h>
#include <stdbool.h>
bool heap_init(void);
void *kmalloc(size_t bytes);
size_t heap_used_bytes(void);
size_t heap_capacity_bytes(void);
/* Bootstrap bump allocator, no kfree; stats are diagnostic only. */
#endif

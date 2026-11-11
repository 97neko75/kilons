#ifndef KILONS_HEAP_H
#define KILONS_HEAP_H
#include <stddef.h>
#include <stdbool.h>
bool heap_init(void);
void *kmalloc(size_t bytes);
/* false for NULL, outside/interior pointers, and repeated frees. */
bool kfree(void *ptr);
size_t heap_used_bytes(void);
size_t heap_capacity_bytes(void);
/* Full metadata/payload accounting audit; false before initialization. */
bool heap_validate(void);
/* 64 KiB bootstrap arena; capacity includes allocator metadata.
 * heap_used_bytes() reports currently allocated aligned payload. */
#endif

#ifndef KILONS_VM_H
#define KILONS_VM_H
#include <stdint.h>
#include <stdbool.h>
/* Bootstrap-only kernel mappings in the first GiB. */
#define VM_TEST_BASE 0x30000000ULL
#define VM_HEAP_BASE 0x31000000ULL
bool vm_init(void);
bool vm_map(uint64_t virt, uint64_t phys, uint64_t flags);
bool vm_unmap(uint64_t virt);
uint64_t vm_translate(uint64_t virt);
#define VM_WRITABLE 2ULL
#define VM_NX (1ULL << 63)
#endif

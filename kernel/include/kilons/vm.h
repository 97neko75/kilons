#ifndef KILONS_VM_H
#define KILONS_VM_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Bootstrap-only kernel mappings in the first GiB. */
#define VM_TEST_BASE 0x30000000ULL
#define VM_HEAP_BASE 0x31000000ULL
bool vm_init(void);
#define VM_USER_BASE 0x32000000ULL
#define VM_USER_PAGES 32U
struct vm_space { uint64_t pml4, pdpt, pd, pt; uint16_t anon_pages; };
bool vm_user_init(void);
bool vm_space_selftest(void);
bool vm_space_create(struct vm_space *space);
bool vm_space_clone(struct vm_space *dst,const struct vm_space *src);
/* Up to eight private anonymous pages at VA user-base+16*PAGE. */
uint64_t vm_space_anonymous(struct vm_space *space,unsigned pages);
bool vm_space_unmap_anonymous(struct vm_space *space,uint64_t addr,unsigned pages);
bool vm_space_protect_anonymous(struct vm_space *space,uint64_t addr,unsigned pages,bool writable);
void vm_space_destroy(struct vm_space *space);
void vm_space_activate(struct vm_space *space);
uint64_t vm_space_cr3(const struct vm_space *space);
bool vm_space_map(struct vm_space *space,unsigned index,uint64_t phys,bool writable,bool executable);
bool vm_space_check(const struct vm_space *space,uint64_t address,uint64_t length,bool writable);
bool vm_user_check(uint64_t addr,uint64_t len,bool writable);
/* Kernel reads/writes a user's private frames through their physical aliases. */
bool vm_space_copyin(const struct vm_space *space,void *dst,uint64_t src,size_t n);
bool vm_space_copyout(const struct vm_space *space,uint64_t dst,const void *src,size_t n);
bool vm_map(uint64_t virt, uint64_t phys, uint64_t flags);
bool vm_unmap(uint64_t virt);
uint64_t vm_translate(uint64_t virt);
#define VM_WRITABLE 2ULL
#define VM_NX (1ULL << 63)
#endif

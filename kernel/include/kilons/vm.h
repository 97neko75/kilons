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
/* owned[] is the authoritative frame ledger: a forged/corrupted PTE is
 * never sufficient grounds for releasing another address space's frame. */
struct vm_space {
    uint64_t pml4, pdpt, pd, pt;
    uint64_t owned[VM_USER_PAGES];
    uint16_t anon_pages;
};
bool vm_user_init(void);
#ifdef KILONS_HOST_TEST
/* HOST TEST ONLY: emulate CR3 without privileged instructions. */
void vm_host_set_cr3(uint64_t phys);
#endif
bool vm_space_selftest(void);
bool vm_space_create(struct vm_space *space);
bool vm_space_clone(struct vm_space *dst,const struct vm_space *src);
/* Bounded page-table/PTE ownership and W^X audit; useful before fork. */
bool vm_space_validate(const struct vm_space *space);
/* Up to eight private anonymous pages at VA user-base+16*PAGE. */
uint64_t vm_space_anonymous(struct vm_space *space,unsigned pages);
bool vm_space_unmap_anonymous(struct vm_space *space,uint64_t addr,unsigned pages);
bool vm_space_protect_anonymous(struct vm_space *space,uint64_t addr,unsigned pages,bool writable);
void vm_space_destroy(struct vm_space *space);
void vm_space_activate(struct vm_space *space);
uint64_t vm_space_cr3(const struct vm_space *space);
bool vm_space_map(struct vm_space *space,unsigned index,uint64_t phys,bool writable,bool executable);
bool vm_space_check(const struct vm_space *space,uint64_t address,uint64_t length,bool writable);
bool vm_space_check_execute(const struct vm_space *space,uint64_t address,uint64_t length);
bool vm_user_check(uint64_t addr,uint64_t len,bool writable);
/* Debug-only classification of a rejected user memory range; never bypass checks.
 * 0=valid, 1=address/length, 2=page-table chain, 3=absent leaf,
 * 4=not user, 5=unmapped physical page, 6=W^X, 7=write forbidden,
 * 8=unknown PTE flags, 9=physical frame not owned by this VM space. */
int vm_user_check_reason(uint64_t addr,uint64_t len,bool writable);
/* Kernel reads/writes a user's private frames through their physical aliases. */
bool vm_space_copyin(const struct vm_space *space,void *dst,uint64_t src,size_t n);
bool vm_space_copyout(const struct vm_space *space,uint64_t dst,const void *src,size_t n);
bool vm_map(uint64_t virt, uint64_t phys, uint64_t flags);
bool vm_unmap(uint64_t virt);
uint64_t vm_translate(uint64_t virt);
#define VM_WRITABLE 2ULL
#define VM_NX (1ULL << 63)
#endif

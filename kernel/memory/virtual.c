#include <kilons/vm.h>
#include <kilons/memory.h>
#include <stddef.h>
/* CR3 still points at bootstrap tables; first GiB is identity-mapped with 2 MiB pages.
 * Reserve two previously unused 2 MiB slots and replace their huge PDEs with PTs.
 * No general-purpose address-space or SMP support yet. */
#define P 1ULL
#define W 2ULL
#define HUGE (1ULL << 7)
#define ADDR 0x000ffffffffff000ULL
static uint64_t *test_pt, *heap_pt;
static uint64_t test_pt_phys, heap_pt_phys;
static bool ready;
static uint64_t cr3(void) { uint64_t v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v & ADDR; }
static void flush(uint64_t va) { __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory"); }
static uint64_t *directory(void) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)cr3();
    if (!(pml4[0] & P)) return NULL;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[0] & ADDR);
    if (!(pdpt[0] & P)) return NULL;
    return (uint64_t *)(uintptr_t)(pdpt[0] & ADDR);
}
static void zero_pt(uint64_t *pt) { for (unsigned i=0;i<512;i++) pt[i]=0; }
bool vm_init(void) {
    uint64_t *pd=directory();
    if (!pd || ready) return false;
    unsigned a=(unsigned)(VM_TEST_BASE >> 21), b=(unsigned)(VM_HEAP_BASE >> 21);
    if ((pd[a] & (P|HUGE)) != (P|HUGE) || (pd[b] & (P|HUGE)) != (P|HUGE)) return false;
    test_pt_phys=page_alloc(); heap_pt_phys=page_alloc();
    if (!test_pt_phys || !heap_pt_phys) return false;
    test_pt=(uint64_t *)(uintptr_t)test_pt_phys;
    heap_pt=(uint64_t *)(uintptr_t)heap_pt_phys;
    zero_pt(test_pt); zero_pt(heap_pt);
    /* These virtual ranges are now unmapped. They must not be used as physical aliases. */
    pd[a]=test_pt_phys|P|W; pd[b]=heap_pt_phys|P|W;
    for (uint64_t va=VM_TEST_BASE;va<VM_TEST_BASE+0x200000;va+=4096) flush(va);
    for (uint64_t va=VM_HEAP_BASE;va<VM_HEAP_BASE+0x200000;va+=4096) flush(va);
    ready=true;
    return true;
}
static uint64_t *entry(uint64_t va) {
    if (!ready || (va & 4095)) return NULL;
    if (va>=VM_TEST_BASE && va<VM_TEST_BASE+0x200000) return &test_pt[(va-VM_TEST_BASE)>>12];
    if (va>=VM_HEAP_BASE && va<VM_HEAP_BASE+0x200000) return &heap_pt[(va-VM_HEAP_BASE)>>12];
    return NULL;
}
bool vm_map(uint64_t va,uint64_t pa,uint64_t flags) {
    uint64_t *e=entry(va);
    if (!e || (pa & 4095) || !pa || (flags & ~(W)) || (*e & P)) return false;
    /* Supervisor-only; NX requires EFER.NXE, so not enabled here. */
    *e=pa|P|flags; flush(va); return true;
}
bool vm_unmap(uint64_t va) { uint64_t *e=entry(va); if (!e || !(*e&P)) return false; *e=0;flush(va);return true; }
uint64_t vm_translate(uint64_t va) { uint64_t *e=entry(va&~4095ULL); return e&&(*e&P) ? (*e&ADDR)|(va&4095) : 0; }

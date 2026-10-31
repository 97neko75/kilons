#include <kilons/heap.h>
#include <kilons/vm.h>
#include <kilons/memory.h>
#include <stdint.h>
#define HEAP_PAGES 16U
static size_t used;
static bool ready;
bool heap_init(void) {
    if (ready) return false;
    for (unsigned i=0;i<HEAP_PAGES;i++) {
        uint64_t phys=page_alloc();
        if (!phys || !vm_map(VM_HEAP_BASE+i*PAGE_SIZE,phys,VM_WRITABLE)) return false;
        uint8_t *p=(uint8_t *)(uintptr_t)(VM_HEAP_BASE+i*PAGE_SIZE);
        for (unsigned j=0;j<PAGE_SIZE;j++) p[j]=0;
    }
    used=0;ready=true;return true;
}
void *kmalloc(size_t bytes) {
    if (!ready || !bytes || bytes>SIZE_MAX-15) return 0;
    size_t n=(bytes+15)&~(size_t)15;
    if (n>HEAP_PAGES*PAGE_SIZE-used) return 0;
    void *p=(void *)(uintptr_t)(VM_HEAP_BASE+used);used+=n;return p;
}

size_t heap_used_bytes(void) { return used; }
size_t heap_capacity_bytes(void) { return HEAP_PAGES*PAGE_SIZE; }

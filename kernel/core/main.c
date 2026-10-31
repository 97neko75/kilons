#include <stdint.h>
#include <kilons/console.h>
#include <kilons/serial.h>
#include <kilons/memory.h>
#include <kilons/keyboard.h>
#include <kilons/interrupt.h>
#include <kilons/vm.h>
#include <kilons/heap.h>
#include <kilons/tss.h>
#include <kilons/scheduler.h>
#include <kilons/shell.h>
static void print_number(uint64_t n) {
    char buf[21]; unsigned i = sizeof(buf)-1; buf[i]=0;
    do { buf[--i]=(char)('0'+n%10); n/=10; } while (n);
    console_write(buf+i);
}
static void halt(void) { for (;;) __asm__ volatile ("cli; hlt"); }
static void worker_a(void) { for (;;) { thread_sleep(10); } }
static void worker_b(void) { for (;;) { thread_sleep(25); } }
void kernel_main(uint32_t multiboot_info_phys) {
    serial_init(); console_init();
    console_write("kilons: amd64 long mode entered\n");
    if (!memory_init(multiboot_info_phys)) {
        console_write("kilons: ERROR invalid Multiboot2 memory map\n"); halt();
    }
    uint32_t before = memory_free_pages();
    console_write("kilons: usable free 4KiB frames: "); print_number(before); console_write("\n");
    uint64_t a = page_alloc(), b = page_alloc();
    if (!a || !b || a == b || memory_free_pages() != before-2 ||
        !page_free(a) || !page_free(b) || memory_free_pages() != before || page_free(a)) {
        console_write("kilons: ERROR physical allocator self-test failed\n"); halt();
    }
    console_write("kilons: physical page allocator self-test passed\n");
    tss_init();
    console_write("kilons: GDT/TSS and IST initialized\n");
    if (!vm_init()) { console_write("kilons: ERROR VM init\n"); halt(); }
    uint64_t frame=page_alloc();
    if (!frame || !vm_map(VM_TEST_BASE,frame,VM_WRITABLE) || vm_translate(VM_TEST_BASE)!=frame) {
        console_write("kilons: ERROR VM map test\n"); halt();
    }
    volatile uint64_t *test=(volatile uint64_t *)(uintptr_t)VM_TEST_BASE;
    *test=0x4b494c4f4e53ULL;
    if (*test!=0x4b494c4f4e53ULL || !vm_unmap(VM_TEST_BASE) || vm_translate(VM_TEST_BASE) || !page_free(frame)) {
        console_write("kilons: ERROR VM unmap test\n"); halt();
    }
    console_write("kilons: virtual mapping self-test passed\n");
    if (!heap_init()) { console_write("kilons: ERROR heap init\n"); halt(); }
    uint64_t *heap_sample=kmalloc(sizeof(uint64_t)*8);
    if (!heap_sample) { console_write("kilons: ERROR kmalloc\n"); halt(); }
    for (unsigned i=0;i<8;i++) heap_sample[i]=i+100;
    for (unsigned i=0;i<8;i++) if (heap_sample[i]!=i+100) { console_write("kilons: ERROR heap test\n"); halt(); }
    console_write("kilons: bootstrap heap self-test passed\n");
    interrupt_init();
    console_write("kilons: IDT/PIC/PIT initialized (100 Hz nominal)\n");
    scheduler_init();
    if (thread_create(worker_a)<0 || thread_create(worker_b)<0) {
        console_write("kilons: ERROR creating worker threads\n"); halt();
    }
    console_write("kilons: cooperative scheduler and timed sleeping initialized\n");
    debug_shell_run();
}

#include <stdint.h>
#include <kilons/io.h>
#include <kilons/console.h>
#include <kilons/interrupt.h>
#include <kilons/keyboard.h>

struct __attribute__((packed)) gate { uint16_t low; uint16_t selector; uint8_t ist; uint8_t flags; uint16_t mid; uint32_t high; uint32_t reserved; };
struct __attribute__((packed)) idtr { uint16_t limit; uint64_t base; };
static struct gate idt[256];
extern void (*interrupt_stub_table[48])(void);
static volatile uint64_t ticks;
/* 15 pushed registers, followed by vector, error code, RIP, CS, RFLAGS. */
struct frame { uint64_t regs[15]; uint64_t vector, error, rip, cs, flags; };
static void print_hex(uint64_t v) {
    const char digits[] = "0123456789abcdef";
    console_write("0x");
    for (int i = 60; i >= 0; i -= 4) console_putc(digits[(v >> i) & 15]);
}
static void panic_halt(void) { for (;;) __asm__ volatile("cli; hlt"); }
void interrupt_dispatch(const struct frame *f) {
    uint64_t n = f->vector;
    if (n < 32) {
        console_write("\nkilons: CPU exception vector="); print_hex(n);
        console_write(" error="); print_hex(f->error);
        console_write(" RIP="); print_hex(f->rip);
        if (n == 14) { uint64_t cr2; __asm__ volatile("mov %%cr2,%0":"=r"(cr2)); console_write(" CR2="); print_hex(cr2); }
        console_write("\n"); panic_halt();
    }
    if (n == 32) ++ticks;
    else if (n == 33) keyboard_irq();
    /* IRQ 32..47: slave must be acknowledged before master. */
    if (n >= 40 && n < 48) outb(0xa0, 0x20);
    if (n >= 32 && n < 48) outb(0x20, 0x20);
}
uint64_t interrupt_ticks(void) {
    uint64_t v;
    __asm__ volatile("pushfq; cli" ::: "memory");
    v = ticks;
    __asm__ volatile("popfq" ::: "memory");
    return v;
}
static void set_gate(unsigned n, uint64_t p) {
    uint8_t ist = n == 8 ? 1 : (n == 14 ? 2 : 0);
    idt[n] = (struct gate){(uint16_t)p, 0x08, ist, 0x8e, (uint16_t)(p >> 16), (uint32_t)(p >> 32), 0};
}
void interrupt_init(void) {
    __asm__ volatile("cli");
    for (unsigned i = 0; i < 48; ++i) set_gate(i, (uint64_t)interrupt_stub_table[i]);
    struct idtr d = {(uint16_t)(sizeof(idt)-1), (uint64_t)idt};
    __asm__ volatile("lidt %0" :: "m"(d) : "memory");
    /* Remap 8259 PIC: master 0x20..0x27, slave 0x28..0x2f. */
    outb(0x20,0x11); outb(0xa0,0x11);
    outb(0x21,0x20); outb(0xa1,0x28);
    outb(0x21,0x04); outb(0xa1,0x02);
    outb(0x21,0x01); outb(0xa1,0x01);
    outb(0x21,0xfc); /* IRQ0 timer and IRQ1 keyboard only */
    outb(0xa1,0xff);
    /* PIT channel 0, mode 3, approximately 100 Hz. */
    uint16_t divisor = 11932;
    outb(0x43,0x36); outb(0x40,(uint8_t)divisor); outb(0x40,(uint8_t)(divisor>>8));
    __asm__ volatile("sti");
}

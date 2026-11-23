#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kilons/io.h>
#include <kilons/console.h>
#include <kilons/interrupt.h>
#include <kilons/keyboard.h>
#include <kilons/ring3.h>
#include <kilons/irq_guard.h>

/*2026\11\9彩蛋！这是开发者的话！
 * 这个操作系统。。。。。。
 * 还行哦，等v1.0的时候，我一定会当作主力系统的！*/

struct __attribute__((packed)) gate { uint16_t low; uint16_t selector; uint8_t ist; uint8_t flags; uint16_t mid; uint32_t high; uint32_t reserved; };
struct __attribute__((packed)) idtr { uint16_t limit; uint64_t base; };
static struct gate idt[256];
extern void (*interrupt_stub_table[48])(void);
extern void ring3_syscall_stub(void);
static volatile uint64_t ticks;
#define PIT_DIVISOR 11932U
static uint16_t pit_previous;
static uint64_t pit_polled_periods;
static bool pit_timer_ready;
/* 15 pushed registers, followed by vector, error code, RIP, CS, RFLAGS. */
struct frame { uint64_t regs[15]; uint64_t vector,error,rip,cs,flags,rsp,ss; };
_Static_assert(offsetof(struct frame,vector)==120, "IRQ vector offset");
_Static_assert(offsetof(struct frame,rip)==136, "IRQ iret RIP offset");
_Static_assert(offsetof(struct frame,rsp)==160, "IRQ user RSP offset");
static void print_hex(uint64_t v) {
    const char digits[] = "0123456789abcdef";
    console_write("0x");
    for (int i = 60; i >= 0; i -= 4) console_putc(digits[(v >> i) & 15]);
}
static void panic_halt(void) { for (;;) __asm__ volatile("cli; hlt"); }
int interrupt_dispatch(const struct frame *f) {
    uint64_t n = f->vector;
    if (n < 32) {
        if ((f->cs & 3U)==3U) {
            uint64_t address=0;
            if(n==14) __asm__ volatile("mov %%cr2,%0":"=r"(address));
            (void)ring3_preempt(f->regs,f->rip,f->cs,f->flags,f->rsp,f->ss);
            ring3_on_fault(n,f->error,f->rip,address);
            return 1; /* abandon faulting user frame, restore kernel launcher */
        }
        console_write("\nkilons: CPU exception vector="); print_hex(n);
        console_write(" error="); print_hex(f->error);
        console_write(" RIP="); print_hex(f->rip);
        if (n == 14) { uint64_t cr2; __asm__ volatile("mov %%cr2,%0":"=r"(cr2)); console_write(" CR2="); print_hex(cr2); }
        console_write("\n"); panic_halt();
    }
    int switch_user=0;
    if (n == 32) {
        uint64_t old_ticks=ticks;
        interrupt_poll_timer();
        /* A poll may already have accounted for this same PIT period. */
        if(pit_polled_periods) --pit_polled_periods;
        else ++ticks;
        if (ticks/5!=old_ticks/5) keyboard_service(); /* Recover buffered input after a missed IRQ1. */
        /* Time slice: 5 PIT ticks = ~50ms. Never preempt a CPL0 handler. */
        if ((f->cs&3U)==3U && ticks/5!=old_ticks/5)
            switch_user=ring3_preempt(f->regs,f->rip,f->cs,f->flags,f->rsp,f->ss);
    } else if (n == 33) keyboard_service();
    /* IRQ 32..47: slave must be acknowledged before master. */
    if (n >= 40 && n < 48) outb(0xa0, 0x20);
    if (n >= 32 && n < 48) outb(0x20, 0x20);
    return switch_user;
}
uint64_t interrupt_ticks(void) {
    uint64_t v;
    __asm__ volatile("pushfq; cli" ::: "memory");
    v = ticks;
    __asm__ volatile("popfq" ::: "memory");
    return v;
}
void interrupt_poll_timer(void) {
    if(!pit_timer_ready)return;
    uint64_t flags=kilons_irq_save();
    /* Latch channel 0 without changing its mode or reload value. This counter
     * keeps running even when firmware leaves the PIC/APIC IRQ route broken.
     * The Ring 3 idle loop polls it, so nanosleep and timed waits still wake. */
    outb(0x43,0x00);
    uint16_t current=inb(0x40);
    current|=(uint16_t)inb(0x40)<<8;
    if(current>PIT_DIVISOR) {
        pit_previous=current;
        kilons_irq_restore(flags);
        return;
    }
    /* Channel 0 counts down and reloads at each period. A rising count is
     * the reload edge. Polling in the dispatcher idle path accounts for that
     * edge even if the PIC never delivers IRQ0. A later IRQ consumes the
     * corresponding credit instead of counting the period twice. */
    if(current>pit_previous) {
        ++ticks;
        if(pit_polled_periods!=UINT64_MAX) ++pit_polled_periods;
    }
    pit_previous=current;
    kilons_irq_restore(flags);
}
static void set_gate(unsigned n, uint64_t p) {
    uint8_t ist = n == 8 ? 1 : (n == 14 ? 2 : 0);
    idt[n] = (struct gate){(uint16_t)p, 0x08, ist, 0x8e, (uint16_t)(p >> 16), (uint32_t)(p >> 32), 0};
}
/* This single-CPU interrupt backend uses the legacy 8259 PIC.
 * Firmware/bootloaders may leave the local APIC enabled with LINT0 masked,
 * which prevents PIC IRQ0/IRQ1 from reaching the processor. Select legacy
 * delivery before unmasking the PIC. Clear EN and EXTD together so an
 * inherited x2APIC state also transitions directly to the disabled state. */
static void select_pic_delivery(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    if (edx & (1U << 9)) {
        uint32_t low, high;
        __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1b));
        if (low & (1U << 11)) {
            low &= ~((1U << 11) | (1U << 10));
            __asm__ volatile("wrmsr" :: "c"(0x1b), "a"(low), "d"(high) : "memory");
        }
    }
    /* Some PC-compatible chipsets route the 8259 output through IMCR.
     * Explicitly select direct PIC delivery after a bootloader handoff. */
    outb(0x22, 0x70);
    uint8_t imcr = inb(0x23);
    outb(0x23, (uint8_t)(imcr & ~1U));
}
void interrupt_init(void) {
    __asm__ volatile("cli");
    for (unsigned i = 0; i < 48; ++i) set_gate(i, (uint64_t)interrupt_stub_table[i]);
    set_gate(0x80,(uint64_t)ring3_syscall_stub);
    idt[0x80].flags=0xee; /* DPL3 interrupt gate */
    struct idtr d = {(uint16_t)(sizeof(idt)-1), (uint64_t)idt};
    __asm__ volatile("lidt %0" :: "m"(d) : "memory");
    select_pic_delivery();
    /* Remap 8259 PIC: master 0x20..0x27, slave 0x28..0x2f. */
    outb(0x20,0x11); outb(0xa0,0x11);
    outb(0x21,0x20); outb(0xa1,0x28);
    outb(0x21,0x04); outb(0xa1,0x02);
    outb(0x21,0x01); outb(0xa1,0x01);
    outb(0x21,0xff);
    outb(0xa1,0xff);
    const char *keyboard_error = keyboard_init();
    console_write("kilons: i8042: ");
    console_write(keyboard_error ? keyboard_error : "port 1 ready (translated set 2)");
    console_write("\n");
    outb(0x21,keyboard_error ? 0xfe : 0xfc);
    /* PIT channel 0, mode 2, approximately 100 Hz. Its counter reloads once
     * per IRQ period, which lets the idle poll detect missed timer delivery. */
    uint16_t divisor=PIT_DIVISOR;
    outb(0x43,0x34); outb(0x40,(uint8_t)divisor); outb(0x40,(uint8_t)(divisor>>8));
    pit_polled_periods=0;
    outb(0x43,0x00);
    pit_previous=inb(0x40);
    pit_previous|=(uint16_t)inb(0x40)<<8;
    pit_timer_ready=pit_previous<=PIT_DIVISOR;
    __asm__ volatile("sti");
}

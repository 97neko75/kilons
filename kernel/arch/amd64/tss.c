#include <stdint.h>
#include <stddef.h>
/* Reinstall GDT using the same CS/DS selectors as bootstrap (0x08/0x10).
 * IST1 handles double fault, IST2 page fault. */
struct __attribute__((packed)) tss64 {
    uint32_t reserved0; uint64_t rsp0,rsp1,rsp2; uint64_t reserved1;
    uint64_t ist1,ist2,ist3,ist4,ist5,ist6,ist7;
    uint64_t reserved2; uint16_t reserved3,iomap;
};
struct __attribute__((packed)) gdtr { uint16_t limit; uint64_t base; };
static uint64_t gdt[5] __attribute__((aligned(16)));
static struct tss64 tss;
static uint8_t df_stack[8192] __attribute__((aligned(16)));
static uint8_t pf_stack[8192] __attribute__((aligned(16)));
void tss_init(void) {
    gdt[0]=0;gdt[1]=0x00af9a000000ffffULL;gdt[2]=0x00af92000000ffffULL;
    tss.ist1=(uint64_t)(uintptr_t)(df_stack+sizeof(df_stack));
    tss.ist2=(uint64_t)(uintptr_t)(pf_stack+sizeof(pf_stack));
    tss.iomap=sizeof(tss);
    uint64_t base=(uint64_t)(uintptr_t)&tss;
    uint64_t lim=sizeof(tss)-1;
    gdt[3]=(lim&0xffff)|((base&0xffffff)<<16)|(0x89ULL<<40)|(((lim>>16)&15)<<48)|(((base>>24)&255)<<56);
    gdt[4]=base>>32;
    struct gdtr desc={(uint16_t)(sizeof(gdt)-1),(uint64_t)(uintptr_t)gdt};
    __asm__ volatile("lgdt %0" :: "m"(desc) : "memory");
    uint16_t selector=0x18;
    __asm__ volatile("ltr %0" :: "r"(selector) : "memory");
}

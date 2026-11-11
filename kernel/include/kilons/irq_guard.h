#ifndef KILONS_IRQ_GUARD_H
#define KILONS_IRQ_GUARD_H
#include <stdint.h>
/* UP kernel only: this prevents interrupt reentry, NOT SMP concurrency.
 * Always restore the caller's original IF instead of blindly enabling IRQs. */
static inline uint64_t kilons_irq_save(void) {
#ifdef KILONS_HOST_TEST
    return 0;
#else
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
#endif
}
static inline void kilons_irq_restore(uint64_t flags) {
#ifndef KILONS_HOST_TEST
    if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
    else __asm__ volatile("" ::: "memory");
#else
    (void)flags;
#endif
}
#endif

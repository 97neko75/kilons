#include <stdint.h>
#include <stddef.h>
#include <kilons/scheduler.h>
#include <kilons/interrupt.h>
#include <kilons/irq_guard.h>

#define MAX_THREADS 4
#define STACK_BYTES 16384
/* Each context is a saved stack pointer; assembly saves callee-saved registers. */
struct thread { uint64_t rsp; unsigned active; unsigned finished; uint64_t wake_at; uint64_t runs; };
static struct thread threads[MAX_THREADS];
static uint8_t stacks[MAX_THREADS-1][STACK_BYTES] __attribute__((aligned(16)));
static unsigned current;
static uint64_t switches;
static uint64_t wakeups, sleeps;
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

void scheduler_init(void) {
    for (unsigned i=0;i<MAX_THREADS;i++) {
        threads[i].rsp=0; threads[i].active=0; threads[i].finished=0; threads[i].wake_at=0; threads[i].runs=0;
    }
    threads[0].active=1; current=0; switches=0; wakeups=0; sleeps=0;
}

extern void thread_start(void);
void thread_run(void (*entry)(void)) __attribute__((noreturn));
void thread_run(void (*entry)(void)) {
    __asm__ volatile("sti" ::: "memory");
    entry();
    thread_exit();
}

int thread_create(void (*entry)(void)) {
    if (!entry) return -1;
    uint64_t irq_flags=kilons_irq_save();
    for (unsigned i=1;i<MAX_THREADS;i++) if (!threads[i].active &&
            !threads[i].wake_at && (threads[i].finished || !threads[i].rsp)) {
        uintptr_t sp=(uintptr_t)(stacks[i-1]+STACK_BYTES);
        sp &= ~(uintptr_t)15;
        /* Context switch pops r15,r14,r13,r12,rbx,rbp then ret. */
        sp -= 8; *(uint64_t *)sp=(uint64_t)(uintptr_t)entry;
        sp -= 8; *(uint64_t *)sp=(uint64_t)(uintptr_t)thread_start;
        for (unsigned r=0;r<6;r++) { sp-=8; *(uint64_t *)sp=0; }
        threads[i].rsp=sp; threads[i].active=1; threads[i].finished=0;
        threads[i].wake_at=0; threads[i].runs=0;
        kilons_irq_restore(irq_flags);
        return (int)i;
    }
    kilons_irq_restore(irq_flags);
    return -1;
}

/* The bootstrap thread is the always-runnable scheduler anchor. On this
 * single CPU it is intentionally never put to sleep. This prevents the
 * final worker from switching to a non-runnable context when all workers
 * are waiting for PIT ticks. A future idle thread can replace this rule. */
static void wake_due_threads(uint64_t now) {
    for (unsigned i=1;i<MAX_THREADS;i++) {
        if (threads[i].wake_at && now>=threads[i].wake_at &&
            !threads[i].finished) {
            threads[i].wake_at=0;
            threads[i].active=1;
            wakeups++;
        }
    }
}
static unsigned next_runnable(void) {
    for (unsigned step=1;step<=MAX_THREADS;step++) {
        unsigned candidate=(current+step)%MAX_THREADS;
        if (threads[candidate].active && !threads[candidate].finished)
            return candidate;
    }
    /* The boot thread cannot sleep or exit, so this must always exist. */
    return 0;
}
void thread_yield(void) {
    /* Preserve the original IF per *thread*. A context switch suspends this
     * call; its IRQ restoration occurs when the same thread resumes. */
    uint64_t flags=kilons_irq_save();
    wake_due_threads(interrupt_ticks());
    unsigned next=next_runnable();
    if (next != current) {
        unsigned old=current;
        current=next;
        switches++;
        threads[next].runs++;
        context_switch(&threads[old].rsp,threads[next].rsp);
    }
    kilons_irq_restore(flags);
}

void thread_sleep(uint64_t ticks) {
    if (!ticks) { thread_yield(); return; }
    uint64_t flags=kilons_irq_save();
    if (current==0) {
        /* A missing runnable idle context must never be a deadlock. */
        kilons_irq_restore(flags);
        thread_yield();
        return;
    }
    uint64_t now=interrupt_ticks();
    /* Zero is the non-sleeping sentinel; saturate when adding a long delay.
     * Compare deadlines as UNSIGNED. The former signed subtraction caused
     * far-future deadlines (e.g. UINT64_MAX) to appear immediately due. */
    uint64_t deadline=UINT64_MAX-now<ticks?UINT64_MAX:now+ticks;
    if (!deadline) deadline=UINT64_MAX;
    threads[current].wake_at=deadline;
    threads[current].active=0;
    sleeps++;
    thread_yield();
    kilons_irq_restore(flags);
}
void thread_exit(void) {
    (void)kilons_irq_save();
    if (current==0) {
        /* The scheduler anchor cannot return or be recycled. */
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    threads[current].active=0;
    threads[current].wake_at=0;
    threads[current].finished=1;
    unsigned next=next_runnable();
    unsigned old=current;
    current=next;
    switches++;
    threads[next].runs++;
    context_switch(&threads[old].rsp,threads[next].rsp);
    /* A finished worker must not be dispatched again. */
    for (;;) __asm__ volatile("cli; hlt" ::: "memory");
}
unsigned scheduler_thread_count(void) {
    uint64_t flags=kilons_irq_save();
    unsigned count=0;
    for (unsigned i=0;i<MAX_THREADS;i++) if (threads[i].active) count++;
    kilons_irq_restore(flags);
    return count;
}
uint64_t scheduler_switch_count(void) { uint64_t f=kilons_irq_save(); uint64_t n=switches; kilons_irq_restore(f); return n; }

unsigned scheduler_sleeping_count(void) {
    uint64_t flags=kilons_irq_save();
    unsigned count=0;
    for (unsigned i=0;i<MAX_THREADS;i++) if (threads[i].wake_at) count++;
    kilons_irq_restore(flags);
    return count;
}

uint64_t scheduler_wakeup_count(void) { uint64_t f=kilons_irq_save(); uint64_t n=wakeups; kilons_irq_restore(f); return n; }
uint64_t scheduler_sleep_count(void) { uint64_t f=kilons_irq_save(); uint64_t n=sleeps; kilons_irq_restore(f); return n; }
uint64_t scheduler_thread_runs(unsigned id) { uint64_t f=kilons_irq_save(); uint64_t n=id<MAX_THREADS ? threads[id].runs : 0; kilons_irq_restore(f); return n; }
unsigned scheduler_thread_slots(void) { return MAX_THREADS; }

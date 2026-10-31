#include <stdint.h>
#include <stddef.h>
#include <kilons/scheduler.h>
#include <kilons/interrupt.h>

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
    for (unsigned i=1;i<MAX_THREADS;i++) if (!threads[i].active && !threads[i].finished) {
        uintptr_t sp=(uintptr_t)(stacks[i-1]+STACK_BYTES);
        sp &= ~(uintptr_t)15;
        /* Context switch pops r15,r14,r13,r12,rbx,rbp then ret. */
        sp -= 8; *(uint64_t *)sp=(uint64_t)(uintptr_t)entry;
        sp -= 8; *(uint64_t *)sp=(uint64_t)(uintptr_t)thread_start;
        for (unsigned r=0;r<6;r++) { sp-=8; *(uint64_t *)sp=0; }
        threads[i].rsp=sp; threads[i].active=1; threads[i].wake_at=0; threads[i].runs=0;
        return (int)i;
    }
    return -1;
}

void thread_yield(void) {
    /* Prevent PIT IRQ during scheduler state updates and context switching. */
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    uint64_t now=interrupt_ticks();
    for (unsigned i=0;i<MAX_THREADS;i++) {
        if (threads[i].wake_at && (int64_t)(now-threads[i].wake_at)>=0) {
            threads[i].wake_at=0; threads[i].active=1; wakeups++;
        }
    }
    unsigned next=current;
    for (unsigned step=1;step<=MAX_THREADS;step++) {
        unsigned candidate=(current+step)%MAX_THREADS;
        if (threads[candidate].active) { next=candidate; break; }
    }
    if (next != current) {
        unsigned old=current; current=next; switches++; threads[next].runs++;
        context_switch(&threads[old].rsp,threads[next].rsp);
    }
    if (flags & (1ULL<<9)) __asm__ volatile("sti" ::: "memory");
}

void thread_sleep(uint64_t ticks) {
    if (!ticks) { thread_yield(); return; }
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    threads[current].wake_at=interrupt_ticks()+ticks;
    threads[current].active=0; sleeps++;
    /* thread 0 remains runnable; workers can sleep without deadlock. */
    thread_yield();
    if (flags & (1ULL<<9)) __asm__ volatile("sti" ::: "memory");
}
void thread_exit(void) {
    __asm__ volatile("cli" ::: "memory");
    threads[current].active=0;
    threads[current].finished=1;
    for (;;) {
        unsigned next=MAX_THREADS;
        for (unsigned step=1;step<=MAX_THREADS;step++) {
            unsigned candidate=(current+step)%MAX_THREADS;
            if (threads[candidate].active) { next=candidate; break; }
        }
        if (next==MAX_THREADS) __asm__ volatile("hlt");
        unsigned old=current; current=next; switches++; threads[next].runs++;
        context_switch(&threads[old].rsp,threads[next].rsp);
    }
}
unsigned scheduler_thread_count(void) {
    unsigned count=0;
    for (unsigned i=0;i<MAX_THREADS;i++) if (threads[i].active) count++;
    return count;
}
uint64_t scheduler_switch_count(void) { return switches; }

unsigned scheduler_sleeping_count(void) {
    unsigned count=0;
    for (unsigned i=0;i<MAX_THREADS;i++) if (threads[i].wake_at) count++;
    return count;
}

uint64_t scheduler_wakeup_count(void) { return wakeups; }
uint64_t scheduler_sleep_count(void) { return sleeps; }
uint64_t scheduler_thread_runs(unsigned id) { return id<MAX_THREADS ? threads[id].runs : 0; }
unsigned scheduler_thread_slots(void) { return MAX_THREADS; }

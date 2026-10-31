#ifndef KILONS_SCHEDULER_H
#define KILONS_SCHEDULER_H
#include <stdint.h>
/* Cooperative kernel threads with PIT-based timed sleeping. Not preemptive. */
void scheduler_init(void);
int thread_create(void (*entry)(void));
void thread_yield(void);
void thread_sleep(uint64_t ticks);
void thread_exit(void) __attribute__((noreturn));
unsigned scheduler_thread_count(void);
unsigned scheduler_sleeping_count(void);
uint64_t scheduler_switch_count(void);
uint64_t scheduler_wakeup_count(void);
uint64_t scheduler_sleep_count(void);
uint64_t scheduler_thread_runs(unsigned id);
unsigned scheduler_thread_slots(void);
#endif

/* Hosted single-CPU scheduler state-machine regression. The assembly context
 * switch is a no-op fixture, so this tests queue/deadline accounting only;
 * stack switching and interrupt entry still require QEMU tests. */
#include <kilons/scheduler.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t now;
static unsigned switch_calls;
uint64_t interrupt_ticks(void) { return now; }
void context_switch(uint64_t *old_rsp,uint64_t new_rsp) {
    assert(old_rsp && new_rsp);
    *old_rsp=0x12340000ULL; /* emulate saving the suspended root context */
    ++switch_calls;
}
void thread_start(void) {}
static void worker(void) {}
int main(void) {
    scheduler_init();
    assert(scheduler_thread_slots()==4 && scheduler_thread_count()==1);
    thread_yield();
    assert(!switch_calls && scheduler_switch_count()==0);
    assert(thread_create(NULL)==-1);
    assert(thread_create(worker)==1);
    assert(thread_create(worker)==2);
    assert(thread_create(worker)==3);
    assert(thread_create(worker)==-1); /* no slot clobber */
    assert(scheduler_thread_count()==4);
    thread_yield(); /* hosted context fixture: conceptual current=1 */
    assert(switch_calls==1 && scheduler_thread_runs(1)==1);
    thread_sleep(UINT64_MAX); /* do not wake at tick 1 or at 2^63 */
    assert(scheduler_sleep_count()==1);
    assert(scheduler_sleeping_count()==1);
    assert(scheduler_thread_count()==3);
    now=10;
    thread_yield();
    assert(scheduler_sleeping_count()==1 && scheduler_wakeup_count()==0);
    thread_sleep(7); /* current=3; due at tick 17 */
    assert(scheduler_sleeping_count()==2);
    now=16;
    thread_yield();
    assert(scheduler_wakeup_count()==0);
    now=17;
    thread_yield();
    assert(scheduler_wakeup_count()==1 && scheduler_sleeping_count()==1);
    now=UINT64_MAX-1;
    thread_yield();
    assert(scheduler_sleeping_count()==1);
    now=UINT64_MAX;
    thread_yield();
    assert(scheduler_sleeping_count()==0 && scheduler_wakeup_count()==2);
    assert(scheduler_switch_count()==switch_calls);
    assert(scheduler_thread_runs(99)==0);
    puts("P0 scheduler always-runnable bootstrap / saturated deadline / wakeup PASS");
    return 0;
}

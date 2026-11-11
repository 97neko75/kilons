/* Process state stress: bounded capacity, zombie lifecycle and orphan reaping. */
#include <kilons/process.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void start_stop_collect(uint32_t parent,uint32_t pid) {
    assert(process_create_child(pid,parent));
    assert(process_start(pid));
    assert(process_exit(pid,(int)(pid&255)));
    assert(process_state_of(pid)==PROCESS_ZOMBIE);
    int status=-1;
    assert(process_collect(parent,pid,&status));
    assert(status==(int)(pid&255));
    assert(process_state_of(pid)==PROCESS_FREE);
}
int main(void) {
    /* PID zero is not a parent allowed to wait on orphan records. */
    int orphan_status=777;
    assert(!process_collect(0,123,&orphan_status));
    assert(process_collect_any(0,&orphan_status)==-10);
    assert(orphan_status==777);
    assert(process_validate());
    assert(process_create_child(1,0));
    assert(process_start(1));
    process_set_reaper(1);
    assert(!process_create_child(1,0));
    for(uint32_t i=2;i<=16;i++)assert(process_create_child(i,1));
    assert(process_live_count()==16 && process_ready_count()==15);
    assert(!process_create_child(17,1)); /* bounded table, no overwrite */
    for(uint32_t i=2;i<=16;i++) {
        assert(process_start(i));
        assert(process_exit(i,(int)i));
    }
    assert(process_live_count()==1 && process_zombie_count()==15);
    assert(!process_create_child(99,1)); /* zombies cost slots until waited */
    for(uint32_t i=2;i<=16;i++) {
        int status=-1;
        assert(process_collect(1,i,&status) && status==(int)i);
    }
    assert(process_zombie_count()==0);
    for(uint32_t i=100;i<10100;i++) {
        start_stop_collect(1,i);
        if((i&255U)==0)assert(process_validate());
    }
    assert(process_validate());
    /* Reject invalid state transitions and preserve live state. */
    assert(process_create_child(20000,1));
    assert(!process_exit(20000,1));
    assert(!process_block(20000));
    assert(process_start(20000));
    assert(!process_start(20000));
    assert(process_block(20000));
    assert(!process_suspend(20000));
    assert(process_wake(20000));
    assert(process_start(20000));
    assert(process_exit(20000,19));
    int transition_status=0;
    assert(process_collect(1,20000,&transition_status));
    assert(transition_status==19);

    /* Parent dies while child is alive: reparent to the still-live reaper. */
    assert(process_create_child(3000,1));
    assert(process_start(3000));
    assert(process_create_child(3001,3000));
    assert(process_start(3001));
    assert(process_exit(3000,0));
    assert(process_parent_of(3001)==1);
    assert(process_exit(3001,77));
    int status=-1;
    assert(process_collect(1,3001,&status) && status==77);
    assert(process_collect(1,3000,&status) && status==0);
    /* An unpublished parent may be rolled back after READY children exist.
     * The children must be adopted by the live reaper, not stranded. */
    assert(process_create_child(3500,1));
    assert(process_create_child(3501,3500));
    assert(process_abort(3500));
    assert(process_parent_of(3501)==1);
    assert(process_start(3501));
    assert(process_exit(3501,123));
    assert(process_collect(1,3501,&status) && status==123);
    assert(!process_abort(3500));
    assert(!process_wake(3501) && !process_suspend(3501));
    /* An unregistered reaper must not leave zombie records orphaned. */
    process_set_reaper(999999);
    assert(process_create_child(3600,1));
    assert(process_start(3600));
    assert(process_create_child(3601,3600));
    assert(process_start(3601));
    assert(process_exit(3601,0));
    assert(process_exit(3600,0));
    assert(process_state_of(3601)==PROCESS_FREE);
    assert(process_collect(1,3600,&status));
    process_set_reaper(1);
    /* Without any reaper, an orphan's zombie record is auto-discarded. */
    assert(process_create_child(4000,1));
    assert(process_start(4000));
    assert(process_exit(1,0));
    assert(process_parent_of(4000)==0);
    assert(process_exit(4000,9));
    assert(process_state_of(4000)==PROCESS_FREE);
    assert(process_live_count()==0 && process_zombie_count()==0);
    assert(process_validate());
    puts("P0 process guarded transitions / rollback adoption / orphan reclaim PASS");
    return 0;
}

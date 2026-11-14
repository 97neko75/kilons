/* Hosted state-machine regression; no OS-specific hardware required. */
#include <kilons/process.h>
#include <assert.h>
#include <stdio.h>
static void group_test(void) {
    int status;
    assert(process_create_child(701,1) && process_create_child(702,1));
    assert(process_group_id(701,0,false)==1 && process_group_id(701,0,true)==1);
    assert(process_set_group(1,701,0)==0);
    assert(process_set_group(1,702,701)==0);
    assert(process_new_session(701)==-1);
    assert(process_group_id(1,702,false)==701);
    process_mark_exec(702);
    assert(process_set_group(1,702,0)==-13);
    assert(process_set_group(702,701,0)==-3);
    assert(process_set_group(702,0,1234)==-1);
    assert(process_set_group(702,0,-1)==-22);
    assert(process_collect_selected(1,0,&status)==-10);
    assert(process_collect_selected(1,-701,&status)==0);
    assert(process_new_session(702)==702 && process_group_id(702,0,true)==702);
    assert(process_set_group(702,0,701)==-1);
    assert(process_job_signal(1,702,19,0,0)==0 && process_is_stopped(702));
    assert(process_wait_events(1,702,0,&status,false,0,2)==702 && status==((19<<8)|127));
    assert(process_job_signal(1,702,18,0,0)==0 && !process_is_stopped(702));
    assert(process_wait_events(1,702,0,&status,false,0,4)==702 && status==0xffff);
    assert(process_start(702) && process_exit(702,9));
    assert(process_collect_selected(1,-701,&status)==0);
    process_note_uid(702,1001,1001);
    uint32_t uid=0;
    assert(process_wait_event(1,-702,0,&status,true,&uid)==702 && status==9 && uid==1001);
    assert(process_collect_selected(1,-702,&status)==702 && status==9);
    assert(process_start(701) && process_exit(701,7));
    assert(process_collect_selected(1,-701,&status)==701 && status==7);
}
int main(void) {
    assert(process_create_child(1,0));
    process_set_reaper(1);
    assert(process_start(1));
    assert(!process_create_child(1,0));
    assert(process_collect_any(1,(int[]){0})==-10);
    group_test();
    assert(process_create_child(703,1) && process_create_child(704,703));
    assert(process_collect_selected(703,0,(int[]){0})==0);
    assert(process_set_group(1,703,0)==0);
    assert(process_start(704) && process_exit(704,42));
    assert(process_collect_selected(703,0,(int[]){0})==-10);
    assert(process_collect_group(703,1,(int[]){0})==704);
    assert(process_start(703) && process_exit(703,0));
    assert(process_collect_any(1,(int[]){0})==703);
    for(unsigned i=0;i<100;i++) {
        uint32_t child=2+i;
        int status=-1;
        assert(process_create_child(child,1));
        assert(process_parent_of(child)==1);
        assert(!process_create_child(child,1));
        assert(process_start(child));
        assert(process_suspend(child));
        assert(process_start(child));
        assert(process_block(1));
        assert(process_exit(child,42));
        assert(process_wake(1));
        assert(process_start(1));
        assert(process_collect(1,child,&status) && status==42);
        assert(process_state_of(child)==PROCESS_FREE);
        assert(process_live_count()==1 && process_zombie_count()==0);
    }
    assert(process_collect_any(1,(int[]){0})==-10);
    /* Failed fork can roll back a READY slot without leaving zombies. */
    assert(process_create_child(400,1));
    assert(process_abort(400));
    assert(!process_abort(400));
    assert(process_state_of(400)==PROCESS_FREE);
    assert(process_ready_count()==0);
    assert(process_blocked_count()==0);
    assert(process_create_child(500,1));
    assert(process_create_child(501,1));
    assert(process_start(500));
    assert(process_exit(500,17));
    int result=-1;
    assert(process_collect_any(1,&result)==500 && result==17);
    assert(process_collect_any(1,&result)==0);
    assert(process_start(501));
    assert(process_exit(501,23));
    assert(process_collect_any(1,&result)==501 && result==23);
    assert(process_collect_any(1,&result)==-10);
    assert(process_create_child(600,1));
    assert(process_start(600));
    assert(process_exit(1,0));
    /* Live children of a dying init become orphans, not owned by a zombie. */
    assert(process_is_child(0,600));
    assert(process_exit(600,1));
    puts("process state / blocking / unique PID / wait / reaping PASS");
    return 0;
}

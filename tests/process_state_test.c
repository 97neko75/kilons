/* Hosted state-machine regression; no OS-specific hardware required. */
#include <kilons/process.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(process_create_child(1,0));
    process_set_reaper(1);
    assert(process_start(1));
    assert(!process_create_child(1,0));
    assert(process_collect_any(1,(int[]){0})==-10);
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

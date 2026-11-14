#include <kilons/process.h>
#include <kilons/irq_guard.h>
#include <stddef.h>

/* Bounded UP process-state table. Protect each state transition from IRQ
 * reentry, and never expose a partially changed record to a reader. This is
 * NOT an SMP lock and does not turn the task scheduler into a SMP scheduler. */
#define PROCESS_SLOTS 16U
struct process_record {
    uint32_t uid,saved_uid;
    uint32_t pid,parent,pgid,sid;
    bool execed,stopped,stop_event,continue_event;
    enum process_state state;
    int status,stop_signal;
};
static struct process_record records[PROCESS_SLOTS];
static uint32_t reaper;

static struct process_record *find_locked(uint32_t pid) {
    if(!pid)return NULL;
    for(unsigned i=0;i<PROCESS_SLOTS;i++)
        if(records[i].state!=PROCESS_FREE && records[i].pid==pid)return &records[i];
    return NULL;
}
static void clear_record(struct process_record *p) { *p=(struct process_record){0}; }
static bool can_adopt(uint32_t pid,uint32_t dying) {
    struct process_record *p=find_locked(pid);
    return pid && pid!=dying && p && p->state!=PROCESS_ZOMBIE;
}
/* Rehome children exactly once whenever a parent is removed, whether by a
 * normal exit or an unpublished task rollback. Dead orphans without a living
 * reaper cannot ever be wait()ed, so reclaim just their zombie records. */
static void orphan_children(uint32_t dying) {
    uint32_t adopt=can_adopt(reaper,dying)?reaper:0;
    for(unsigned i=0;i<PROCESS_SLOTS;i++) {
        struct process_record *child=&records[i];
        if(child->state==PROCESS_FREE || child->parent!=dying)continue;
        if(adopt)child->parent=adopt;
        else if(child->state==PROCESS_ZOMBIE)clear_record(child);
        else child->parent=0;
    }
}
void process_set_reaper(uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    reaper=can_adopt(pid,0)?pid:0;
    kilons_irq_restore(flags);
}
bool process_abort(uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    bool ok=p && p->state==PROCESS_READY;
    if(ok) {
        if(reaper==pid)reaper=0;
        orphan_children(pid);
        clear_record(p);
    }
    kilons_irq_restore(flags);
    return ok;
}
bool process_create_child(uint32_t pid,uint32_t parent) {
    uint64_t flags=kilons_irq_save();
    bool ok=false;
    if(!pid || find_locked(pid))goto out;
    struct process_record *ancestor=find_locked(parent);
    if(parent && (!ancestor || ancestor->state==PROCESS_ZOMBIE))goto out;
    for(unsigned i=0;i<PROCESS_SLOTS;i++)if(records[i].state==PROCESS_FREE) {
        records[i]=(struct process_record){.pid=pid,.parent=parent,.state=PROCESS_READY,
            .uid=ancestor?ancestor->uid:1000,.saved_uid=ancestor?ancestor->saved_uid:1000,
            .pgid=ancestor?ancestor->pgid:pid,.sid=ancestor?ancestor->sid:pid};
        ok=true;break;
    }
out:
    kilons_irq_restore(flags);
    return ok;
}
static bool transition(uint32_t pid,enum process_state from,enum process_state to) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    bool ok=p && p->state==from;
    if(ok)p->state=to;
    kilons_irq_restore(flags);
    return ok;
}
bool process_start(uint32_t pid) {return transition(pid,PROCESS_READY,PROCESS_RUNNING);}
bool process_suspend(uint32_t pid) {return transition(pid,PROCESS_RUNNING,PROCESS_READY);}
bool process_block(uint32_t pid) {return transition(pid,PROCESS_RUNNING,PROCESS_BLOCKED);}
bool process_wake(uint32_t pid) {return transition(pid,PROCESS_BLOCKED,PROCESS_READY);}
bool process_exit(uint32_t pid,int status) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    bool ok=p && p->state==PROCESS_RUNNING;
    if(ok) {
        p->status=status;
        p->state=PROCESS_ZOMBIE;
        if(reaper==pid)reaper=0;
        orphan_children(pid);
        if(p->parent==0)clear_record(p);
    }
    kilons_irq_restore(flags);
    return ok;
}
bool process_is_child(uint32_t parent,uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    bool yes=p && p->parent==parent;
    kilons_irq_restore(flags);
    return yes;
}
uint32_t process_parent_of(uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    uint32_t result=p?p->parent:0;
    kilons_irq_restore(flags);
    return result;
}
bool process_collect(uint32_t parent,uint32_t pid,int *status) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    bool ok=parent!=0 && status && p && p->parent==parent && p->state==PROCESS_ZOMBIE;
    if(ok){*status=p->status;clear_record(p);}
    kilons_irq_restore(flags);
    return ok;
}
int process_wait_events(uint32_t parent,int selector,uint32_t group,int *status,bool peek,uint32_t *uid,unsigned events) {
    if(!status || !parent)return -10;
    uint64_t flags=kilons_irq_save();
    int result=-10;
    struct process_record *caller=find_locked(parent);
    if(!caller){kilons_irq_restore(flags);return result;}
    if(selector==0 && group==0)group=caller->pgid;
    if(selector< -1)group=(uint32_t)(-(int64_t)selector);
    for(unsigned i=0;i<PROCESS_SLOTS;i++) {
        struct process_record *p=&records[i];
        if(p->state==PROCESS_FREE || p->parent!=parent)continue;
        if(selector>0 && p->pid!=(unsigned)selector)continue;
        if((selector==0 || selector< -1) && p->pgid!=group)continue;
        if(result==-10)result=0;  /* has child, but may still be running */
        if(p->state!=PROCESS_ZOMBIE && ((p->stop_event && (events&2)) || (p->continue_event && (events&4)))) {
            result=(int)p->pid;
            if(uid)*uid=p->uid;
            if(p->stop_event && (events&2)) {
                *status=(p->stop_signal<<8)|127;
                if(!peek)p->stop_event=false;
            } else {
                *status=0xffff;
                if(!peek)p->continue_event=false;
            }
            break;
        }
        if(p->state==PROCESS_ZOMBIE && (events&1)) {
            result=(int)p->pid;
            *status=p->status;
            if(uid)*uid=p->uid;
            if(!peek)clear_record(p);
            break;
        }
    }
    kilons_irq_restore(flags);
    return result;
}
int process_wait_event(uint32_t parent,int selector,uint32_t group,int *status,bool peek,uint32_t *uid) {
    return process_wait_events(parent,selector,group,status,peek,uid,1);
}
bool process_is_stopped(uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    bool stopped=p && p->stopped;
    kilons_irq_restore(flags);
    return stopped;
}
void process_stop(uint32_t pid,int sig) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    if(p && !p->stopped) {
        p->stopped=true;
        p->stop_signal=sig;
        p->stop_event=true;
        p->continue_event=false;
    }
    kilons_irq_restore(flags);
}
int process_send_signal(uint32_t caller,int target,int sig,uint32_t real,uint32_t effective,
                        void (*queue)(uint32_t,int)) {
    uint64_t flags=kilons_irq_save();
    struct process_record *self=find_locked(caller);
    int rc=-3;
    for(unsigned i=0;self && i<PROCESS_SLOTS;i++) {
        struct process_record *p=&records[i];
        if(p->state==PROCESS_FREE)continue;
        if(target>0 && (uint32_t)target!=p->pid)continue;
        if(!target && p->pgid!=self->pgid)continue;
        if(target< -1 && p->pgid!=(uint32_t)(-(int64_t)target))continue;
        if(target==-1 && p->pid==1)continue;
        if(effective && real!=p->uid && effective!=p->uid &&
           real!=p->saved_uid && effective!=p->saved_uid && !(sig==18 && p->sid==self->sid)) {
            if(rc==-3)rc=-1;
            continue;
        }
        rc=0;
        if(p->state==PROCESS_ZOMBIE)continue;
        if(sig==9)p->stopped=false;
        if(sig && queue)queue(p->pid,sig);
        if(sig==19 && !p->stopped) {
            p->stopped=true;p->stop_signal=sig;p->stop_event=true;p->continue_event=false;
        } else if(sig==18 && p->stopped) {
            p->stopped=false;p->continue_event=true;p->stop_event=false;
        }
    }
    kilons_irq_restore(flags);
    return rc;
}
int process_job_signal(uint32_t caller,int target,int sig,uint32_t real,uint32_t effective) {
    if(sig!=0 && sig!=19 && sig!=18)return -22;
    return process_send_signal(caller,target,sig,real,effective,NULL);
}
int process_collect_selected(uint32_t parent,int selector,int *status) {
    return process_wait_event(parent,selector,0,status,false,0);
}
int process_collect_group(uint32_t parent,uint32_t group,int *status) {
    return process_wait_event(parent,0,group,status,false,0);
}
int process_collect_any(uint32_t parent,int *status) {
    return process_collect_selected(parent,-1,status);
}
void process_note_uid(uint32_t pid,uint32_t uid,uint32_t saved) {
    uint64_t flags=kilons_irq_save();
    struct process_record *record=find_locked(pid);
    if(record){record->uid=uid;record->saved_uid=saved;}
    kilons_irq_restore(flags);
}
void process_mark_exec(uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    if(p)p->execed=true;
    kilons_irq_restore(flags);
}
static struct process_record *group_locked(uint32_t pgid) {
    for(unsigned i=0;i<PROCESS_SLOTS;i++)
        if(records[i].state!=PROCESS_FREE && records[i].pgid==pgid)return &records[i];
    return NULL;
}
int process_group_id(uint32_t caller,int pid,bool session) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid?(uint32_t)pid:caller);
    int result=p?(int)(session?p->sid:p->pgid):-3;
    kilons_irq_restore(flags);
    return result;
}
int process_set_group(uint32_t caller,int pid,int pgid) {
    if(pgid<0)return -22;
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid?(uint32_t)pid:caller);
    struct process_record *self=find_locked(caller);
    int rc=0;
    if(!self || !p || (p!=self && p->parent!=caller))rc=-3;
    else if(p!=self && p->execed)rc=-13;
    else if(p->sid!=self->sid || p->pid==p->sid)rc=-1;
    else {
        unsigned group=pgid?(unsigned)pgid:p->pid;
        struct process_record *member=group_locked(group);
        if(group!=p->pid && (!member || member->sid!=self->sid))rc=-1;
        else p->pgid=group;
    }
    kilons_irq_restore(flags);
    return rc;
}
int process_new_session(uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    int rc=-1;
    if(p && !group_locked(pid)) {
        p->sid=p->pgid=pid;
        rc=(int)pid;
    }
    kilons_irq_restore(flags);
    return rc;
}
enum process_state process_state_of(uint32_t pid) {
    uint64_t flags=kilons_irq_save();
    struct process_record *p=find_locked(pid);
    enum process_state state=p?p->state:PROCESS_FREE;
    kilons_irq_restore(flags);
    return state;
}
static unsigned count_states(unsigned mask) {
    uint64_t flags=kilons_irq_save();
    unsigned n=0;
    for(unsigned i=0;i<PROCESS_SLOTS;i++)
        if(mask & (1U<<records[i].state))n++;
    kilons_irq_restore(flags);
    return n;
}
unsigned process_live_count(void) {
    return count_states((1U<<PROCESS_READY)|(1U<<PROCESS_RUNNING)|(1U<<PROCESS_BLOCKED));
}
unsigned process_zombie_count(void) {return count_states(1U<<PROCESS_ZOMBIE);}
unsigned process_ready_count(void) {return count_states(1U<<PROCESS_READY);}
unsigned process_blocked_count(void) {return count_states(1U<<PROCESS_BLOCKED);}

/* Verify process-state invariants independently of caller-facing state
 * transitions. The table is tiny (16 slots), so exhaustive checking is
 * preferable to silently tolerating a broken PID/parent relationship. */
bool process_validate(void) {
    uint64_t flags=kilons_irq_save();
    bool ok=true;
    if(reaper && !can_adopt(reaper,0))ok=false;
    for(unsigned i=0;i<PROCESS_SLOTS && ok;i++) {
        const struct process_record *p=&records[i];
        if(p->state==PROCESS_FREE) {
            if(p->pid || p->parent || p->status)ok=false;
            continue;
        }
        if(p->state<PROCESS_READY || p->state>PROCESS_ZOMBIE ||
           !p->pid || p->pid==p->parent ||
           (p->state==PROCESS_ZOMBIE && !p->parent)) {ok=false;break;}
        if(p->parent) {
            const struct process_record *parent=find_locked(p->parent);
            if(!parent || parent->state==PROCESS_ZOMBIE) {ok=false;break;}
        }
        for(unsigned j=i+1;j<PROCESS_SLOTS;j++)
            if(records[j].state!=PROCESS_FREE && records[j].pid==p->pid)
                {ok=false;break;}
    }
    kilons_irq_restore(flags);
    return ok;
}

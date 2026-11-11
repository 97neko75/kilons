#include <kilons/process.h>
#include <kilons/irq_guard.h>
#include <stddef.h>

/* Bounded UP process-state table. Protect each state transition from IRQ
 * reentry, and never expose a partially changed record to a reader. This is
 * NOT an SMP lock and does not turn the task scheduler into a SMP scheduler. */
#define PROCESS_SLOTS 16U
struct process_record {
    uint32_t pid,parent;
    enum process_state state;
    int status;
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
    if(parent) {
        struct process_record *p=find_locked(parent);
        if(!p || p->state==PROCESS_ZOMBIE)goto out;
    }
    for(unsigned i=0;i<PROCESS_SLOTS;i++)if(records[i].state==PROCESS_FREE) {
        records[i]=(struct process_record){pid,parent,PROCESS_READY,0};
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
int process_collect_any(uint32_t parent,int *status) {
    if(!status || !parent)return -10;
    uint64_t flags=kilons_irq_save();
    int result=-10;
    for(unsigned i=0;i<PROCESS_SLOTS;i++) {
        struct process_record *p=&records[i];
        if(p->state==PROCESS_FREE || p->parent!=parent)continue;
        if(result==-10)result=0;  /* has child, but may still be running */
        if(p->state==PROCESS_ZOMBIE) {
            result=(int)p->pid;
            *status=p->status;
            clear_record(p);
            break;
        }
    }
    kilons_irq_restore(flags);
    return result;
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

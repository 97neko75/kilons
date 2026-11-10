#include <kilons/process.h>
#include <stddef.h>
#define PROCESS_SLOTS 16U
struct process_record { uint32_t pid,parent; enum process_state state; int status; };
static struct process_record records[PROCESS_SLOTS];
static uint32_t reaper;
static struct process_record *find(uint32_t pid) {
    if(!pid)return NULL;
    for(unsigned i=0;i<PROCESS_SLOTS;i++)
        if(records[i].state!=PROCESS_FREE && records[i].pid==pid) return &records[i];
    return NULL;
}
void process_set_reaper(uint32_t pid) {reaper=pid;}
bool process_abort(uint32_t pid) {
    struct process_record *p=find(pid);
    if(!p || p->state!=PROCESS_READY)return false;
    *p=(struct process_record){0};return true;
}
bool process_create_child(uint32_t pid,uint32_t parent) {
    if(!pid || find(pid))return false;
    if(parent) {
        struct process_record *p=find(parent);
        if(!p || p->state==PROCESS_ZOMBIE)return false;
    }
    for(unsigned i=0;i<PROCESS_SLOTS;i++) if(records[i].state==PROCESS_FREE) {
        records[i]=(struct process_record){pid,parent,PROCESS_READY,0};
        return true;
    }
    return false;
}
bool process_start(uint32_t pid) {
    struct process_record *p=find(pid);
    if(!p || p->state!=PROCESS_READY)return false;
    p->state=PROCESS_RUNNING;return true;
}
bool process_suspend(uint32_t pid) {
    struct process_record *p=find(pid);
    if(!p || p->state!=PROCESS_RUNNING)return false;
    p->state=PROCESS_READY;return true;
}
bool process_block(uint32_t pid) {
    struct process_record *p=find(pid);
    if(!p || p->state!=PROCESS_RUNNING)return false;
    p->state=PROCESS_BLOCKED;return true;
}
bool process_wake(uint32_t pid) {
    struct process_record *p=find(pid);
    if(!p || p->state!=PROCESS_BLOCKED)return false;
    p->state=PROCESS_READY;return true;
}
bool process_exit(uint32_t pid,int status) {
    struct process_record *p=find(pid);
    if(!p || p->state!=PROCESS_RUNNING)return false;
    p->status=status;p->state=PROCESS_ZOMBIE;
    /* A dying parent must not strand its children forever. */
    if(reaper==pid)reaper=0;
    for(unsigned i=0;i<PROCESS_SLOTS;i++) if(records[i].parent==pid && records[i].pid!=pid) {
        if(reaper && find(reaper))records[i].parent=reaper;
        else if(records[i].state==PROCESS_ZOMBIE)records[i]=(struct process_record){0};
        else records[i].parent=0;
    }
    return true;
}
bool process_is_child(uint32_t parent,uint32_t pid) {
    struct process_record *p=find(pid);return p && p->parent==parent;
}
uint32_t process_parent_of(uint32_t pid) {
    struct process_record *p=find(pid);return p?p->parent:0;
}
bool process_collect(uint32_t parent,uint32_t pid,int *status) {
    struct process_record *p=find(pid);
    if(!p || p->parent!=parent || p->state!=PROCESS_ZOMBIE || !status)return false;
    *status=p->status;*p=(struct process_record){0};return true;
}
int process_collect_any(uint32_t parent,int *status) {
    if(!status)return -10;
    bool has_child=false;
    for(unsigned i=0;i<PROCESS_SLOTS;i++) if(records[i].state!=PROCESS_FREE && records[i].parent==parent) {
        has_child=true;
        if(records[i].state==PROCESS_ZOMBIE) {
            int pid=(int)records[i].pid;
            *status=records[i].status;
            records[i]=(struct process_record){0};
            return pid;
        }
    }
    return has_child?0:-10;
}
enum process_state process_state_of(uint32_t pid) {
    struct process_record *p=find(pid);return p?p->state:PROCESS_FREE;
}
unsigned process_live_count(void) {
    unsigned n=0;
    for(unsigned i=0;i<PROCESS_SLOTS;i++) if(records[i].state==PROCESS_READY ||
       records[i].state==PROCESS_RUNNING || records[i].state==PROCESS_BLOCKED)++n;
    return n;
}
unsigned process_zombie_count(void) {
    unsigned n=0;
    for(unsigned i=0;i<PROCESS_SLOTS;i++) if(records[i].state==PROCESS_ZOMBIE)++n;
    return n;
}

unsigned process_ready_count(void) {
    unsigned n=0;for(unsigned i=0;i<PROCESS_SLOTS;i++)if(records[i].state==PROCESS_READY)++n;return n;
}
unsigned process_blocked_count(void) {
    unsigned n=0;for(unsigned i=0;i<PROCESS_SLOTS;i++)if(records[i].state==PROCESS_BLOCKED)++n;return n;
}

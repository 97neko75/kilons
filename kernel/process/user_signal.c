#include <kilons/user_signal.h>
static uint64_t bit(int sig) { return 1ULL<<sig; }
static bool valid_restorer(const struct vm_space *vm,uint64_t address) {
    /* libc's private restorer is deliberately a seven-byte ABI sequence:
     * mov $15,%eax; int $0x80.  Checking the bytes prevents a process from
     * registering an arbitrary executable address and later manufacturing a
     * signal-return frame by jumping to address+7. */
    static const uint8_t expected[KILONS_SIGNAL_RESTORER_BYTES]={
        0xb8,0x0f,0x00,0x00,0x00,0xcd,0x80
    };
    uint8_t actual[KILONS_SIGNAL_RESTORER_BYTES];
    if(!address || !vm_space_copyin(vm,actual,address,sizeof actual))return false;
    for(unsigned i=0;i<sizeof actual;i++)if(actual[i]!=expected[i])return false;
    return true;
}
static uint64_t blockable(uint64_t mask) {
    return mask & KILONS_SIGNAL_SET & ~(bit(SIGSTOP)|bit(SIGKILL));
}
bool signal_valid(int sig) {
    return sig>0 && sig<32 && (bit(sig)&KILONS_SIGNAL_SET);
}
bool signal_stop(int sig) {
    return sig==SIGSTOP || sig==SIGTSTP || sig==SIGTTIN || sig==SIGTTOU;
}
bool signal_ignored(const struct user_signals *s,int sig) {
    uint64_t handler=s->actions[sig].handler;
    return handler==1 || (!handler && (sig==SIGCONT || sig==SIGCHLD || sig==SIGURG || sig==SIGWINCH));
}
void signal_queue(struct user_signals *s,int sig) {
    if(signal_stop(sig))s->pending&=~bit(SIGCONT);
    if(sig==SIGCONT)s->pending&=~(bit(SIGSTOP)|bit(SIGTSTP)|bit(SIGTTIN)|bit(SIGTTOU));
    if(sig!=SIGSTOP && !signal_ignored(s,sig))s->pending|=bit(sig);
}
int signal_ready(const struct user_signals *s) {
    uint64_t ready=s->pending & ~s->mask;
    if(ready & bit(SIGKILL))return SIGKILL;
    for(int sig=1;sig<32;sig++)if(ready & bit(sig))return sig;
    return 0;
}
void signal_after_exec(struct user_signals *s) {
    s->depth=0;
    s->suspended=false;
    for(int sig=1;sig<32;sig++) {
        if(s->actions[sig].handler!=1)s->actions[sig]=(struct user_action){0};
        s->restorers[sig]=0;
        if(signal_ignored(s,sig))s->pending&=~bit(sig);
    }
}
long signal_action(struct user_signals *s,const struct vm_space *vm,const struct user_context *r) {
    int sig=(int)r->rdi;
    if(!signal_valid(sig) || (r->rsi && (sig==SIGKILL || sig==SIGSTOP)))return -22;
    struct user_action action;
    if(r->rsi) {
        if(!vm_space_copyin(vm,&action,r->rsi,sizeof action))return -14;
        if(action.flags & ~(SA_RESTART|SA_NODEFER|SA_RESETHAND|SA_NOCLDSTOP|SA_NOCLDWAIT) ||
           action.reserved)return -22;
        if(action.handler>1 && (!vm_space_check_execute(vm,action.handler,1) ||
                                !vm_space_check_execute(vm,r->r10,KILONS_SIGNAL_RESTORER_BYTES) ||
                                !valid_restorer(vm,r->r10)))return -14;
        action.mask=blockable(action.mask);
        action.reserved=0;
    }
    if(r->rdx && !vm_space_copyout(vm,r->rdx,&s->actions[sig],sizeof action))return -14;
    if(r->rsi) {
        s->actions[sig]=action;
        s->restorers[sig]=r->r10;
        if(signal_ignored(s,sig))s->pending&=~bit(sig);
    }
    return 0;
}
long signal_mask(struct user_signals *s,const struct vm_space *vm,const struct user_context *r) {
    uint64_t next=s->mask,set;
    if(r->rsi) {
        if(!vm_space_copyin(vm,&set,r->rsi,sizeof set))return -14;
        switch(r->rdi) {
        case SIG_BLOCK: next|=set; break;
        case SIG_UNBLOCK: next&=~set; break;
        case SIG_SETMASK: next=set; break;
        default: return -22;
        }
    }
    if(r->rdx && !vm_space_copyout(vm,r->rdx,&s->mask,sizeof s->mask))return -14;
    s->mask=blockable(next);
    return 0;
}
long signal_suspend(struct user_signals *s,const struct vm_space *vm,uint64_t address) {
    uint64_t mask;
    if(!vm_space_copyin(vm,&mask,address,sizeof mask))return -14;
    s->suspend_mask=s->mask;
    s->suspended=true;
    s->mask=blockable(mask);
    return 0;
}
bool signal_enter(struct user_signals *s,const struct vm_space *vm,struct user_context *r,int sig) {
    if(!s||!vm||!r||!signal_valid(sig)||r->rsp<136U)return false;
    struct user_action action=s->actions[sig];
    uint64_t sp=((r->rsp-128)&~15ULL)-8;
    if(s->depth==32 || (action.handler>1 &&
        !valid_restorer(vm,s->restorers[sig])) ||
        !vm_space_copyout(vm,sp,&s->restorers[sig],sizeof(uint64_t)))return false;
    struct saved_handler *frame=&s->frames[s->depth++];
    frame->context=*r;
    frame->mask=s->suspended?s->suspend_mask:s->mask;
    frame->restorer=s->restorers[sig];
    s->suspended=false;
    s->mask|=action.mask;
    if(!(action.flags&SA_NODEFER))s->mask|=bit(sig);
    if(action.flags&SA_RESETHAND)s->actions[sig]=(struct user_action){0};
    r->rsp=sp;
    r->rip=action.handler;
    r->rdi=(unsigned)sig;
    r->flags&=~(1UL<<10);
    return true;
}
bool signal_return(struct user_signals *s,struct user_context *r) {
    if(!s->depth || !r)return false;
    struct saved_handler *frame=&s->frames[s->depth-1];
    /* Only the fixed libc restorer sequence (mov eax,15; int 0x80) may
     * consume a saved signal frame. A user process must not be able to invoke
     * the private return syscall and manufacture arbitrary register state. */
    if(!frame->restorer || frame->restorer>UINT64_MAX-KILONS_SIGNAL_RESTORER_BYTES ||
       r->rip!=frame->restorer+KILONS_SIGNAL_RESTORER_BYTES)return false;
    s->depth--;
    *r=frame->context;
    s->mask=frame->mask;
    return true;
}

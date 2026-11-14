#include "include/signal.h"
#include "include/errno.h"
#include "unistd.h"
#include "syscall_internal.h"
extern void kilons_signal_restore(void);

static int result(long value) {
    if(value>=0)return (int)value;
    errno=(int)-value;
    return -1;
}
int sigaction(int sig,const struct sigaction *action,struct sigaction *old) {
    return result(k_syscall6(13,sig,(long)action,(long)old,
                             (long)kilons_signal_restore,0,0));
}
void (*signal(int sig,void (*handler)(int)))(int) {
    struct sigaction action={.sa_handler=handler,.sa_flags=SA_RESTART},old;
    if(sigaction(sig,&action,&old)<0)return SIG_ERR;
    return old.sa_handler;
}
static unsigned long bit(int sig) {
    if(sig<=0 || sig>=32 || !(KILONS_SIGNAL_SET & (1UL<<sig))) {
        errno=EINVAL;
        return 0;
    }
    return 1UL<<sig;
}
int sigemptyset(sigset_t *set) { *set=0; return 0; }
int sigfillset(sigset_t *set) { *set=KILONS_SIGNAL_SET; return 0; }
int sigaddset(sigset_t *set,int sig) {
    unsigned long flag=bit(sig);
    if(!flag)return -1;
    *set|=flag;
    return 0;
}
int sigdelset(sigset_t *set,int sig) {
    unsigned long flag=bit(sig);
    if(!flag)return -1;
    *set&=~flag;
    return 0;
}
int sigismember(const sigset_t *set,int sig) {
    unsigned long flag=bit(sig);
    return flag?!!(*set&flag):-1;
}
int sigprocmask(int how,const sigset_t *set,sigset_t *old) {
    return result(k_syscall3(14,how,(long)set,(long)old));
}
int sigpending(sigset_t *set) { return result(k_syscall3(127,(long)set,0,0)); }
int sigsuspend(const sigset_t *set) { return result(k_syscall3(130,(long)set,0,0)); }
int pause(void) { return result(k_syscall3(34,0,0,0)); }
int raise(int sig) { return kill(getpid(),sig); }

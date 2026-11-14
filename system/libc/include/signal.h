#ifndef KILONS_SIGNAL_H
#define KILONS_SIGNAL_H
#include "sys/types.h"
#include "bits/signal_numbers.h"
typedef int sig_atomic_t;
typedef unsigned long sigset_t;
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
    unsigned reserved;
};
int sigaction(int, const struct sigaction *, struct sigaction *);
void (*signal(int, void (*)(int)))(int);
int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *,int);
int sigdelset(sigset_t *,int);
int sigismember(const sigset_t *,int);
int sigpending(sigset_t *);
int sigprocmask(int,const sigset_t *,sigset_t *);
int sigsuspend(const sigset_t *);
int raise(int);
#define CLD_STOPPED 5
#define CLD_CONTINUED 6
int kill(pid_t,int);
#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
typedef struct {
    int si_signo,si_code,si_errno;
    pid_t si_pid;
    uid_t si_uid;
    int si_status;
} siginfo_t;
#endif

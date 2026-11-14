#include "../libc/include/errno.h"
#include "../libc/include/signal.h"
#include "../libc/include/sys/wait.h"
#include "../libc/unistd.h"

static volatile sig_atomic_t usr1_count;
static volatile sig_atomic_t usr2_count;

static void on_usr1(int sig) {
    if(sig==SIGUSR1)usr1_count++;
}

static void on_usr2(int sig) {
    if(sig==SIGUSR2)usr2_count++;
}

static int fail(int code) {
    static const char prefix[]="signalprobe: fail ";
    char digit=(char)('0'+code);
    (void)write(2,prefix,sizeof prefix-1);
    (void)write(2,&digit,1);
    (void)write(2,"\n",1);
    _exit(code);
}

int main(void) {
    struct sigaction action={0};
    sigemptyset(&action.sa_mask);
    action.sa_handler=on_usr1;
    action.sa_flags=SA_RESTART;
    if(sizeof action!=24 || sigaction(SIGUSR1,&action,0)<0)fail(1);

    sigset_t blocked;
    sigemptyset(&blocked);
    if(sigaddset(&blocked,SIGUSR1)<0 || sigprocmask(SIG_BLOCK,&blocked,0)<0)fail(2);
    if(raise(SIGUSR1)<0)fail(3);

    sigset_t pending;
    if(sigpending(&pending)<0 || sigismember(&pending,SIGUSR1)!=1)fail(4);
    if(sigprocmask(SIG_UNBLOCK,&blocked,0)<0)fail(5);
    if(getpid()<1 || usr1_count!=1)fail(6);

    action.sa_handler=on_usr2;
    action.sa_flags=0;
    if(sigaction(SIGUSR2,&action,0)<0)fail(7);
    sigset_t held;
    sigemptyset(&held);
    if(sigaddset(&held,SIGUSR2)<0 || sigprocmask(SIG_BLOCK,&held,0)<0)fail(8);
    int gate[2];
    if(pipe(gate)<0)fail(9);
    int child=fork();
    if(child<0)fail(10);
    if(child==0) {
        close(gate[0]);
        if(kill(getppid(),SIGUSR2)<0)_exit(11);
        close(gate[1]);
        _exit(0);
    }
    close(gate[1]);
    sigset_t unblocked;
    sigemptyset(&unblocked);
    if(sigsuspend(&unblocked)!=-1 || errno!=EINTR)fail(12);
    int status;
    if(waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=0)fail(13);
    close(gate[0]);
    if(usr2_count!=1)fail(14);
    return 0;
}

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

static volatile sig_atomic_t seen;
static void caught(int sig) { seen=sig; }
static int install(int flags) {
    struct sigaction a={0};
    a.sa_handler=caught;
    sigemptyset(&a.sa_mask);
    a.sa_flags=flags;
    return sigaction(SIGUSR1,&a,0);
}
static int reap(pid_t child,int expected) {
    int status;
    pid_t result;
    do { result=waitpid(child,&status,0); } while(result<0 && errno==EINTR);
    return result==child && WIFEXITED(status) && WEXITSTATUS(status)==expected;
}
static int masks(void) {
    sigset_t one,old,pending;
    sigemptyset(&one);
    sigaddset(&one,SIGUSR1);
    if(install(0)<0 || sigprocmask(SIG_BLOCK,&one,&old)<0)return 0;
    seen=0;
    if(raise(SIGUSR1)<0 || raise(SIGUSR1)<0 || seen ||
       sigpending(&pending)<0 || sigismember(&pending,SIGUSR1)!=1)return 0;
    if(sigsuspend(&old)!=-1 || errno!=EINTR || seen!=SIGUSR1)return 0;
    if(sigprocmask(SIG_SETMASK,0,&pending)<0 || !sigismember(&pending,SIGUSR1))return 0;
    return sigprocmask(SIG_SETMASK,&old,0)==0;
}
enum operation { PIPE_READ, PIPE_WRITE, CHILD_WAIT, CHILD_WAITID, FILE_LOCK, TIMER };
static int blocking(enum operation op,int restart) {
    int gate[2],data[2],fd=-1;
    if(install(restart?SA_RESTART:0)<0 || pipe(gate)<0 || pipe(data)<0)return 0;
    struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};
    if(op==FILE_LOCK) {
        fd=open("/tmp/intrcheck.lock",O_CREAT|O_EXCL|O_RDWR,0600);
        if(fd<0 || unlink("/tmp/intrcheck.lock")<0)return 0;
    }
    if(op==PIPE_WRITE) {
        if(fcntl(data[1],F_SETFL,O_NONBLOCK)<0)return 0;
        char bytes[512]={0};
        while(write(data[1],bytes,sizeof bytes)>0){}
        while(write(data[1],"x",1)>0){}
        if(errno!=EAGAIN || fcntl(data[1],F_SETFL,0)<0)return 0;
    }
    seen=0;
    pid_t child=fork();
    if(child<0)return 0;
    if(!child) {
        char drain[4096];
        if(op==FILE_LOCK && fcntl(fd,F_SETLK,&lock)<0)_exit(1);
        if(write(gate[1],"a",1)!=1)_exit(2);
        /* Keep the resource unavailable until the parent has entered its call. */
        struct timespec delay={0,100000000};
        if(nanosleep(&delay,0)<0 || kill(getppid(),SIGUSR1)<0 || nanosleep(&delay,0)<0)_exit(3);
        if(op==PIPE_READ && write(data[1],"r",1)!=1)_exit(4);
        if(op==PIPE_WRITE && read(data[0],drain,sizeof drain)<=0)_exit(5);
        _exit(0);
    }
    close(gate[1]);
    char byte;
    int ok=read(gate[0],&byte,1)==1, status=0;
    siginfo_t info={0};
    struct timespec duration={3,0},remaining={0,0};
    int result;
    if(op==PIPE_READ)result=(int)read(data[0],&byte,1);
    else if(op==PIPE_WRITE)result=(int)write(data[1],"w",1);
    else if(op==CHILD_WAIT)result=waitpid(child,&status,0);
    else if(op==CHILD_WAITID)result=waitid(P_PID,(id_t)child,&info,WEXITED);
    else if(op==FILE_LOCK)result=fcntl(fd,F_SETLKW,&lock);
    else result=nanosleep(&duration,&remaining);
    int interrupted=result==-1 && errno==EINTR;
    if(!restart || op==TIMER)ok=ok && interrupted;
    else if(op==PIPE_READ)ok=ok && result==1 && byte=='r';
    else if(op==PIPE_WRITE)ok=ok && result==1;
    else if(op==CHILD_WAIT)ok=ok && result==child && WIFEXITED(status) && WEXITSTATUS(status)==0;
    else if(op==CHILD_WAITID)ok=ok && result==0 && info.si_pid==child && info.si_code==CLD_EXITED && info.si_status==0;
    else ok=ok && result==0;
    if(op==TIMER)ok=ok && remaining.tv_sec>=0 && remaining.tv_sec<3;
    if(!((op==CHILD_WAIT && result==child) || (op==CHILD_WAITID && result==0)))
        ok=reap(child,0) && ok;
    if(op==FILE_LOCK && interrupted)ok=ok && fcntl(fd,F_SETLK,&lock)==0;
    if(fd>=0)close(fd);
    close(gate[0]);close(data[0]);close(data[1]);
    return ok && seen==SIGUSR1;
}
static int broken_pipe(void) {
    int p[2],status;
    if(pipe(p)<0 || close(p[0])<0)return 0;
    pid_t child=fork();
    if(!child) {
        signal(SIGPIPE,SIG_DFL);
        write(p[1],"x",1);
        _exit(1);
    }
    if(child<0 || waitpid(child,&status,0)!=child || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGPIPE)return 0;
    if(signal(SIGPIPE,SIG_IGN)==SIG_ERR)return 0;
    int ok=write(p[1],"x",1)==-1 && errno==EPIPE;
    return close(p[1])==0 && ok;
}
static int long_write(int interrupt) {
    char buffer[4500]={0};
    int data[2];
    if(install(SA_RESTART)<0 || pipe(data)<0)return 0;
    if(interrupt) {
        /* Leave at most one page free on either the guest or the host. */
        if(fcntl(data[1],F_SETFL,O_NONBLOCK)<0)return 0;
        while(write(data[1],buffer,512)>0){}
        if(errno!=EAGAIN || read(data[0],buffer,4096)<=0 || fcntl(data[1],F_SETFL,0)<0)return 0;
    }
    seen=0;
    pid_t child=fork();
    if(!child) {
        close(data[1]);
        struct timespec delay={0,100000000};
        if(nanosleep(&delay,0)<0)_exit(1);
        if(interrupt && kill(getppid(),SIGUSR1)<0)_exit(2);
        if(interrupt && nanosleep(&delay,0)<0)_exit(3);
        long total=0,n;
        while((n=read(data[0],buffer,512))>0)total+=n;
        _exit(n<0 || (!interrupt && total!=(long)sizeof buffer));
    }
    close(data[0]);
    ssize_t result=write(data[1],buffer,sizeof buffer);
    close(data[1]);
    return child>0 && reap(child,0) &&
           (interrupt?(seen==SIGUSR1 && result>0 && result<(long)sizeof buffer):result==(long)sizeof buffer);
}
static int inherited(int after_exec) {
    struct sigaction action;
    sigset_t mask,pending;
    if(sigaction(SIGUSR1,0,&action)<0 || action.sa_handler!=(after_exec?SIG_DFL:caught))return 0;
    if(sigaction(SIGPIPE,0,&action)<0 || action.sa_handler!=SIG_IGN)return 0;
    if(sigprocmask(SIG_SETMASK,0,&mask)<0 || sigismember(&mask,SIGUSR2)!=1)return 0;
    if(sigpending(&pending)<0 || sigismember(&pending,SIGUSR2)!=after_exec)return 0;
    return 1;
}
static int inheritance(const char *self) {
    sigset_t set,old;
    sigemptyset(&set);
    sigaddset(&set,SIGUSR2);
    if(install(0)<0 || signal(SIGPIPE,SIG_IGN)==SIG_ERR ||
       sigprocmask(SIG_BLOCK,&set,&old)<0 || raise(SIGUSR2)<0)return 0;
    pid_t child=fork();
    if(!child) {
        if(!inherited(0) || raise(SIGUSR2)<0)_exit(3);
        execlp(self,self,"exec",(char *)0);
        _exit(4);
    }
    int ok=child>0 && reap(child,0);
    if(signal(SIGUSR2,SIG_IGN)==SIG_ERR || sigprocmask(SIG_SETMASK,&old,0)<0)return 0;
    return ok;
}
int main(int argc,char **argv) {
    if(argc==2)return inherited(1)?0:1;
    if(!masks())return 1;
    for(int op=PIPE_READ;op<=TIMER;op++)
        for(int restart=0;restart<=1;restart++)
            if(!blocking((enum operation)op,restart))return 2+op*2+restart;
    if(!broken_pipe())return 14;
    if(!inheritance(argv[0]))return 15;
    if(!long_write(0) || !long_write(1))return 16;
    static const char pass[]="intrcheck: PASS\n";
    write(1,pass,sizeof pass-1);
    return 0;
}

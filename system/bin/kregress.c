#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sched.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
/* Run from an isolated process: waitpid(-1) only observes this test's children. */
static int start(char *name){
    int pid=fork();
    if(pid==0){
        char *args[]={name,0};
        execvp(name,args);_exit(127);
    }
    return pid;
}
static int collect(int pid,int signal){
    int status;
    if(pid<1 || waitpid(pid,&status,0)!=pid)return 0;
    return signal?(WIFSIGNALED(status) && WTERMSIG(status)==signal):
                  (WIFEXITED(status) && WEXITSTATUS(status)==0);
}
/* Exercise the installed shell, file redirection, and repeated pipelines.
 * A successful mkdir gives this invocation exclusive ownership of the scratch path. */
static int pipeline_check(void){
    const char *directory="/tmp/test19.run";
    const char *output="/tmp/test19.run/output";
    if(mkdir(directory,0700)<0)return 0;
    int ok=1;
    for(unsigned round=0;round<2 && ok;round++){
        int pid=fork();
        if(pid==0){
            char *args[]={"sh","-c","echo ok | cat > /tmp/test19.run/output",0};
            execvp("sh",args);
            _exit(127);
        }
        ok=collect(pid,0);
        if(ok){
            int fd=open(output,O_RDONLY);
            if(fd<0){ok=0;break;}
            char data[4];
            ok=read(fd,data,sizeof data)==3 && data[0]=='o' && data[1]=='k' && data[2]=='\n';
            if(close(fd)<0)ok=0;
        }
    }
    if(unlink(output)<0)ok=0;
    if(rmdir(directory)<0)ok=0;
    return ok;
}
static int same(const char *a,const char *b){
    while(*a && *a==*b){a++;b++;}
    return *a==*b;
}
/* Enter the installed ELF a second time so crt0, argv and environ are
 * exercised together across a real address-space replacement. */
static const char long_arg[]=
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static int exec_args_check(void) {
    char *args[]={"argcheck","1","2","3","4","5","6","7","8","9","10",(char *)long_arg,0};
    char *env[]={"CHECK=passed",0};
    int pid=fork();
    if(pid==0) {
        execve("/bin/kregress",args,env);
        _exit(127);
    }
    return collect(pid,0);
}
static int symlink_check(void) {
    const char *dir="/tmp/test19.links";
    if(mkdir(dir,0700)<0)return 0;
    int fd=open(dir,O_RDONLY);
    struct stat link,target;
    char bytes[5]={0,0,0,0,'!'};
    int ok=fd>=0 && symlinkat("/bin/echo",fd,"echo")==0;
    if(ok) {
        ok=fstatat(fd,"echo",&link,AT_SYMLINK_NOFOLLOW)==0 && S_ISLNK(link.st_mode) &&
           link.st_size==9 && fstatat(fd,"echo",&target,0)==0 && S_ISREG(target.st_mode) &&
           readlinkat(fd,"echo",bytes,4)==4 && bytes[0]=='/' && bytes[1]=='b' &&
           bytes[2]=='i' && bytes[3]=='n' && bytes[4]=='!';
        if(ok) {
            int child=fork();
            if(child==0) {
                execl("/tmp/test19.links/echo","echo","symlink exec",(char *)0);
                _exit(127);
            }
            ok=collect(child,0);
        }
        if(unlink("/tmp/test19.links/echo")<0)ok=0;
    }
    if(fd>=0)close(fd);
    return rmdir(dir)==0 && ok;
}
static int record_lock_check(void) {
    const char *path="/tmp/test19.lock";
    int fd=open(path,O_RDWR|O_CREAT|O_EXCL,0600);
    if(fd<0)return 0;
    int ready[2];
    struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};
    if(unlink(path)<0 || fcntl(fd,F_SETLK,&lock)<0 || pipe(ready)<0) {
        close(fd);
        return 0;
    }
    int child=fork();
    if(child==0) {
        close(ready[0]);
        struct flock query=lock;
        if(fcntl(fd,F_GETLK,&query)<0 || query.l_pid!=getppid() ||
           query.l_type!=F_WRLCK)_exit(1);
        if(fcntl(fd,F_SETLK,&lock)==0 || errno!=EAGAIN)_exit(2);
        if(write(ready[1],"R",1)!=1)_exit(3);
        _exit(fcntl(fd,F_SETLKW,&lock)<0?4:0);
    }
    close(ready[1]);
    char mark;
    int ok=child>0 && read(ready[0],&mark,1)==1 && mark=='R';
    close(ready[0]);
    close(fd);
    return collect(child,0) && ok;
}
static int fd_exec_check(void) {
    int fd=open("/bin/echo",O_RDONLY|O_CLOEXEC);
    if(fd<0)return 0;
    char *args[]={"echo","descriptor exec",0};
    char *env[]={0};
    int child=fork();
    if(child==0) {
        fexecve(fd,args,env);
        _exit(127);
    }
    int ok=collect(child,0);
    return close(fd)==0 && ok;
}
static int vfork_check(void) {
    int child=vfork();
    if(child==0) {
        execl("/bin/echo","echo","vfork/exec",(char *)0);
        _exit(127);
    }
    if(!collect(child,0))return 0;
    child=vfork();
    if(child==0)_exit(34);
    int exit_status;
    if(child<0 || waitpid(child,&exit_status,0)!=child ||
       !WIFEXITED(exit_status) || WEXITSTATUS(exit_status)!=34)return 0;
    volatile unsigned marker=0x97531U;
    child=vfork();
    if(child==0) {
        char *args[]={"missing",0};
        char *env[]={0};
        if(execve("/kilons-vfork-exec-missing",args,env)!=-1 || errno!=ENOENT)
            _exit(35);
        _exit(36);
    }
    if(child<0 || marker!=0x97531U || waitpid(child,&exit_status,0)!=child ||
       !WIFEXITED(exit_status) || WEXITSTATUS(exit_status)!=36)return 0;
    return 1;
}

static int wait_boundary_check(void) {
    int gate[2];
    if(pipe(gate)<0)return 0;
    int child=fork();
    if(child==0) {
        close(gate[1]);
        char byte=0;
        if(read(gate[0],&byte,1)!=1 || byte!='W')_exit(41);
        close(gate[0]);
        _exit(29);
    }
    close(gate[0]);
    if(child<0) {close(gate[1]);return 0;}
    int status=0;
    if(waitpid(child,&status,WNOHANG)!=0 ||
       waitpid(child+1000,&status,WNOHANG)!=-1 || errno!=ECHILD ||
       write(gate[1],"W",1)!=1) {
        close(gate[1]);
        return 0;
    }
    close(gate[1]);
    if(waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=29)
        return 0;
    child=fork();
    if(child==0)_exit(30);
    if(child<0 || waitpid(child,0,0)!=child)return 0;
    return waitpid(-1,&status,WNOHANG)==-1 && errno==ECHILD;
}

static int exec_failure_check(void) {
    static const char invalid_image[]="/tmp/kregress-invalid-elf";
    int channel[2];
    if(pipe(channel)<0)return 0;
    int child=fork();
    if(child==0) {
        close(channel[0]);
        volatile unsigned marker=0x13579U;
        volatile uintptr_t invalid_argv=0x40000000U;
        char *args[]={"echo",0};
        char *env[]={0};
        unsigned char malformed[64]={0};
        if(execve("/bin/echo",(char *const *)(uintptr_t)invalid_argv,env)!=-1 ||
           errno!=EFAULT)_exit(51);
        if(execve("/kilons-exec-missing",args,env)!=-1 || errno!=ENOENT)_exit(52);
        int image=open(invalid_image,O_CREAT|O_EXCL|O_RDWR,0700);
        if(image<0 || write(image,malformed,sizeof malformed)!=(ssize_t)sizeof malformed ||
           close(image)<0)_exit(53);
        /* This gets past path lookup and replacement-VM creation, then fails
         * ELF validation. Repetition catches replacement-space leaks. */
        for(unsigned i=0;i<16;i++)
            if(execve(invalid_image,args,env)!=-1 || errno!=ENOEXEC)_exit(54);
        if(unlink(invalid_image)<0)_exit(55);
        if(marker!=0x13579U || write(channel[1],"E",1)!=1)_exit(53);
        close(channel[1]);
        _exit(0);
    }
    close(channel[1]);
    if(child<0) {close(channel[0]);return 0;}
    int ok=collect(child,0);
    char byte=0;
    ok=ok && read(channel[0],&byte,1)==1 && byte=='E' && read(channel[0],&byte,1)==0;
    if(close(channel[0])<0)ok=0;
    return ok;
}

static volatile sig_atomic_t inherited_signal_count;
static void inherited_signal_handler(int sig) {
    if(sig==SIGUSR1)inherited_signal_count++;
}
static int signal_fork_cleanup_check(void) {
    struct sigaction action={0};
    action.sa_handler=inherited_signal_handler;
    if(sigaction(SIGUSR1,&action,0)<0)return 0;
    int ok=1;
    for(unsigned i=0;i<16 && ok;i++) {
        inherited_signal_count=0;
        int child=fork();
        if(child==0) {
            if(inherited_signal_count || raise(SIGUSR1)<0 || inherited_signal_count!=1)
                _exit(61);
            _exit(0);
        }
        ok=child>0 && collect(child,0) && inherited_signal_count==0;
    }
    action.sa_handler=SIG_DFL;
    if(sigaction(SIGUSR1,&action,0)<0)ok=0;
    return ok;
}

static int orphan_reparent_check(void) {
    int channel[2];
    if(pipe(channel)<0)return 0;
    int parent=fork();
    if(parent==0) {
        close(channel[0]);
        int orphan=fork();
        if(orphan==0) {
            char result='X';
            for(unsigned i=0;i<2048;i++) {
                if(getppid()==1) {result='O';break;}
                (void)sched_yield();
            }
            (void)write(channel[1],&result,1);
            close(channel[1]);
            _exit(result=='O'?0:71);
        }
        close(channel[1]);
        _exit(orphan>0?0:72);
    }
    close(channel[1]);
    if(parent<0) {close(channel[0]);return 0;}
    int ok=collect(parent,0);
    char result=0;
    ok=ok && read(channel[0],&result,1)==1 && result=='O' && read(channel[0],&result,1)==0;
    if(close(channel[0])<0)ok=0;
    return ok;
}

static int fork_memory_stress(unsigned rounds) {
    const size_t page=4096;
    for(unsigned round=0;round<rounds;round++) {
        volatile unsigned char *memory=mmap(0,8*page,PROT_READ|PROT_WRITE,
                                            MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(memory==MAP_FAILED)return 0;
        for(unsigned i=0;i<8;i++)memory[i*page]=(unsigned char)(round+i+1);
        int child=fork();
        if(child==0) {
            for(unsigned i=0;i<8;i++)if(memory[i*page]!=(unsigned char)(round+i+1))_exit(81);
            memory[0]^=0xff;
            if(mprotect((void *)memory,page,PROT_READ)<0 ||
               mprotect((void *)memory,page,PROT_READ|PROT_WRITE)<0 ||
               munmap((void *)(memory+3*page),page)<0)_exit(82);
            void *hole=mmap(0,page,PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(hole!=(void *)(memory+3*page))_exit(83);
            *(volatile unsigned char *)hole=0xa5;
            if(munmap((void *)memory,8*page)<0)_exit(84);
            _exit(0);
        }
        int status=0;
        int ok=child>0 && waitpid(child,&status,0)==child &&
               WIFEXITED(status) && WEXITSTATUS(status)==0;
        for(unsigned i=0;i<8 && ok;i++)
            if(memory[i*page]!=(unsigned char)(round+i+1))ok=0;
        if(munmap((void *)memory,8*page)<0)ok=0;
        if(!ok)return 0;
    }
    return 1;
}
int main(int argc,char **argv){
    if(argc==12 && same(argv[0],"argcheck")) {
        const char *check=getenv("CHECK");
        return !(same(argv[11],long_arg) && same(argv[10],"10") && check && same(check,"passed"));
    }
    const char *mode=argv[0];
    for(const char *p=argv[0];*p;p++)if(*p=='/')mode=p+1;
    if(argc!=1)return 2;
    int stress_only=same(mode,"stress");
    int integration_only=same(mode,"test19");
    if(stress_only) {
        if(!wait_boundary_check() || !exec_failure_check() ||
           !signal_fork_cleanup_check() || !orphan_reparent_check() ||
           !fork_memory_stress(256)) {
            static const char message[]="kregress: process/memory stress FAIL\n";
            write(2,message,sizeof message-1);
            return 1;
        }
        static const char message[]="kregress: process/memory stress PASS\n";
        write(1,message,sizeof message-1);
        return 0;
    }
    char *images[]={"forkprobe","execprobe","mapprobe","signalprobe","posix_example","fault","ud","divide"};
    int signals[]={0,0,0,0,0,SIGSEGV,SIGILL,SIGFPE};
    for(unsigned i=0;!stress_only && i<(integration_only?5:sizeof images/sizeof images[0]);i++){
        if(!collect(start(images[i]),signals[i])){
            size_t n=0;while(images[i][n])n++;
            write(2,"failed: ",8);write(2,images[i],n);write(2,"\n",1);return 1;
        }
    }
    for(unsigned i=0;!integration_only && i<20;i++){
        int a=start("probe"),b=start("probe");
        int ok=collect(a,0);
        if(!collect(b,0) || !ok)return 1;
    }
    if(!stress_only && !collect(start("intrcheck"),0)) {
        static const char message[]="kregress: FAIL intrcheck\n";
        write(2,message,sizeof message-1);
        return 1;
    }
    if(!stress_only && !collect(start("posixcheck"),0)) {
        static const char message[]="posixcheck failed\n";
        write(2,message,sizeof message-1);
        return 1;
    }
    if(!stress_only && !symlink_check()) {
        static const char message[]="symlink traversal failed\n";
        write(2,message,sizeof message-1);
        return 1;
    }
    if(!stress_only && !exec_args_check()) {
        static const char message[]="exec argv/envp failed\n";
        write(2,message,sizeof message-1);
        return 1;
    }
    if(!stress_only && !record_lock_check()) {
        static const char message[]="fcntl record locks failed\n";
        write(2,message,sizeof message-1);
        return 1;
    }
    if(!stress_only && (!vfork_check() || !fd_exec_check()))return 1;
    if(!wait_boundary_check() || !exec_failure_check() ||
       !signal_fork_cleanup_check() || !orphan_reparent_check() ||
       !fork_memory_stress(64)) {
        static const char message[]="kregress: process/memory lifecycle FAIL\n";
        write(2,message,sizeof message-1);
        return 1;
    }
    if(!stress_only && !pipeline_check()){
        write(2,"pipeline/redirection failed\n",28);return 1;
    }
    int status;
    if(waitpid(-1,&status,WNOHANG)!=-1 || errno!=ECHILD)return 1;
    static const char lifecycle[]="kregress: process/memory lifecycle PASS\n";
    write(1,lifecycle,sizeof lifecycle-1);
    write(1,"kregress: PASS\n",15);return 0;
}

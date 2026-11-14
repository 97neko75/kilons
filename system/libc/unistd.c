#include "syscall_internal.h"
#include <stdarg.h>
#include "unistd.h"
long k_syscall3(long nr,long a,long b,long c) {
    register long ax __asm__("rax")=nr;
    register long di __asm__("rdi")=a;
    register long si __asm__("rsi")=b;
    register long dx __asm__("rdx")=c;
    __asm__ volatile("int $0x80":"+a"(ax):"D"(di),"S"(si),"d"(dx):"rcx","r8","r9","r10","r11","memory","cc");
    return ax;
}
/* int 0x80 uses the x86-64 six-argument register convention. In
 * particular, mmap flags must travel in r10, not be silently replaced by
 * kernel defaults. Caller-saved registers are explicit compiler operands. */
long k_syscall6(long nr,long a,long b,long c,long d,long e,long f) {
    register long ax __asm__("rax")=nr;
    register long di __asm__("rdi")=a;
    register long si __asm__("rsi")=b;
    register long dx __asm__("rdx")=c;
    register long ten __asm__("r10")=d;
    register long eight __asm__("r8")=e;
    register long nine __asm__("r9")=f;
    __asm__ volatile("int $0x80" : "+a"(ax)
                     : "D"(di),"S"(si),"d"(dx),"r"(ten),"r"(eight),"r"(nine)
                     : "rcx","r11","memory","cc");
    return ax;
}
int errno;
static long posix_result(long value) {
    if(value<0 && value>=-4095) {errno=(int)-value;return -1;}
    return value;
}
long write(int fd,const void *buf,size_t n) {return posix_result(k_syscall3(1,fd,(long)buf,(long)n));}
long read(int fd,void *buf,size_t n) {return posix_result(k_syscall3(0,fd,(long)buf,(long)n));}
int open(const char *path,int flags,...) {
    int mode=0;
    if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);}
    return (int)posix_result(k_syscall3(2,(long)path,flags,mode));}
int openat(int dirfd,const char *path,int flags,...) {
    unsigned mode=0;
    if(flags&O_CREAT) {
        va_list ap;
        va_start(ap,flags);
        mode=va_arg(ap,unsigned);
        va_end(ap);
    }
    return (int)posix_result(k_syscall6(257,dirfd,(long)path,flags,mode,0,0));
}
int creat(const char *path,mode_t mode) {
    return open(path,O_CREAT|O_WRONLY|O_TRUNC,mode);
}
int stat(const char *path,struct stat *out) {return (int)posix_result(k_syscall3(4,(long)path,(long)out,sizeof *out));}
int fexecve(int fd,char *const argv[],char *const envp[]) {
    return (int)posix_result(k_syscall3(322,fd,(long)argv,(long)envp));
}
int fstatat(int dirfd,const char *path,struct stat *out,int flags) {
    return (int)posix_result(k_syscall6(262,dirfd,(long)path,(long)out,flags,sizeof *out,0));
}
int lstat(const char *path,struct stat *out) {
    return fstatat(AT_FDCWD,path,out,AT_SYMLINK_NOFOLLOW);
}
int fstat(int fd,struct stat *out) {return (int)posix_result(k_syscall3(5,fd,(long)out,sizeof *out));}
int fcntl(int fd,int command,...){
    long argument=0;
    if(command==F_DUPFD || command==F_DUPFD_CLOEXEC || command==F_DUPFD_CLOFORK || command==F_SETOWN || command==F_SETFD || command==F_SETFL){
        va_list ap;va_start(ap,command);argument=va_arg(ap,int);va_end(ap);
    }
    if(command==F_GETLK || command==F_SETLK || command==F_SETLKW ||
       command==F_OFD_GETLK || command==F_OFD_SETLK || command==F_OFD_SETLKW){
        va_list ap;va_start(ap,command);argument=(long)va_arg(ap,struct flock *);va_end(ap);
    }
    return (int)posix_result(k_syscall3(72,fd,command,argument));
}
int waitid(idtype_t kind,id_t id,siginfo_t *info,int options) {
    if(!info){errno=EFAULT;return -1;}
    if(!(options&(WEXITED|WSTOPPED|WCONTINUED)) || (options&~(WEXITED|WSTOPPED|WCONTINUED|WNOHANG|WNOWAIT)) ||
       (kind!=P_ALL && kind!=P_PID && kind!=P_PGID) ||
       (kind!=P_ALL && id>2147483647U) || (kind==P_PID && !id)) {
        errno=EINVAL;
        return -1;
    }
    int selector=kind==P_ALL?-1:kind==P_PID?(int)id:0;
    unsigned event[2]={0};
    long child=posix_result(k_syscall6(247,selector,(long)event,
                         (options&WNOHANG)|((options&WNOWAIT)?2:0)|(options&WEXITED)|
                         ((options&WSTOPPED)?8:0)|(options&WCONTINUED),kind==P_PGID?id:0,0,0));
    if(child<0)return -1;
    *info=(siginfo_t){0};
    if(child) {
        info->si_pid=(pid_t)child;
        info->si_signo=SIGCHLD;
        int status=(int)event[0];
        info->si_uid=event[1];
        info->si_code=WIFEXITED(status)?CLD_EXITED:CLD_KILLED;
        info->si_status=WIFEXITED(status)?WEXITSTATUS(status):WTERMSIG(status);
        if(WIFSTOPPED(status)){info->si_code=CLD_STOPPED;info->si_status=WSTOPSIG(status);}
        if(WIFCONTINUED(status)){info->si_code=CLD_CONTINUED;info->si_status=SIGCONT;}
    }
    return 0;
}
int lockf(int fd,int action,off_t length) {
    struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_CUR,.l_len=length};
    switch(action) {
    case F_ULOCK:
        lock.l_type=F_UNLCK;
        return fcntl(fd,F_SETLK,&lock);
    case F_LOCK:
        return fcntl(fd,F_SETLKW,&lock);
    case F_TLOCK:
        return fcntl(fd,F_SETLK,&lock);
    case F_TEST:
        if(fcntl(fd,F_GETLK,&lock)<0)return -1;
        if(lock.l_type==F_UNLCK)return 0;
        errno=EACCES;
        return -1;
    default:
        errno=EINVAL;
        return -1;
    }
}
int fdatasync(int fd){return fsync(fd);}
int fsync(int fd){return (int)posix_result(k_syscall3(74,fd,0,0));}
int isatty(int fd){
    struct stat st;
    if(fstat(fd,&st)<0)return 0;
    if(S_ISCHR(st.st_mode))return 1;
    errno=25;return 0;
}
int close(int fd) {return (int)posix_result(k_syscall3(3,fd,0,0));}
int pipe(int fds[2]) {return (int)posix_result(k_syscall3(22,(long)fds,0,0));}
int dup(int fd) {return (int)posix_result(k_syscall3(32,fd,0,0));}
int dup2(int oldfd,int newfd) {return (int)posix_result(k_syscall3(33,oldfd,newfd,0));}
long lseek(int fd,long offset,int whence) {return posix_result(k_syscall3(8,fd,offset,whence));}
ssize_t pread(int fd,void *buf,size_t count,off_t offset) {
    return posix_result(k_syscall6(17,fd,(long)buf,count,offset,0,0));
}
ssize_t pwrite(int fd,const void *buf,size_t count,off_t offset) {
    return posix_result(k_syscall6(18,fd,(long)buf,count,offset,0,0));
}
int fchownat(int fd,const char *path,uid_t uid,gid_t gid,int flags) {
    return (int)posix_result(k_syscall6(260,fd,(long)path,uid,gid,flags,0));
}
int chown(const char *path,uid_t uid,gid_t gid){return fchownat(AT_FDCWD,path,uid,gid,0);}
int lchown(const char *path,uid_t uid,gid_t gid){return fchownat(AT_FDCWD,path,uid,gid,AT_SYMLINK_NOFOLLOW);}
int fchown(int fd,uid_t uid,gid_t gid){return (int)posix_result(k_syscall6(260,fd,0,uid,gid,0,1));}
int utimensat(int fd,const char *path,const struct timespec times[2],int flags) {
    return (int)posix_result(k_syscall6(280,fd,(long)path,(long)times,flags,0,0));
}
int futimens(int fd,const struct timespec times[2]) {
    return (int)posix_result(k_syscall6(280,fd,0,(long)times,0,1,0));
}
int linkat(int srcfd,const char *old,int dstfd,const char *name,int flags) {
    return (int)posix_result(k_syscall6(265,srcfd,(long)old,dstfd,(long)name,flags,0));
}
int link(const char *old,const char *name) {
    return linkat(AT_FDCWD,old,AT_FDCWD,name,0);
}
int renameat(int srcfd,const char *src,int dstfd,const char *dst) {
    return (int)posix_result(k_syscall6(264,srcfd,(long)src,dstfd,(long)dst,0,0));
}
int faccessat(int dirfd,const char *path,int mode,int flags) {
    return (int)posix_result(k_syscall6(439,dirfd,(long)path,mode,flags,0,0));
}
int fchmodat(int dirfd,const char *path,mode_t mode,int flags) {
    return (int)posix_result(k_syscall6(452,dirfd,(long)path,mode,flags,0,0));
}
int fchmod(int fd,mode_t mode) {return (int)posix_result(k_syscall3(91,fd,mode,0));}
int access(const char *path,int mode) {return faccessat(AT_FDCWD,path,mode,0);}
int mkdirat(int fd,const char *path,mode_t mode) {return (int)posix_result(k_syscall3(258,fd,(long)path,mode));}
int unlinkat(int fd,const char *path,int flags) {return (int)posix_result(k_syscall3(263,fd,(long)path,flags));}
int fchdir(int fd) {return (int)posix_result(k_syscall3(81,fd,0,0));}
int pipe2(int fds[2],int flags) {return (int)posix_result(k_syscall3(293,(long)fds,flags,0));}
int dup3(int oldfd,int newfd,int flags) {return (int)posix_result(k_syscall3(292,oldfd,newfd,flags));}
mode_t umask(mode_t mask) {return (mode_t)k_syscall3(95,mask,0,0);}
int setuid(uid_t id){return (int)posix_result(k_syscall3(105,id,0,0));}
int seteuid(uid_t id){return (int)posix_result(k_syscall3(117,id,0,0));}
int setgid(gid_t id){return (int)posix_result(k_syscall3(106,id,0,0));}
int setegid(gid_t id){return (int)posix_result(k_syscall3(119,id,0,0));}
uid_t getuid(void) {return (uid_t)k_syscall3(102,0,0,0);}
uid_t geteuid(void) {return (uid_t)k_syscall3(107,0,0,0);}
gid_t getgid(void) {return (gid_t)k_syscall3(104,0,0,0);}
gid_t getegid(void) {return (gid_t)k_syscall3(108,0,0,0);}
pid_t getpgid(pid_t pid) {return (pid_t)posix_result(k_syscall3(121,pid,0,0));}
pid_t getpgrp(void) {return getpgid(0);}
int setpgid(pid_t pid,pid_t group) {return (int)posix_result(k_syscall3(109,pid,group,0));}
pid_t getsid(pid_t pid) {return (pid_t)posix_result(k_syscall3(124,pid,0,0));}
pid_t setsid(void) {return (pid_t)posix_result(k_syscall3(112,0,0,0));}
int wait(int *status) {return waitpid(-1,status,0);}

int mkdir(const char *path,unsigned mode) {return (int)posix_result(k_syscall3(83,(long)path,mode,0));}
int rmdir(const char *path) {return (int)posix_result(k_syscall3(84,(long)path,0,0));}
int symlinkat(const char *target,int fd,const char *name) {
    return (int)posix_result(k_syscall3(266,(long)target,fd,(long)name));
}
ssize_t readlinkat(int fd,const char *path,char *buffer,size_t size) {
    return posix_result(k_syscall6(267,fd,(long)path,(long)buffer,size,0,0));
}
int symlink(const char *target,const char *path) {
    return (int)posix_result(k_syscall3(88,(long)target,(long)path,0));
}
ssize_t readlink(const char *path,char *buffer,size_t size) {
    return posix_result(k_syscall3(89,(long)path,(long)buffer,size));
}
int unlink(const char *path) {return (int)posix_result(k_syscall3(87,(long)path,0,0));}
int remove(const char *path) {
    int rc=unlink(path);
    return rc<0 && errno==EISDIR?rmdir(path):rc;
}
int rename(const char *oldpath,const char *newpath) {return (int)posix_result(k_syscall3(82,(long)oldpath,(long)newpath,0));}
int truncate(const char *path,off_t length) {return (int)posix_result(k_syscall3(76,(long)path,(long)length,0));}
int ftruncate(int fd,off_t length) {return (int)posix_result(k_syscall3(77,fd,(long)length,0));}
int chmod(const char *path,unsigned mode){return (int)posix_result(k_syscall3(90,(long)path,mode,0));}
int getpid(void) {return (int)posix_result(k_syscall3(39,0,0,0));}
int getppid(void) {return (int)posix_result(k_syscall3(110,0,0,0));}
int vfork(void) {return (int)posix_result(k_syscall3(58,0,0,0));}
int fork(void) {return (int)posix_result(k_syscall3(57,0,0,0));}
int execve(const char *path,char *const argv[],char *const envp[]) {
    return (int)posix_result(k_syscall3(59,(long)path,(long)argv,(long)envp));
}
int chdir(const char *path) {return (int)posix_result(k_syscall3(80,(long)path,0,0));}
char *getcwd(char *buffer,size_t length) {
    return posix_result(k_syscall3(79,(long)buffer,(long)length,0))<0?0:buffer;
}
void *mmap(void *addr,size_t length,int prot,int flags,int fd,long offset) {
    if(flags!=(MAP_PRIVATE|MAP_ANONYMOUS) || fd!=-1 || offset!=0){errno=22;return (void *)-1;}
    return (void *)posix_result(k_syscall6(9,(long)addr,(long)length,prot,flags,fd,offset));
}
int munmap(void *addr,size_t length) {return (int)posix_result(k_syscall3(11,(long)addr,(long)length,0));}
int mprotect(void *addr,size_t length,int prot) {return (int)posix_result(k_syscall3(10,(long)addr,(long)length,prot));}
void _exit(int status) {(void)k_syscall3(60,status,0,0);for(;;) {}}
int sched_yield(void) {return (int)posix_result(k_syscall3(24,0,0,0));}
int kill(pid_t pid,int sig){return (int)posix_result(k_syscall3(62,pid,sig,0));}
int waitpid(int pid,int *status,int options) {
    if(options&(WSTOPPED|WCONTINUED)) {
        if(options&~(WNOHANG|WSTOPPED|WCONTINUED)){errno=EINVAL;return -1;}
        unsigned event[2]={0};
        long child=posix_result(k_syscall6(247,pid,(long)event,WEXITED|(options&WNOHANG)|
                                 ((options&WSTOPPED)?8:0)|(options&WCONTINUED),0,0,0));
        if(child>0 && status)*status=(int)event[0];
        return (int)child;
    }
    /* The kernel blocks this user task until the child exits. */
    return (int)posix_result(k_syscall3(61,pid,(long)status,options));
}

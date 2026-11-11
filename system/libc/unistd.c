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
int stat(const char *path,struct stat *out) {return (int)posix_result(k_syscall3(4,(long)path,(long)out,0));}
int fstat(int fd,struct stat *out) {return (int)posix_result(k_syscall3(5,fd,(long)out,0));}
int fcntl(int fd,int command,...){
    int argument=0;
    if(command==F_DUPFD || command==F_DUPFD_CLOEXEC || command==F_SETFD || command==F_SETFL){
        va_list ap;va_start(ap,command);argument=va_arg(ap,int);va_end(ap);
    }
    return (int)posix_result(k_syscall3(72,fd,command,argument));
}
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
int mkdir(const char *path,unsigned mode) {return (int)posix_result(k_syscall3(83,(long)path,mode,0));}
int rmdir(const char *path) {return (int)posix_result(k_syscall3(84,(long)path,0,0));}
int unlink(const char *path) {return (int)posix_result(k_syscall3(87,(long)path,0,0));}
int rename(const char *oldpath,const char *newpath) {return (int)posix_result(k_syscall3(82,(long)oldpath,(long)newpath,0));}
int truncate(const char *path,off_t length) {return (int)posix_result(k_syscall3(76,(long)path,(long)length,0));}
int ftruncate(int fd,off_t length) {return (int)posix_result(k_syscall3(77,fd,(long)length,0));}
int chmod(const char *path,unsigned mode){return (int)posix_result(k_syscall3(90,(long)path,mode,0));}
int getpid(void) {return (int)posix_result(k_syscall3(39,0,0,0));}
int getppid(void) {return (int)posix_result(k_syscall3(110,0,0,0));}
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
int waitpid(int pid,int *status,int options) {
    /* The kernel blocks this user task until the child exits. */
    return (int)posix_result(k_syscall3(61,pid,(long)status,options));
}

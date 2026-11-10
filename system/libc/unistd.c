#include "unistd.h"
long k_syscall3(long nr,long a,long b,long c) {
    register long ax __asm__("rax")=nr;
    register long di __asm__("rdi")=a;
    register long si __asm__("rsi")=b;
    register long dx __asm__("rdx")=c;
    __asm__ volatile("int $0x80":"+a"(ax):"D"(di),"S"(si),"d"(dx):"rcx","r8","r9","r10","r11","memory","cc");
    return ax;
}
int errno;
static long posix_result(long value) {
    if(value<0 && value>=-4095) {errno=(int)-value;return -1;}
    return value;
}
long write(int fd,const void *buf,size_t n) {return posix_result(k_syscall3(1,fd,(long)buf,(long)n));}
long read(int fd,void *buf,size_t n) {return posix_result(k_syscall3(0,fd,(long)buf,(long)n));}
int open(const char *path,int flags,int mode) {return (int)posix_result(k_syscall3(2,(long)path,flags,mode));}
int stat(const char *path,struct k_stat *out) {return (int)posix_result(k_syscall3(4,(long)path,(long)out,0));}
int fstat(int fd,struct k_stat *out) {return (int)posix_result(k_syscall3(5,fd,(long)out,0));}
int close(int fd) {return (int)posix_result(k_syscall3(3,fd,0,0));}
int pipe(int fds[2]) {return (int)posix_result(k_syscall3(22,(long)fds,0,0));}
int dup(int fd) {return (int)posix_result(k_syscall3(32,fd,0,0));}
int dup2(int oldfd,int newfd) {return (int)posix_result(k_syscall3(33,oldfd,newfd,0));}
long lseek(int fd,long offset,int whence) {return posix_result(k_syscall3(8,fd,offset,whence));}
int mkdir(const char *path,int mode) {return (int)posix_result(k_syscall3(83,(long)path,mode,0));}
int rmdir(const char *path) {return (int)posix_result(k_syscall3(84,(long)path,0,0));}
int unlink(const char *path) {return (int)posix_result(k_syscall3(87,(long)path,0,0));}
int rename(const char *oldpath,const char *newpath) {return (int)posix_result(k_syscall3(82,(long)oldpath,(long)newpath,0));}
int truncate(const char *path,size_t length) {return (int)posix_result(k_syscall3(76,(long)path,(long)length,0));}
int ftruncate(int fd,size_t length) {return (int)posix_result(k_syscall3(77,fd,(long)length,0));}
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
    return (void *)posix_result(k_syscall3(9,(long)addr,(long)length,prot));
}
int munmap(void *addr,size_t length) {return (int)posix_result(k_syscall3(11,(long)addr,(long)length,0));}
int mprotect(void *addr,size_t length,int prot) {return (int)posix_result(k_syscall3(10,(long)addr,(long)length,prot));}
void _exit(int status) {(void)k_syscall3(60,status,0,0);for(;;) {}}
int sched_yield(void) {return (int)posix_result(k_syscall3(24,0,0,0));}
int spawn(const char *name) {return (int)posix_result(k_syscall3(400,(long)name,0,0));}
int waitpid(int pid,int *status,int options) {
    /* The kernel blocks this user task until the child exits. */
    return (int)posix_result(k_syscall3(61,pid,(long)status,options));
}
int k_readdir(const char *path,struct k_dirent *entry,unsigned index) {
    return (int)posix_result(k_syscall3(402,(long)path,(long)entry,index));
}
int shutdown_system(void) {return (int)posix_result(k_syscall3(404,0,0,0));}

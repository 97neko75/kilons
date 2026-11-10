#ifndef KILONS_USER_UNISTD_H
#define KILONS_USER_UNISTD_H
#include <stddef.h>
/* libc translates kernel -errno returns into POSIX -1 and errno. */
extern int errno;
#define EBADF 9
#define ECHILD 10
#define EFAULT 14
#define EACCES 13
#define EAGAIN 11
#define EEXIST 17
#define ESPIPE 29
#define ENOENT 2
long k_syscall3(long nr,long a,long b,long c);
long write(int fd,const void *buf,size_t n);
long read(int fd,void *buf,size_t n);
int open(const char *path,int flags,int mode);
struct k_stat {unsigned long mode,size;};
int stat(const char *path,struct k_stat *out);
int fstat(int fd,struct k_stat *out);
int close(int fd);
int dup(int fd);
int pipe(int fds[2]);
int dup2(int oldfd,int newfd);
long lseek(int fd,long offset,int whence);
int getpid(void);
int getppid(void);
int fork(void);
int execve(const char *path,char *const argv[],char *const envp[]);
int chdir(const char *path);
char *getcwd(char *buffer,size_t length);
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32
void *mmap(void *addr,size_t length,int prot,int flags,int fd,long offset);
int munmap(void *addr,size_t length);
int mprotect(void *addr,size_t length,int prot);
int mkdir(const char *path,int mode);
int rmdir(const char *path);
int unlink(const char *path);
int rename(const char *oldpath,const char *newpath);
int truncate(const char *path, size_t length);
int ftruncate(int fd, size_t length);
int sched_yield(void);
int spawn(const char *name);
int waitpid(int pid,int *status,int options);
struct k_dirent {char name[128];int is_dir;};
int k_readdir(const char *path,struct k_dirent *entry,unsigned index);
int shutdown_system(void);
void _exit(int status) __attribute__((noreturn));
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define SEEK_SET 0
#define WNOHANG 1
#endif

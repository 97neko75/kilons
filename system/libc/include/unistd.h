#ifndef KILONS_PUBLIC_UNISTD_H
#define KILONS_PUBLIC_UNISTD_H
#include <stddef.h>
#include "sys/types.h"
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define SEEK_DATA 3
#define SEEK_HOLE 4
ssize_t read(int,void *,size_t);
ssize_t write(int,const void *,size_t);
ssize_t pread(int,void *,size_t,off_t);
ssize_t pwrite(int,const void *,size_t,off_t);
int close(int);
int fsync(int);
int fdatasync(int);
#define F_ULOCK 0
#define F_LOCK 1
#define F_TLOCK 2
#define F_TEST 3
int lockf(int, int, off_t);
int isatty(int);
pid_t fork(void);
pid_t vfork(void) __attribute__((returns_twice));
pid_t getpid(void);
pid_t getppid(void);
pid_t getpgrp(void);
pid_t getpgid(pid_t);
int setpgid(pid_t,pid_t);
pid_t getsid(pid_t);
pid_t setsid(void);
int pipe(int [2]);
int dup(int);
int dup2(int,int);
off_t lseek(int,off_t,int);
int fchdir(int);
int access(const char *,int);
int faccessat(int,const char *,int,int);
int chown(const char *,uid_t,gid_t);
int lchown(const char *,uid_t,gid_t);
int fchown(int,uid_t,gid_t);
int fchownat(int,const char *,uid_t,gid_t,int);
int link(const char *,const char *);
int linkat(int,const char *,int,const char *,int);
int unlinkat(int,const char *,int);
int dup3(int,int,int);
int pipe2(int [2],int);
int setuid(uid_t);
int seteuid(uid_t);
int setgid(gid_t);
int setegid(gid_t);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
int chdir(const char *);
char *getcwd(char *,size_t);
int symlinkat(const char *,int,const char *);
ssize_t readlinkat(int,const char *,char *,size_t);
int symlink(const char *,const char *);
ssize_t readlink(const char *,char *,size_t);
int unlink(const char *);
int rmdir(const char *);
int truncate(const char *,off_t);
int ftruncate(int,off_t);
int execve(const char *,char *const [],char *const []);
int fexecve(int,char *const [],char *const []);
int execv(const char *,char *const []);
int execl(const char *,const char *,...);
int execle(const char *,const char *,...);
int execlp(const char *,const char *,...);
int execvp(const char *,char *const []);
void _exit(int) __attribute__((noreturn));
int pause(void);
#endif

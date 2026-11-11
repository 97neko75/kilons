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
ssize_t read(int,void *,size_t);
ssize_t write(int,const void *,size_t);
int close(int);
int fsync(int);
int isatty(int);
pid_t fork(void);
pid_t getpid(void);
pid_t getppid(void);
int pipe(int [2]);
int dup(int);
int dup2(int,int);
off_t lseek(int,off_t,int);
int chdir(const char *);
char *getcwd(char *,size_t);
int unlink(const char *);
int rmdir(const char *);
int truncate(const char *,off_t);
int ftruncate(int,off_t);
int execve(const char *,char *const [],char *const []);
int execvp(const char *,char *const []);
void _exit(int) __attribute__((noreturn));
#endif

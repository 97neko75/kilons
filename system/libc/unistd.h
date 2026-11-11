#ifndef KILONS_USER_UNISTD_H
#define KILONS_USER_UNISTD_H
#include <stddef.h>
/* libc translates kernel -errno returns into POSIX -1 and errno. */
#include "include/unistd.h"
#include "include/fcntl.h"
#include "include/errno.h"
#include "include/sys/stat.h"
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32
void *mmap(void *addr,size_t length,int prot,int flags,int fd,long offset);
int munmap(void *addr,size_t length);
int mprotect(void *addr,size_t length,int prot);
#include "include/sched.h"
#include "include/sys/wait.h"
int rename(const char *,const char *);
#endif

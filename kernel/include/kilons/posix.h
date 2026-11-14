#ifndef KILONS_POSIX_H
#define KILONS_POSIX_H
#include <stdint.h>
#include <stddef.h>
int posix_open_exec(const char *);
#include <stdbool.h>
/* False when no per-process FD/credential slot could be allocated. */
bool posix_task_begin(uint32_t pid);
bool posix_task_clone(uint32_t parent,uint32_t child);
void posix_task_discard(uint32_t pid);
void posix_task_activate(uint32_t pid);
void posix_task_end(void);
unsigned posix_saved_uid(void);
unsigned posix_effective_uid(void);
unsigned posix_real_uid(void);
void posix_exec_ids(unsigned,unsigned,unsigned);
void posix_task_exec_close(uint32_t pid);
bool posix_blocking_pipe_writer(uint32_t,uint64_t);
bool posix_io_should_wait(uint32_t pid,uint64_t fd,bool write,uint64_t bytes);
int posix_exec_backend(int fd);
long posix_positioned_io(bool,int,uint64_t,uint64_t,int64_t);
long posix_chownat(int,uint64_t,unsigned,unsigned,int,int);
long posix_utimens(int,uint64_t,uint64_t,int,int);
long posix_linkat(int,uint64_t,int,uint64_t,int);
long posix_renameat(int,uint64_t,int,uint64_t);
long posix_faccessat(int,uint64_t,int,int);
long posix_fchmodat(int,uint64_t,unsigned,int);
long posix_openat(int,uint64_t,uint64_t,unsigned);
long posix_readlinkat(int,uint64_t,uint64_t,uint64_t);
long posix_fstatat(int,uint64_t,uint64_t,int,uint64_t);
long posix_dispatch(uint64_t nr,uint64_t a,uint64_t b,uint64_t c);
#endif

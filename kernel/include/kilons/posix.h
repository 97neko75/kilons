#ifndef KILONS_POSIX_H
#define KILONS_POSIX_H
#include <stdint.h>
#include <stddef.h>
int posix_task_path(const char *,char *,size_t);
#include <stdbool.h>
/* False when no per-process FD/credential slot could be allocated. */
bool posix_task_begin(uint32_t pid);
bool posix_task_clone(uint32_t parent,uint32_t child);
void posix_task_discard(uint32_t pid);
void posix_task_activate(uint32_t pid);
void posix_task_end(void);
void posix_task_exec_close(uint32_t pid);
bool posix_io_should_wait(uint32_t pid,uint64_t fd,bool write,uint64_t bytes);
long posix_dispatch(uint64_t nr,uint64_t a,uint64_t b,uint64_t c);
#endif

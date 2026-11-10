#ifndef KILONS_POSIX_H
#define KILONS_POSIX_H
#include <stdint.h>
#include <stdbool.h>
void posix_task_begin(uint32_t pid);
bool posix_task_clone(uint32_t parent,uint32_t child);
void posix_task_discard(uint32_t pid);
void posix_task_activate(uint32_t pid);
void posix_task_end(void);
long posix_dispatch(uint64_t nr,uint64_t a,uint64_t b,uint64_t c);
#endif

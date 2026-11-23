#ifndef KILONS_SYS_MMAN_H
#define KILONS_SYS_MMAN_H
#include <stddef.h>
#include "types.h"

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 16
#define MAP_ANONYMOUS 32
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

void *mmap(void *,size_t,int,int,int,off_t);
int munmap(void *,size_t);
int mprotect(void *,size_t,int);

#endif

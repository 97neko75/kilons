#ifndef KILONS_FCNTL_H
#define KILONS_FCNTL_H
#include "sys/types.h"
#include "bits/fcntl_flags.h"
#include "bits/record_lock.h"
int fcntl(int,int,...);
int creat(const char *,mode_t);
int openat(int,const char *,int,...);
int open(const char *,int,...);
#endif

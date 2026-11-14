#ifndef KILONS_KERNEL_RECORD_LOCK_H
#define KILONS_KERNEL_RECORD_LOCK_H
#include <stdint.h>
#include "../../../include/record_lock.h"
struct lock_file {
    const void *filesystem;
    uint64_t inode;
};
int record_request(int pid,int fd,int command,struct flock *arg);
int record_resume(int pid);
void record_close(int pid,struct lock_file file);
void record_cancel_wait(int pid);
void record_exit(int pid);
#endif

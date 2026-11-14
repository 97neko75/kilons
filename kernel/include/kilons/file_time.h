#ifndef KILONS_FILE_TIME_H
#define KILONS_FILE_TIME_H
#include <stdint.h>
struct file_times { int64_t access,modify,change; };
#if __STDC_HOSTED__
#include <time.h>
static inline int64_t file_now(void) { return (int64_t)time(0); }
#else
int64_t file_now(void);
void wall_clock_init(void);
#endif
struct inode_update {
    int ownership;
    unsigned uid,gid;
    int64_t seconds[2];
    unsigned mask;
    int explicit_time;
};
static inline void change_times(struct file_times *times,const struct inode_update *update) {
    if(update->mask&1)times->access=update->seconds[0];
    if(update->mask&2)times->modify=update->seconds[1];
    times->change=file_now();
}
static inline struct file_times file_created(void) {
    int64_t now=file_now();
    return (struct file_times){now,now,now};
}
#endif

#ifndef KILONS_USER_TIME_H
#define KILONS_USER_TIME_H
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {long tv_sec;long tv_nsec;};
#endif
int clock_gettime(int clock_id,struct timespec *result);
int nanosleep(const struct timespec *requested,struct timespec *remaining);
#endif

#ifndef KILONS_USER_TIME_H
#define KILONS_USER_TIME_H
#define CLOCK_MONOTONIC 1
struct timespec {long tv_sec;long tv_nsec;};
int clock_gettime(int clock_id,struct timespec *result);
int nanosleep(const struct timespec *requested,struct timespec *remaining);
#endif

#include "syscall_internal.h"
#include "time.h"
#include "unistd.h"
/* Early libc nanosleep uses cooperative yields, NOT kernel blocking sleep.
 * Accuracy limited to PIT nominal 100 Hz, no signal interruption support. */
int clock_gettime(int clock_id,struct timespec *out){
    long rc=k_syscall3(228,clock_id,(long)out,0);
    if(rc<0 && rc>=-4095){errno=(int)-rc;return -1;}
    return (int)rc;
}
int nanosleep(const struct timespec *req,struct timespec *rem){
    if(!req){errno=14;return -1;}
    if(req->tv_sec<0||req->tv_nsec<0||req->tv_nsec>=1000000000L){errno=22;return -1;}
    struct timespec start,now;
    if(clock_gettime(CLOCK_MONOTONIC,&start)<0)return -1;
    /* Overflow guarded before comparing nanosecond elapsed time. */
    if(req->tv_sec>3600L){errno=22;return -1;} /* bounded prototype */
    unsigned long target=(unsigned long)req->tv_sec*100U+
       (unsigned long)(req->tv_nsec+9999999L)/10000000UL;
    for(;;){
        if(clock_gettime(CLOCK_MONOTONIC,&now)<0)return -1;
        unsigned long elapsed=(unsigned long)(now.tv_sec-start.tv_sec)*100UL;
        if(now.tv_nsec>=start.tv_nsec)elapsed+=(unsigned long)(now.tv_nsec-start.tv_nsec)/10000000UL;
        else elapsed-=(unsigned long)(start.tv_nsec-now.tv_nsec+9999999L)/10000000UL;
        if(elapsed>=target)break;
        if(sched_yield()<0)return -1;
    }
    if(rem){rem->tv_sec=0;rem->tv_nsec=0;}
    return 0;
}

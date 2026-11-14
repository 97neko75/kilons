#include "syscall_internal.h"
#include "time.h"
#include "unistd.h"
int clock_gettime(int clock_id,struct timespec *out){
    long rc=k_syscall3(228,clock_id,(long)out,0);
    if(rc<0 && rc>=-4095){errno=(int)-rc;return -1;}
    return (int)rc;
}
int nanosleep(const struct timespec *req,struct timespec *rem){
    long rc=k_syscall3(35,(long)req,(long)rem,0);
    if(rc<0){errno=(int)-rc;return -1;}
    return 0;
}

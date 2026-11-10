#include "../libc/unistd.h"
/* Small userspace stream filter for PKS pipeline integration tests. */
void _start(void) {
    char buf[128];
    for(;;) {
        long n=read(0,buf,sizeof(buf));
        if(n==0)break;
        if(n<0) {
            if(errno==EAGAIN) { (void)sched_yield();continue; }
            _exit(101);
        }
        long done=0;
        while(done<n) {
            long sent=write(1,buf+done,(size_t)(n-done));
            if(sent<0) {
                if(errno==EAGAIN) {(void)sched_yield();continue;}
                _exit(102);
            }
            if(!sent)_exit(103);
            done+=sent;
        }
    }
    _exit(0);
}

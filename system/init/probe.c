#include "../libc/unistd.h"
/* Independent image instances intentionally use identical virtual addresses.
 * Each child must retain private writable data through repeated context switches. */
static volatile unsigned long marker;
void _start(void) {
    unsigned long own=(unsigned long)getpid();
    if(own<2 || getppid()<1)_exit(51);
    marker=own^0x4b494c4fUL;
    for(unsigned i=0;i<8;i++) {
        if(marker!=(own^0x4b494c4fUL))_exit(52);
        if(sched_yield()!=0)_exit(53);
    }
    if(marker!=(own^0x4b494c4fUL))_exit(54);
    _exit(0);
}

#include "../libc/unistd.h"
/* The entire purpose of this ELF64 is to run without voluntary yields.
 * An IRQ0 user scheduler must still let other child processes make progress. */
static volatile unsigned accumulator;
void _start(void) {
    for(unsigned i=0;i<100000000U;i++)accumulator+=(i&3U);
    if(!accumulator)_exit(71);
    _exit(0);
}

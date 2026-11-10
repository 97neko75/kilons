#include "../libc/unistd.h"
/* Deliberate user-mode #DE fault, must not terminate the kernel or PKS. */
void _start(void) {
    volatile unsigned divisor=0;
    volatile unsigned numerator=19;
    volatile unsigned quotient=numerator/divisor;
    (void)quotient;
    _exit(99);
}

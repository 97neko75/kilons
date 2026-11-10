#include "../libc/unistd.h"
/* Deliberate user-mode access to supervisor-only kernel memory.
 * A correct VM and exception handler terminate ONLY this process. */
void _start(void) {
    static const char msg[]="Ring 3 fault probe: attempting protected kernel read\n";
    (void)write(1,msg,sizeof(msg)-1);
    volatile unsigned long protected_value=*(volatile unsigned long *)0x100000UL;
    (void)protected_value;
    _exit(99); /* kernel mapping was not isolated: test MUST fail */
}

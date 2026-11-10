#include "../libc/unistd.h"
/* User-mode #UD exception isolation test; not reached beyond ud2. */
void _start(void) {
    __asm__ volatile("ud2");
    _exit(99);
}

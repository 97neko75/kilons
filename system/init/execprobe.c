#include "../libc/unistd.h"
/* A successful execve replaces this image with /bin/second. */
void _start(void) {
    char *argv[]={"second","from-execve",0};
    char *envp[]={"KILONS=0.3.3",0};
    int rc=execve("/bin/second",argv,envp);
    (void)rc;
    _exit(76);
}

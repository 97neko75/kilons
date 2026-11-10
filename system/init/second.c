#include "../libc/unistd.h"
/* Distinct ELF64 binary: verifies a new PID, fresh user stack/data and VFS. */
static char scratch[80];
static const char msg[]="Ring 3 second ELF64: independent image and PID PASS\n";
void _start(void) {
    if(getpid()<2 || getppid()<1) _exit(41);
    for(unsigned i=0;i<sizeof(scratch);i++) if(scratch[i]) _exit(42);
    scratch[0]='S';
    static const char yielding[]="Ring 3 second ELF64: yielding with private data\n";
    (void)write(1,yielding,sizeof(yielding)-1);
    if(sched_yield()!=0 || scratch[0]!='S') _exit(50);
    int fd=open("/tmp/second-program",O_CREAT|O_RDWR,0644);
    if(fd<3) _exit(43);
    static const char data[]="second ELF64 process";
    if(write(fd,data,sizeof(data)-1)!=(long)sizeof(data)-1) _exit(44);
    /* Both processes may hold the same local descriptor number concurrently. */
    if(sched_yield()!=0) _exit(51);
    if(lseek(fd,0,SEEK_SET)!=0) _exit(45);
    if(read(fd,scratch,sizeof(data)-1)!=(long)sizeof(data)-1) _exit(46);
    for(unsigned i=0;i<sizeof(data)-1;i++) if(scratch[i]!=data[i]) _exit(47);
    if(close(fd)!=0 || unlink("/tmp/second-program")!=0) _exit(48);
    if(write(1,msg,sizeof(msg)-1)!=(long)sizeof(msg)-1) _exit(49);
    _exit(0);
}

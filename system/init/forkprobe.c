#include "../libc/unistd.h"
/* Eager fork copy: the child's data and register state are independent, but
 * inherited file descriptors share an open description and its offset. */
static volatile unsigned marker;
void _start(void) {
    marker=0x12345678u;
    int fd=open("/tmp/fork-shared",O_CREAT|O_RDWR,0644);
    if(fd<3 || write(fd,"AB",2)!=2 || lseek(fd,0,SEEK_SET)!=0)_exit(71);
    int pid=fork();
    if(pid<0)_exit(72);
    if(pid==0) {
        char ch=0;
        if(marker!=0x12345678u || read(fd,&ch,1)!=1 || ch!='A')_exit(73);
        marker=0x87654321u;
        (void)close(fd);
        _exit(0);
    }
    int status=-1;char c=0;
    if(waitpid(pid,&status,0)!=pid || status!=0 || marker!=0x12345678u ||
       read(fd,&c,1)!=1 || c!='B')_exit(74);
    (void)close(fd);
    if(unlink("/tmp/fork-shared")!=0)_exit(75);
    static const char ok[]="forkprobe: eager copy + inherited open-file offset PASS\n";
    (void)write(1,ok,sizeof(ok)-1);
    _exit(0);
}

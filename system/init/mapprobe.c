#include "../libc/unistd.h"
void _start(void) {
    volatile unsigned char *mem=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if((long)mem<0)_exit(81);
    mem[0]=0x4b;mem[4096]=0x97;
    if(mem[0]!=0x4b || mem[4096]!=0x97)_exit(82);
    if(mprotect((void *)mem,4096,PROT_READ)!=0)_exit(83);
    if(munmap((void *)mem,8192)!=0)_exit(84);
    char cwd[256];
    if(!getcwd(cwd,sizeof(cwd)) || cwd[0]!='/')_exit(85);
    if(chdir("/tmp")!=0)_exit(86);
    if(!getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1]!='t' || cwd[2]!='m' || cwd[3]!='p')_exit(87);
    static const char ok[]="mapprobe: mmap/mprotect/munmap + cwd PASS\n";
    (void)write(1,ok,sizeof(ok)-1);
    _exit(0);
}

#include "../libc/unistd.h"
void _start(void) {
    if(mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE,-1,0)!=(void *)-1 || errno!=22)_exit(88);
    volatile unsigned char *mem=mmap(0,1,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if((long)mem<0)_exit(81);
    mem[0]=0x4b;
    if(mem[0]!=0x4b)_exit(82);
    if(mprotect((void *)mem,4096,PROT_READ)!=0)_exit(83);
    if(munmap((void *)mem,1)!=0)_exit(84);
    if(munmap((void *)mem,4096)==0 || errno!=22)_exit(88);
    mem=mmap(0,4096,PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if((long)mem<0)_exit(89);
    mem[0]=0x97;
    if(mem[0]!=0x97 || mprotect((void *)mem,4096,PROT_WRITE)!=0)_exit(90);
    mem[0]=0x75;
    if(mem[0]!=0x75 || munmap((void *)mem,4096)!=0)_exit(90);
    char cwd[256];
    if(!getcwd(cwd,sizeof(cwd)) || cwd[0]!='/')_exit(85);
    if(chdir("/tmp")!=0)_exit(86);
    if(!getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1]!='t' || cwd[2]!='m' || cwd[3]!='p')_exit(87);
    static const char ok[]="mapprobe: mmap/mprotect/munmap + cwd PASS\n";
    (void)write(1,ok,sizeof(ok)-1);
    _exit(0);
}

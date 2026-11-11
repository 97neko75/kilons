#include "../libc/unistd.h"
#include "../libc/dirent.h"
#include "../libc/env.h"
#include "../libc/time.h"
static void fail(int code){_exit(code);}
void _start(void){
    int fd=open("/tmp/posix-example",O_CREAT|O_RDWR,0644);
    if(fd<3)fail(1);
    if(write(fd,"kilons",6)!=6)fail(2);
    if(fsync(fd)<0)fail(3);
    if(fcntl(fd,F_GETFL,0)!=O_RDWR)fail(4);
    if(fcntl(fd,F_SETFD,FD_CLOEXEC)<0)fail(5);
    if(fcntl(fd,F_GETFD,0)!=FD_CLOEXEC)fail(6);
    int other=dup(fd);if(other<3)fail(7);
    if(fcntl(other,F_GETFD,0)!=0)fail(8);
    if(lseek(other,0,SEEK_SET)!=0)fail(9);
    char buf[8]={0};if(read(fd,buf,6)!=6)fail(10);
    if(buf[0]!='k'||buf[5]!='s')fail(11);
    if(close(other)<0||close(fd)<0)fail(12);
    DIR *dir=opendir("/tmp");if(!dir)fail(13);
    unsigned found=0;struct dirent *entry;
    while((entry=readdir(dir))){
        if(entry->d_name[0]=='p')found=1;
    }
    if(closedir(dir)<0||!found)fail(14);
    if(setenv("KILONS","test",1)<0)fail(15);
    if(!getenv("KILONS")||getenv("KILONS")[0]!='t')fail(16);
    struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)<0)fail(17);
    if(unlink("/tmp/posix-example")<0)fail(18);
    _exit(0);
}

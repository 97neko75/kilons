#include "dirent.h"
#include "unistd.h"
struct directory_stream {int used,fd;struct dirent entry;};
static DIR streams[4];
DIR *opendir(const char *path){
    if(!path || !*path){errno=path?ENOENT:EFAULT;return 0;}
    unsigned i=0;
    while(i<4 && streams[i].used)i++;
    if(i==4){errno=24;return 0;}
    int fd=open(path,O_RDONLY);
    if(fd<0)return 0;
    struct stat st;
    int error=fstat(fd,&st)<0?errno:S_ISDIR(st.st_mode)?0:20;
    if(!error && fcntl(fd,F_SETFD,FD_CLOEXEC)<0)error=errno;
    if(error){close(fd);errno=error;return 0;}
    streams[i].used=1;streams[i].fd=fd;
    return &streams[i];
}
struct dirent *readdir(DIR *dir){
    if(!dir||!dir->used){errno=EBADF;return 0;}
    long n=read(dir->fd,&dir->entry,sizeof dir->entry);
    if(n==(long)sizeof dir->entry)return &dir->entry;
    if(n>0)errno=5;
    return 0;
}
int closedir(DIR *dir){
    if(!dir||!dir->used){errno=EBADF;return -1;}
    dir->used=0;return close(dir->fd);
}

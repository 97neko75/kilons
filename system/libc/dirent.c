#include "dirent.h"
#include "unistd.h"
struct directory_stream {int used,fd;struct dirent entry;};
static DIR streams[4];
DIR *fdopendir(int fd) {
    struct stat st;
    if(fstat(fd,&st)<0)return 0;
    if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return 0;}
    int flags=fcntl(fd,F_GETFL);
    if(flags<0)return 0;
    if((flags&O_ACCMODE)!=O_RDONLY && (flags&O_ACCMODE)!=O_RDWR){errno=EBADF;return 0;}
    for(unsigned i=0;i<sizeof streams/sizeof streams[0];i++) {
        if(streams[i].used)continue;
        streams[i].used=1;
        streams[i].fd=fd;
        return &streams[i];
    }
    errno=EMFILE;return 0;
}
DIR *opendir(const char *path) {
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)return 0;
    DIR *stream=fdopendir(fd);
    if(!stream){int saved=errno;close(fd);errno=saved;}
    return stream;
}
int dirfd(DIR *dir) {
    if(!dir || !dir->used){errno=EBADF;return -1;}
    return dir->fd;
}
void rewinddir(DIR *dir) {
    int fd=dirfd(dir);
    if(fd>=0)(void)lseek(fd,0,SEEK_SET);
}
long telldir(DIR *dir) {
    int fd=dirfd(dir);
    return fd<0?-1:lseek(fd,0,SEEK_CUR);
}
void seekdir(DIR *dir,long location) {
    int fd=dirfd(dir);
    if(fd>=0)(void)lseek(fd,location,SEEK_SET);
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

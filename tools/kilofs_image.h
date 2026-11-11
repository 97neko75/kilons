#ifndef KILOFS_IMAGE_H
#define KILOFS_IMAGE_H
#define _POSIX_C_SOURCE 200809L
#include <kilons/kilofs.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
struct kfs_host_image {int fd;};
static inline int kfs_host_read(void *p,uint32_t block,void *buffer){
    struct kfs_host_image *h=p;
    off_t start=(off_t)block*KFS_BLOCK;
    unsigned char *dst=buffer;size_t done=0;
    while(done<KFS_BLOCK){
        ssize_t n=pread(h->fd,dst+done,KFS_BLOCK-done,start+(off_t)done);
        if(n<=0)return -5;
        done+=(size_t)n;
    }
    return 0;
}
static inline int kfs_host_write(void *p,uint32_t block,const void *buffer){
    struct kfs_host_image *h=p;
    off_t start=(off_t)block*KFS_BLOCK;
    const unsigned char *src=buffer;size_t done=0;
    while(done<KFS_BLOCK){
        ssize_t n=pwrite(h->fd,src+done,KFS_BLOCK-done,start+(off_t)done);
        if(n<=0)return -5;
        done+=(size_t)n;
    }
    return 0;
}
static inline int kfs_host_flush(void *p){
    struct kfs_host_image *h=p;return fsync(h->fd)==0?0:-5;
}
#endif

#define _POSIX_C_SOURCE 200809L
#include "kilofs_image.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
static struct kilofs fs;
int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s IMAGE\nread-only, reports errors; does not repair\n",argv[0]);return 2;}
    int fd=open(argv[1],O_RDONLY);
    if(fd<0){perror("open");return 2;}
    struct stat st;
    if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<8*(off_t)KFS_BLOCK||
        st.st_size%(off_t)KFS_BLOCK||st.st_size/(off_t)KFS_BLOCK>KFS_MAX_BLOCKS){
        fprintf(stderr,"invalid image size/type\n");close(fd);return 2;
    }
    struct kfs_host_image host={fd};
    struct kfs_device dev={&host,(uint32_t)(st.st_size/KFS_BLOCK),kfs_host_read,0,kfs_host_flush};
    int rc=kfs_mount(&fs,dev,1);
    if(rc<0){fprintf(stderr,"invalid kilofs superblock or root inode: %d\n",rc);close(fd);return 1;}
    unsigned errors=0;rc=kfs_fsck(&fs,&errors);
    if(rc<0){fprintf(stderr,"read failure: %d\n",rc);close(fd);return 1;}
    printf("kilofs check: %u consistency errors\n",errors);
    close(fd);return errors?1:0;
}

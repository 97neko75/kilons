#define _POSIX_C_SOURCE 200809L
#include "kilofs_image.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
static struct kilofs fs;
int main(int argc,char **argv){
    int repair=0;const char *image=0;
    if(argc==2)image=argv[1];
    else if(argc==3&&!strcmp(argv[1],"--repair")){repair=1;image=argv[2];}
    else {fprintf(stderr,"usage: %s [--repair] IMAGE\n",argv[0]);return 2;}
    int fd=open(image,repair?O_RDWR:O_RDONLY);
    if(fd<0){perror("open");return 2;}
    struct stat st;
    if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<(off_t)(KFS_DATA_START+1)*KFS_BLOCK||
        st.st_size%(off_t)KFS_BLOCK||st.st_size/(off_t)KFS_BLOCK>KFS_MAX_BLOCKS){
        fprintf(stderr,"invalid image size/type\n");close(fd);return 2;
    }
    struct kfs_host_image host={fd};
    struct kfs_device dev={&host,(uint32_t)(st.st_size/KFS_BLOCK),kfs_host_read,
                           repair?kfs_host_write:0,kfs_host_flush};
    int rc=kfs_mount(&fs,dev,!repair);
    if(rc<0){fprintf(stderr,"invalid kilofs superblock or root inode: %d\n",rc);close(fd);return 1;}
    unsigned errors=0;
    rc=repair?kfs_fsck_repair(&fs,&errors):kfs_fsck(&fs,&errors);
    if(rc<0){
        fprintf(stderr,"kilofs %s failed: %d (consistency errors: %u)\n",
                repair?"repair":"check",rc,errors);
        close(fd);return 1;
    }
    printf("kilofs %s: %u consistency errors\n",repair?"repair":"check",errors);
    close(fd);return errors?1:0;
}

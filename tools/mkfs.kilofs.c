#define _POSIX_C_SOURCE 200809L
#include "kilofs_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
static struct kilofs fs;
int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"usage: %s NEW_IMAGE BLOCKS\nrefuses existing paths and device nodes\n",argv[0]);return 2;}
    char *end=0;unsigned long blocks=strtoul(argv[2],&end,10);
    if(!*argv[2]||*end||blocks<8||blocks>KFS_MAX_BLOCKS){fprintf(stderr,"invalid block count (8..%u)\n",KFS_MAX_BLOCKS);return 2;}
    /* O_EXCL is intentional: never overwrite an existing disk or image. */
    int fd=open(argv[1],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0){perror("create image");return 2;}
    struct stat st;
    if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)){
        fprintf(stderr,"only new regular image files are supported\n");close(fd);unlink(argv[1]);return 2;
    }
    if(ftruncate(fd,(off_t)blocks*KFS_BLOCK)<0){perror("ftruncate");close(fd);unlink(argv[1]);return 2;}
    close(fd);fd=open(argv[1],O_RDWR);
    if(fd<0){perror("reopen");return 2;}
    struct kfs_host_image host={fd};
    struct kfs_device dev={&host,(uint32_t)blocks,kfs_host_read,kfs_host_write,kfs_host_flush};
    int rc=kfs_format(&fs,dev);
    if(rc<0){fprintf(stderr,"kilofs format failed: %d\n",rc);close(fd);return 1;}
    unsigned errors=0;rc=kfs_fsck(&fs,&errors);
    if(rc<0||errors){fprintf(stderr,"format verification failed: rc=%d errors=%u\n",rc,errors);close(fd);return 1;}
    printf("created %s: %lu blocks, %lu bytes, kilofs format v1\n",argv[1],blocks,blocks*(unsigned long)KFS_BLOCK);
    close(fd);return 0;
}

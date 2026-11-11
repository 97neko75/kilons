#define _POSIX_C_SOURCE 200809L
#include "kilofs_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
/* Install only into a newly created regular image. No existing image or device
 * may be modified by this tool. Arguments are DEST=HOSTFILE pairs. */
static struct kilofs fs;
static int install(const char *spec){
    const char *eq=strchr(spec,'=');
    if(!eq||eq==spec||!eq[1]||(size_t)(eq-spec)>=256)return -1;
    char dest[256];memcpy(dest,spec,(size_t)(eq-spec));dest[eq-spec]=0;
    if(dest[0]!='/'||strstr(dest,"..")||strstr(dest,"//"))return -1;
    const char *source=eq+1;
    int input=open(source,O_RDONLY|O_NOFOLLOW);
    if(input<0){perror(source);return -1;}
    struct stat st;
    if(fstat(input,&st)<0||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>(off_t)(KFS_MAX_FILE_BLOCKS*KFS_BLOCK)){
        fprintf(stderr,"invalid executable size/type: %s\n",source);close(input);return -1;
    }
    unsigned char magic[4];
    if(read(input,magic,4)!=4||memcmp(magic,"\177ELF",4)!=0||lseek(input,0,SEEK_SET)<0){
        fprintf(stderr,"not an ELF: %s\n",source);close(input);return -1;
    }
    int fd=kfs_open(&fs,dest,KFS_WRITE,1,0755);
    if(fd<0){fprintf(stderr,"cannot create %s: %d\n",dest,fd);close(input);return -1;}
    unsigned char buffer[4096];off_t done=0;
    while(done<st.st_size){
        ssize_t n=read(input,buffer,sizeof(buffer));
        if(n<=0||kfs_write(&fs,fd,buffer,(size_t)n)!=n){fprintf(stderr,"write failed: %s\n",dest);kfs_close(&fs,fd);close(input);return -1;}
        done+=n;
    }
    close(input);
    if(kfs_close(&fs,fd)<0||kfs_chmod(&fs,dest,0755)<0)return -1;
    printf("installed %s (%lld bytes)\n",dest,(long long)st.st_size);
    return 0;
}
int main(int argc,char **argv){
    if(argc<4){fprintf(stderr,"usage: %s NEW_IMAGE BLOCKS /bin/name=build/user_name.elf ...\n",argv[0]);return 2;}
    char *end=0;unsigned long blocks=strtoul(argv[2],&end,10);
    if(!*argv[2]||*end||blocks<8||blocks>KFS_MAX_BLOCKS)return 2;
    int fd=open(argv[1],O_CREAT|O_EXCL|O_RDWR|O_NOFOLLOW,0600);
    if(fd<0){perror("create new image");return 2;}
    struct stat st;
    if(fstat(fd,&st)<0||!S_ISREG(st.st_mode)||ftruncate(fd,(off_t)blocks*KFS_BLOCK)<0){
        perror("prepare image");close(fd);unlink(argv[1]);return 2;
    }
    struct kfs_host_image host={fd};
    struct kfs_device dev={&host,(uint32_t)blocks,kfs_host_read,kfs_host_write,kfs_host_flush};
    int rc=kfs_format(&fs,dev);
    if(rc==0)rc=kfs_mkdir(&fs,"/bin",0755);
    if(rc==0)rc=kfs_mkdir(&fs,"/sbin",0755);
    if(rc==0)rc=kfs_mkdir(&fs,"/mnt",0755);
    if(rc==0)rc=kfs_mkdir(&fs,"/mnt/disk",0755);
    if(rc==0)rc=kfs_mkdir(&fs,"/dev",0755);
    if(rc==0)rc=kfs_mkdir(&fs,"/etc",0755);
    if(rc==0)rc=kfs_mkdir(&fs,"/tmp",01777);
    if(rc==0)rc=kfs_mkdir(&fs,"/var",0755);
    if(rc==0)rc=kfs_mkdir(&fs,"/home",0755);
    for(int i=3;i<argc&&rc==0;i++)rc=install(argv[i]);
    if(rc==0)rc=kfs_sync(&fs);
    unsigned errors=0;
    if(rc==0)rc=kfs_fsck(&fs,&errors);
    if(rc<0||errors){fprintf(stderr,"rootfs creation failed: rc=%d errors=%u\n",rc,errors);close(fd);unlink(argv[1]);return 1;}
    close(fd);printf("kilofs root filesystem image: %s (%lu blocks)\n",argv[1],blocks);
    return 0;
}

/* Mount the exact image built for GRUB through the production block/VFS code. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kilons/block.h>
#include <kilons/ramfs.h>
#include <kilons/vfs.h>
#include "../include/dirent.h"
static bool sector(void *context,uint64_t lba,uint8_t dst[512]){
    FILE *image=context;
    return !fseek(image,(long)(lba*512),SEEK_SET) && fread(dst,1,512,image)==512;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);
    FILE *image=fopen(argv[1],"rb");assert(image);
    assert(!fseek(image,0,SEEK_END));
    long size=ftell(image);assert(size>0 && size%512==0);
    block_init();ramfs_init();vfs_init();
    struct block_device device={(uint64_t)size/512,sector,0,image};
    int disk=block_register(&device);assert(disk>=0);
    assert(vfs_mkdir("/mnt/disk")==0 && vfs_mount_kilofs("/mnt/disk",disk,0)==0);
    assert(vfs_use_boot_root()==0);
    vfs_set_credentials(1000,1000);
    int init=vfs_open("/sbin/init",VFS_READ,0);
    char magic[4];assert(init>=3 && vfs_read(init,magic,4)==4 && !memcmp(magic,"\177ELF",4));
    if(argc==3){
        int extra=vfs_open(argv[2],VFS_READ,0);
        assert(extra>=3 && vfs_access(argv[2],1)==0);
        assert(vfs_read(extra,magic,4)==4 && !memcmp(magic,"\177ELF",4));
        assert(vfs_close(extra)==0);
    }
    assert(vfs_access("/sbin/init",1)==0 && vfs_close(init)==0);
    assert(vfs_open("/bin/cat",VFS_WRITE,0)<0);
    int directory=vfs_open("/bin",VFS_READ,0);
    struct dirent d;int rc,found=0;
    while((rc=vfs_read(directory,&d,sizeof d))>0){
        assert(rc==(int)sizeof d && d.d_ino);
        if(!strcmp(d.d_name,"cat"))found=1;
    }
    assert(!rc && found && vfs_close(directory)==0);
    assert(vfs_mkdir("/tmp/a")==0);
    directory=vfs_open("/tmp/a",VFS_READ,0);
    int file=vfs_open("/tmp/a/sample",VFS_WRITE,1);
    assert(file>=0 && vfs_close(file)==0);
    assert(vfs_rename("/tmp/a","/tmp/b")==0 && vfs_retain(directory)==0);
    assert(vfs_read(directory,&d,sizeof d)==(int)sizeof d && !strcmp(d.d_name,"sample"));
    assert(vfs_close(directory)==0 && vfs_read(directory,&d,sizeof d)==0);
    assert(vfs_unlink("/tmp/b/sample")==0 && vfs_rmdir("/tmp/b")==0 && vfs_mkdir("/tmp/b")==0);
    assert(vfs_read(directory,&d,sizeof d)==0 && vfs_close(directory)==0);
    fclose(image);puts("Boot root /sbin/init + read-only /bin + writable /tmp + directory lifetime PASS");
}

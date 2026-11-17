/* Mount the exact minimal image used by the Limine advanced entry through the
 * production block/VFS path, then switch it into the boot-root layout. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kilons/block.h>
#include <kilons/kerrno.h>
#include <kilons/ramfs.h>
#include <kilons/vfs.h>

static bool sector(void *context,uint64_t lba,uint8_t dst[512]){
    FILE *image=context;
    return !fseek(image,(long)(lba*512U),SEEK_SET) && fread(dst,1,512,image)==512;
}

static void check_mount_layout(void){
    struct vfs_mount_info info;
    int saw_root=0,saw_tmp=0;
    for(unsigned i=0;;i++){
        int rc=vfs_mount_info(i,&info);
        if(rc==0)break;
        assert(rc==1);
        if(!strcmp(info.path,"/")){
            assert(info.read_only);
            saw_root=1;
        }
        if(!strcmp(info.path,"/tmp")){
            assert(!info.read_only);
            saw_tmp=1;
        }
    }
    assert(saw_root&&saw_tmp);
}

int main(int argc,char **argv){
    assert(argc==2);
    FILE *image=fopen(argv[1],"rb");assert(image);
    assert(!fseek(image,0,SEEK_END));
    long size=ftell(image);assert(size>0&&size%512==0);

    block_init();
    ramfs_init();
    vfs_init();
    struct block_device device={(uint64_t)size/512U,sector,0,image};
    int disk=block_register(&device);assert(disk>=0);
    assert(vfs_mkdir("/mnt/disk")==0);
    assert(vfs_mount_kilofs("/mnt/disk",disk,0)==0);
    assert(vfs_use_boot_root()==0);
    check_mount_layout();

    struct vfs_info stat;
    assert(vfs_stat("/sbin/init",&stat)==0&&!stat.is_dir&&(stat.mode&0111));
    assert(vfs_stat("/bin/sh",&stat)==0&&!stat.is_dir&&(stat.mode&0111));
    assert(vfs_access("/sbin/init",1)==0&&vfs_access("/bin/sh",1)==0);
    int fd=vfs_open("/sbin/init",VFS_READ,0);assert(fd>=3);
    char magic[4];assert(vfs_read(fd,magic,sizeof magic)==4&&!memcmp(magic,"\177ELF",4));
    assert(vfs_close(fd)==0);
    assert(vfs_open("/bin/sh",VFS_WRITE,0)<0&&vfs_errno()==K_EROFS);

    fd=vfs_open("/tmp/initramfs-check",VFS_READ|VFS_WRITE,1);assert(fd>=3);
    assert(vfs_write(fd,"initramfs",9)==9);
    assert(vfs_seek(fd,0)==0);
    char text[10]={0};assert(vfs_read(fd,text,9)==9&&!memcmp(text,"initramfs",9));
    assert(vfs_close(fd)==0);
    assert(vfs_unlink("/tmp/initramfs-check")==0);
    assert(vfs_access("/mnt/disk/sbin/init",1)==0);

    fclose(image);
    puts("initramfs boot-root /sbin/init + read-only root + writable /tmp PASS");
    return 0;
}

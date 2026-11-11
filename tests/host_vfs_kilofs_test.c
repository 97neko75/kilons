/* Real VFS integration, backing kilofs with the existing bounded block RAM
 * device. This test never addresses a physical disk or ATA I/O port. */
#include <kilons/block.h>
#include <kilons/kilofs.h>
#include <kilons/kilofs_block.h>
#include <kilons/vfs.h>
#include <kilons/ramfs.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kilofs mkfs;
int main(void){
    block_init();ramfs_init();vfs_init();
    struct kfs_block_context ctx;
    struct kfs_device dev;
    assert(kfs_bind_block(&dev,&ctx,0,1)==0);
    assert(kfs_format(&mkfs,dev)==0);
    assert(vfs_mkdir("/mnt/kfs")==0);
    assert(vfs_mount_kilofs("/mnt/kfs",0,1)==0);
    struct vfs_mount_info m;
    assert(vfs_mount_info(1,&m)==1 && !strcmp(m.path,"/mnt/kfs") && !m.read_only);
    assert(vfs_mkdir("/mnt/kfs/dir")==0);
    int fd=vfs_open_mode("/mnt/kfs/dir/a",VFS_READ|VFS_WRITE,1,0600);
    assert(fd>=3);
    assert(vfs_write(fd,"abcdef",6)==6);
    assert(vfs_lseek(fd,-2,2)==4);
    char buf[16]={0};
    assert(vfs_read(fd,buf,2)==2 && !memcmp(buf,"ef",2));
    assert(vfs_fsync(fd)==0);
    assert(vfs_close(fd)==0);
    struct vfs_info stat;
    assert(vfs_stat("/mnt/kfs/dir/a",&stat)==0 && stat.size==6 && stat.mode==0600);
    assert(vfs_rename("/mnt/kfs/dir/a","/mnt/kfs/dir/b")==0);
    assert(vfs_stat("/mnt/kfs/dir/b",&stat)==0 && stat.size==6);
    struct vfs_dirent ent;
    assert(vfs_readdir("/mnt/kfs/dir",0,&ent)==1 && !strcmp(ent.name,"b"));
    assert(vfs_readdir("/mnt/kfs/dir",1,&ent)==0);
    assert(vfs_unlink("/mnt/kfs/dir/b")==0);
    assert(vfs_rmdir("/mnt/kfs/dir")==0);
    assert(vfs_rmdir("/mnt/kfs")==-1); /* mounted root cannot be removed */
    puts("host_vfs_kilofs_test: PASS");return 0;
}

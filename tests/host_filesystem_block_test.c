#include <kilons/block.h>
#include <kilons/vfs.h>
#include <kilons/ramfs.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool readonly_read(void *ctx,uint64_t lba,uint8_t *out) {
    (void)ctx;(void)lba;memset(out,0,BLOCK_SECTOR_SIZE);return true;
}
int main(void) {
    block_init();assert(block_selftest());
    struct block_device ro={4,readonly_read,0,0};
    int ro_id=block_register(&ro);
    uint8_t sector[512]={0};
    assert(ro_id==1 && block_read(ro_id,0,sector));
    assert(!block_write(ro_id,0,sector) && !block_read(ro_id,4,sector));
    ramfs_init();vfs_init();
    assert(ramfs_symlink(NULL,"invalid")==-K_EFAULT);
    assert(vfs_selftest());
    assert(vfs_mkdir("/bin")==0);
    assert(vfs_symlink(NULL,"/bin/invalid")==-1 && vfs_errno()==K_EFAULT);
    int fd=vfs_open("/bin/testing",VFS_READ|VFS_WRITE,1);
    assert(fd>=3);
    assert(vfs_write(fd,"abcdef",6)==6);
    assert(vfs_retain(fd)==0 && vfs_retain(fd)==0);
    assert(vfs_close(fd)==0); /* ref 3 -> 2 */
    assert(vfs_close(fd)==0); /* ref 2 -> 1 */
    assert(vfs_seek(fd,0)==0);
    char buf[8]={0};
    assert(vfs_read(fd,buf,6)==6 && strcmp(buf,"abcdef")==0);
    struct vfs_info info;
    assert(vfs_fstat(fd,&info)==0 && info.size==6 && !info.is_dir);
    assert(vfs_unlink("/bin/testing")==0);
    assert(vfs_fstat(fd,&info)==0 && info.size==6);
    assert(vfs_close(fd)==0 && vfs_fstat(fd,&info)==-1);
    assert(vfs_rmdir("/bin")==0);
    puts("host VFS/RAMFS + refcount + sector/block layer PASS");
    return 0;
}

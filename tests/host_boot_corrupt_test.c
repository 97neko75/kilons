/* A malformed boot module must be rejected before it can become the root
 * mount. The same VFS instance must still accept a valid image afterwards. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kilons/block.h>
#include <kilons/kerrno.h>
#include <kilons/ramfs.h>
#include <kilons/vfs.h>

struct image { uint8_t *data; size_t bytes; };

static bool sector(void *context,uint64_t lba,uint8_t dst[512]){
    const struct image *image=context;
    if(lba>=image->bytes/512U)return false;
    memcpy(dst,image->data+(size_t)lba*512U,512U);
    return true;
}

static int mount_image(struct image *image){
    block_init();
    ramfs_init();
    vfs_init();
    struct block_device device={(uint64_t)image->bytes/512U,sector,0,image};
    int disk=block_register(&device);assert(disk>=0);
    assert(vfs_mkdir("/mnt/disk")==0);
    return vfs_mount_kilofs("/mnt/disk",disk,0);
}

int main(int argc,char **argv){
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));
    long length=ftell(file);assert(length>0&&length%512==0);
    struct image image={(uint8_t *)malloc((size_t)length),(size_t)length};
    assert(image.data);
    assert(!fseek(file,0,SEEK_SET));
    assert(fread(image.data,1,image.bytes,file)==image.bytes);
    fclose(file);

    image.data[0]^=0x01U;
    assert(mount_image(&image)<0&&vfs_errno()==K_EINVAL);
    image.data[0]^=0x01U;
    assert(mount_image(&image)==0);
    assert(vfs_use_boot_root()==0);
    assert(vfs_access("/sbin/init",1)==0);

    free(image.data);
    puts("corrupt boot image rejected, valid image remounted PASS");
    return 0;
}

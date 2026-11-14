#include "internal.h"
/* Bitmap changes and zeroing of a newly allocated data block are not
 * atomic across power loss; fsck is required after an unclean shutdown.
 * The caller holds the filesystem lock throughout each operation. */
static int bit_change(struct kilofs *fs,uint32_t bitmap_block,uint32_t bit,int value){
    int rc=kfs_cache_read(fs,bitmap_block,fs->scratch);if(rc<0)return rc;
    uint8_t mask=(uint8_t)(1U<<(bit%8));
    if(value)fs->scratch[bit/8]|=mask;else fs->scratch[bit/8]&=(uint8_t)~mask;
    return kfs_cache_write(fs,bitmap_block,fs->scratch);
}
int kfs_block_allocate(struct kilofs *fs,uint32_t *out){
    if(!fs||!out||fs->readonly)return -K_EROFS;
    int rc=kfs_cache_read(fs,1,fs->scratch);if(rc<0)return rc;
    uint32_t block=fs->dev.total_blocks;
    for(uint32_t i=fs->data_start;i<fs->dev.total_blocks;i++)
        if(!(fs->scratch[i/8]&(1U<<(i%8)))){block=i;break;}
    if(block>=fs->dev.total_blocks)return -K_ENOSPC;
    rc=bit_change(fs,1,block,1);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);
    rc=kfs_cache_write(fs,block,fs->scratch);
    if(rc<0){(void)bit_change(fs,1,block,0);return rc;}
    *out=block;return 0;
}
int kfs_block_free(struct kilofs *fs,uint32_t block){
    if(!fs||fs->readonly)return -K_EROFS;
    if(block<fs->data_start||block>=fs->dev.total_blocks)return -K_EINVAL;
    int rc=kfs_cache_read(fs,1,fs->scratch);if(rc<0)return rc;
    if(!(fs->scratch[block/8]&(1U<<(block%8))))return -K_EIO;
    fs->scratch[block/8]&=(uint8_t)~(1U<<(block%8));
    return kfs_cache_write(fs,1,fs->scratch);
}
int kfs_inode_allocate(struct kilofs *fs,uint32_t *out){
    if(!fs||!out||fs->readonly)return -K_EROFS;
    int rc=kfs_cache_read(fs,2,fs->scratch);if(rc<0)return rc;
    for(uint32_t i=2;i<KFS_INODES;i++)if(!(fs->scratch[i/8]&(1U<<(i%8)))){
        rc=bit_change(fs,2,i,1);if(rc<0)return rc;
        *out=i;return 0;
    }
    return -K_ENOSPC;
}
int kfs_inode_free(struct kilofs *fs,uint32_t ino){
    if(!fs||fs->readonly)return -K_EROFS;
    if(ino<=KFS_ROOT||ino>=KFS_INODES)return -K_EINVAL;
    int rc=kfs_cache_read(fs,2,fs->scratch);if(rc<0)return rc;
    if(!(fs->scratch[ino/8]&(1U<<(ino%8))))return -K_EIO;
    fs->scratch[ino/8]&=(uint8_t)~(1U<<(ino%8));
    return kfs_cache_write(fs,2,fs->scratch);
}

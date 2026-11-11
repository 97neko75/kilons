#include "internal.h"
/* Exactly 128 bytes on disk per inode. Serialization is explicit and avoids
 * alignment/padding dependence across compilers and architectures. */
int kfs_inode_load(struct kilofs *fs,uint32_t ino,struct kfs_node *out){
    if(!fs||!out||!ino||ino>=KFS_INODES)return -K_EINVAL;
    uint32_t block=3U+ino/32U,offset=(ino%32U)*KFS_INODE_BYTES;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    const uint8_t *p=fs->scratch+offset;
    out->kind=kfs_u16(p);out->mode=kfs_u16(p+2);
    out->uid=kfs_u32(p+4);out->gid=kfs_u32(p+8);
    out->links=kfs_u32(p+12);out->parent=kfs_u32(p+16);
    out->size=kfs_u64(p+24);
    for(unsigned i=0;i<KFS_MAX_FILE_BLOCKS;i++)out->direct[i]=kfs_u32(p+32+i*4);
    if(out->kind!=KFS_KIND_FILE && out->kind!=KFS_KIND_DIR)return -K_ENOENT;
    if(out->size>(uint64_t)KFS_MAX_FILE_BLOCKS*KFS_BLOCK)return -K_EIO;
    /* Malformed media must never redirect file I/O to a metadata block. */
    for(unsigned i=0;i<KFS_MAX_FILE_BLOCKS;i++){
        uint32_t block=out->direct[i];
        if(block && (block<KFS_DATA_START || block>=fs->dev.total_blocks))return -K_EIO;
    }
    return 0;
}
int kfs_inode_save(struct kilofs *fs,uint32_t ino,const struct kfs_node *node){
    if(!fs||!node||fs->readonly||!ino||ino>=KFS_INODES)return -K_EINVAL;
    uint32_t block=3U+ino/32U,offset=(ino%32U)*KFS_INODE_BYTES;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    uint8_t *p=fs->scratch+offset;kfs_zero(p,KFS_INODE_BYTES);
    kfs_p16(p,node->kind);kfs_p16(p+2,node->mode);
    kfs_p32(p+4,node->uid);kfs_p32(p+8,node->gid);
    kfs_p32(p+12,node->links);kfs_p32(p+16,node->parent);
    kfs_p64(p+24,node->size);
    for(unsigned i=0;i<KFS_MAX_FILE_BLOCKS;i++)kfs_p32(p+32+i*4,node->direct[i]);
    return kfs_cache_write(fs,block,fs->scratch);
}
int kfs_drop_inode(struct kilofs *fs,uint32_t ino,struct kfs_node *node){
    if(!fs||!node||ino<=KFS_ROOT)return -K_EINVAL;
    for(unsigned i=0;i<KFS_MAX_FILE_BLOCKS;i++)if(node->direct[i]){
        int rc=kfs_block_free(fs,node->direct[i]);if(rc<0)return rc;
        node->direct[i]=0;
    }
    node->size=0;node->kind=0;node->links=0;
    /* Clear inode slot before freeing its bitmap bit. */
    uint32_t block=3U+ino/32U,offset=(ino%32U)*KFS_INODE_BYTES;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch+offset,KFS_INODE_BYTES);
    rc=kfs_cache_write(fs,block,fs->scratch);if(rc<0)return rc;
    return kfs_inode_free(fs,ino);
}

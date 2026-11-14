#include "internal.h"
/* Exactly 128 bytes on disk per inode. Serialization is explicit and avoids
 * alignment/padding dependence across compilers and architectures. */
static int valid_data_block(const struct kilofs *fs,uint32_t block){
    return block>=fs->data_start&&block<fs->dev.total_blocks;
}
static int validate_pointer_table(struct kilofs *fs,uint32_t block,unsigned used){
    if(!valid_data_block(fs,block)||used>KFS_POINTERS_PER_BLOCK)return -K_EIO;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    for(unsigned i=0;i<KFS_POINTERS_PER_BLOCK;i++){
        uint32_t value=kfs_u32(fs->scratch+i*4U);
        if(value&&!valid_data_block(fs,value))return -K_EIO;
        if(i>=used&&value)return -K_EIO;
    }
    return 0;
}
static int validate_indirect(struct kilofs *fs,const struct kfs_node *node,
                             unsigned used){
    unsigned single_used=used>KFS_DIRECT_BLOCKS?used-KFS_DIRECT_BLOCKS:0;
    if(single_used>KFS_POINTERS_PER_BLOCK)single_used=KFS_POINTERS_PER_BLOCK;
    unsigned double_used=used>KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK?
                         used-KFS_DIRECT_BLOCKS-KFS_POINTERS_PER_BLOCK:0;
    if(single_used==0 && node->indirect)return -K_EIO;
    if(node->indirect){
        int rc=validate_pointer_table(fs,node->indirect,single_used);
        if(rc<0)return rc;
    }
    if(double_used==0 && node->double_indirect)return -K_EIO;
    if(!node->double_indirect)return 0;
    if(!valid_data_block(fs,node->double_indirect))return -K_EIO;
    int rc=kfs_cache_read(fs,node->double_indirect,fs->scratch);if(rc<0)return rc;
    unsigned outer_used=(double_used+KFS_POINTERS_PER_BLOCK-1U)/KFS_POINTERS_PER_BLOCK;
    for(unsigned outer=0;outer<KFS_POINTERS_PER_BLOCK;outer++){
        uint32_t level=kfs_u32(fs->scratch+outer*4U);
        if(level&&!valid_data_block(fs,level))return -K_EIO;
        if(outer>=outer_used){if(level)return -K_EIO;continue;}
        if(level){
            unsigned remaining=double_used-outer*KFS_POINTERS_PER_BLOCK;
            unsigned inner_used=remaining>KFS_POINTERS_PER_BLOCK?
                                 KFS_POINTERS_PER_BLOCK:remaining;
            rc=validate_pointer_table(fs,level,inner_used);
            if(rc<0)return rc;
        }
    }
    return 0;
}
int kfs_inode_load(struct kilofs *fs,uint32_t ino,struct kfs_node *out){
    if(!fs||!out||!ino||ino>=KFS_INODES)return -K_EINVAL;
    uint32_t block=3U+ino/32U,offset=(ino%32U)*KFS_INODE_BYTES;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    const uint8_t *p=fs->scratch+offset;
    out->kind=kfs_u16(p);out->mode=kfs_u16(p+2);
    out->uid=kfs_u32(p+4);out->gid=kfs_u32(p+8);
    out->links=kfs_u32(p+12);out->parent=kfs_u32(p+16);
    out->size=kfs_u64(p+24);
    out->times=(struct file_times){(int64_t)kfs_u64(p+96),(int64_t)kfs_u64(p+104),(int64_t)kfs_u64(p+112)};
    for(unsigned i=0;i<KFS_DIRECT_BLOCKS;i++)out->direct[i]=kfs_u32(p+32+i*4);
    out->indirect=kfs_u32(p+120);out->double_indirect=kfs_u32(p+124);
    if(out->kind!=KFS_KIND_FILE && out->kind!=KFS_KIND_DIR && out->kind!=KFS_KIND_SYMLINK)return -K_ENOENT;
    if(out->parent<KFS_ROOT || out->parent>=KFS_INODES)return -K_EIO;
    if(out->kind==KFS_KIND_SYMLINK && (!out->size || out->size>=256 || !out->direct[0]))return -K_EIO;
    if(out->size>kfs_file_size_limit(fs))return -K_EIO;
    if(out->kind==KFS_KIND_DIR && out->size%KFS_DIRENT_BYTES)return -K_EIO;
    /* Malformed media must never redirect file I/O to a metadata block. */
    for(unsigned i=0;i<KFS_DIRECT_BLOCKS;i++){
        uint32_t block=out->direct[i];
        if(block && (block<fs->data_start || block>=fs->dev.total_blocks))return -K_EIO;
    }
    if((out->indirect&&(out->indirect<fs->data_start||out->indirect>=fs->dev.total_blocks))||
       (out->double_indirect&&(out->double_indirect<fs->data_start||
                               out->double_indirect>=fs->dev.total_blocks)))return -K_EIO;
    /* A direct pointer beyond EOF is never useful in this direct-block
     * format. Reject it here instead of allowing normal file operations to
     * hide an allocation leak until a later fsck. */
    unsigned used=(unsigned)((out->size+KFS_BLOCK-1U)/KFS_BLOCK);
    for(unsigned i=used;i<KFS_DIRECT_BLOCKS;i++)if(out->direct[i])return -K_EIO;
    return validate_indirect(fs,out,used);
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
    kfs_p64(p+96,node->times.access);
    kfs_p64(p+104,node->times.modify);
    kfs_p64(p+112,node->times.change);
    for(unsigned i=0;i<KFS_DIRECT_BLOCKS;i++)kfs_p32(p+32+i*4,node->direct[i]);
    kfs_p32(p+120,node->indirect);kfs_p32(p+124,node->double_indirect);
    return kfs_cache_write(fs,block,fs->scratch);
}
int kfs_drop_inode(struct kilofs *fs,uint32_t ino,struct kfs_node *node){
    if(!fs||!node||ino<=KFS_ROOT)return -K_EINVAL;
    uint32_t used=(uint32_t)((node->size+KFS_BLOCK-1U)/KFS_BLOCK);
    int released=0;
    for(uint32_t i=0;i<used;i++){
        int rc=kfs_data_block_release(fs,node,i);
        if(rc<0){
            /* Keep the on-disk inode consistent with any blocks already
             * released. A failed deletion may leak work, but must not leave
             * the inode pointing at a block whose bitmap bit was cleared. */
            if(released)(void)kfs_inode_save(fs,ino,node);
            return rc;
        }
        released=1;
    }
    /* Empty indirect roots are also pruned when an inode has been reduced to
     * zero bytes. Healthy inodes do not retain them, but this keeps deletion
     * deterministic after a failed truncate. */
    if(!used&&node->indirect){
        int rc=kfs_data_block_release(fs,node,KFS_DIRECT_BLOCKS);if(rc<0)return rc;
    }
    if(!used&&node->double_indirect){
        int rc=kfs_data_block_release(fs,node,KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK);if(rc<0)return rc;
    }
    node->size=0;node->kind=0;node->links=0;
    /* Clear inode slot before freeing its bitmap bit. */
    uint32_t block=3U+ino/32U,offset=(ino%32U)*KFS_INODE_BYTES;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch+offset,KFS_INODE_BYTES);
    rc=kfs_cache_write(fs,block,fs->scratch);if(rc<0)return rc;
    return kfs_inode_free(fs,ino);
}

#include "internal.h"
/* Write-back cache. Every caller must serialize all kilofs operations with
 * the filesystem mutex. No allocation; dirty entries survive write errors. */
static int slot_for(struct kilofs *fs,uint32_t block,unsigned *out){
    if(block>=fs->dev.total_blocks)return -K_EINVAL;
    for(unsigned i=0;i<KFS_CACHE_SLOTS;i++)if(fs->cache[i].valid&&fs->cache[i].block==block){*out=i;return 0;}
    unsigned i=fs->cursor++%KFS_CACHE_SLOTS;
    if(fs->cache[i].valid && fs->cache[i].dirty){
        if(!fs->dev.write)return -K_EROFS;
        int rc=fs->dev.write(fs->dev.context,fs->cache[i].block,fs->cache[i].data);
        if(rc<0)return rc;
        fs->cache[i].dirty=0;
    }
    int rc=fs->dev.read(fs->dev.context,block,fs->cache[i].data);
    if(rc<0){fs->cache[i].valid=0;return rc;}
    fs->cache[i].block=block;fs->cache[i].valid=1;fs->cache[i].dirty=0;
    *out=i;return 0;
}
int kfs_cache_read(struct kilofs *fs,uint32_t block,uint8_t *dst){
    if(!fs||!dst||!fs->dev.read)return -K_EINVAL;
    unsigned i;int rc=slot_for(fs,block,&i);if(rc<0)return rc;
    kfs_copy(dst,fs->cache[i].data,KFS_BLOCK);return 0;
}
int kfs_cache_write(struct kilofs *fs,uint32_t block,const uint8_t *src){
    if(!fs||!src||!fs->dev.write||fs->readonly)return -K_EROFS;
    unsigned i;int rc=slot_for(fs,block,&i);if(rc<0)return rc;
    kfs_copy(fs->cache[i].data,src,KFS_BLOCK);fs->cache[i].dirty=1;return 0;
}
int kfs_cache_flush(struct kilofs *fs){
    if(!fs)return -K_EINVAL;
    for(unsigned i=0;i<KFS_CACHE_SLOTS;i++)if(fs->cache[i].valid&&fs->cache[i].dirty){
        if(!fs->dev.write)return -K_EROFS;
        int rc=fs->dev.write(fs->dev.context,fs->cache[i].block,fs->cache[i].data);
        if(rc<0)return rc;
        fs->cache[i].dirty=0;
    }
    return fs->dev.flush?fs->dev.flush(fs->dev.context): -K_EIO;
}
int kfs_sync(struct kilofs *fs){
    if(!fs||!fs->mounted)return -K_EINVAL;
    return fs->readonly?0:kfs_cache_flush(fs);
}
static void reset(struct kilofs *fs,struct kfs_device dev){
    fs->dev=dev;fs->cursor=0;fs->mounted=0;fs->readonly=0;fs->uid=0;fs->gid=0;
    for(unsigned i=0;i<KFS_CACHE_SLOTS;i++){fs->cache[i].valid=0;fs->cache[i].dirty=0;}
    for(unsigned i=0;i<KFS_HANDLES;i++)fs->handles[i].used=0;
}
static int valid_device(struct kfs_device d,int write){
    if(!d.read||!d.flush||d.total_blocks<KFS_DATA_START+1||d.total_blocks>KFS_MAX_BLOCKS)return -K_EINVAL;
    if(write&&!d.write)return -K_EROFS;
    return 0;
}
int kfs_format(struct kilofs *fs,struct kfs_device dev){
    if(!fs)return -K_EINVAL;
    int rc=valid_device(dev,1);if(rc<0)return rc;
    reset(fs,dev);
    kfs_zero(fs->scratch,KFS_BLOCK);
    kfs_p32(fs->scratch,KFS_MAGIC);kfs_p32(fs->scratch+4,1);
    kfs_p32(fs->scratch+8,KFS_BLOCK);kfs_p32(fs->scratch+12,dev.total_blocks);
    kfs_p32(fs->scratch+16,KFS_INODES);kfs_p32(fs->scratch+20,KFS_DATA_START);
    kfs_p32(fs->scratch+24,KFS_ROOT);
    rc=kfs_cache_write(fs,0,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);
    for(unsigned i=0;i<KFS_DATA_START;i++)fs->scratch[i/8]|=(uint8_t)(1U<<(i%8));
    rc=kfs_cache_write(fs,1,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);fs->scratch[0]=0x03U; /* inode 0 invalid, inode 1=root */
    rc=kfs_cache_write(fs,2,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);
    for(unsigned i=3;i<7;i++){rc=kfs_cache_write(fs,i,fs->scratch);if(rc<0)return rc;}
    struct kfs_node root={0};root.kind=KFS_KIND_DIR;root.mode=0755U;
    root.links=1;root.parent=KFS_ROOT;
    rc=kfs_inode_save(fs,KFS_ROOT,&root);if(rc<0)return rc;
    rc=kfs_cache_flush(fs);if(rc<0)return rc;
    fs->mounted=1;return 0;
}
int kfs_mount(struct kilofs *fs,struct kfs_device dev,int readonly){
    if(!fs)return -K_EINVAL;
    int rc=valid_device(dev,!readonly);if(rc<0)return rc;
    reset(fs,dev);fs->readonly=!!readonly;
    rc=kfs_cache_read(fs,0,fs->scratch);if(rc<0)return rc;
    if(kfs_u32(fs->scratch)!=KFS_MAGIC||kfs_u32(fs->scratch+4)!=1||
       kfs_u32(fs->scratch+8)!=KFS_BLOCK||kfs_u32(fs->scratch+12)!=dev.total_blocks||
       kfs_u32(fs->scratch+16)!=KFS_INODES||kfs_u32(fs->scratch+20)!=KFS_DATA_START||
       kfs_u32(fs->scratch+24)!=KFS_ROOT)return -K_EINVAL;
    struct kfs_node root;
    rc=kfs_inode_load(fs,KFS_ROOT,&root);
    if(rc<0||root.kind!=KFS_KIND_DIR)return -K_EIO;
    fs->mounted=1;return 0;
}

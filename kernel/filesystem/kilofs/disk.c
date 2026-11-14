#include "internal.h"

/* Write-back cache and a small redo journal. Every caller must serialize all
 * kilofs operations with the filesystem mutex. The journal is deliberately
 * block based: a sync either leaves the old home blocks intact, or leaves a
 * committed record that mount can replay after a reset. */
static int slot_for(struct kilofs *fs,uint32_t block,unsigned *out){
    if(block>=fs->dev.total_blocks)return -K_EINVAL;
    for(unsigned i=0;i<KFS_CACHE_SLOTS;i++)
        if(fs->cache[i].valid&&fs->cache[i].block==block){*out=i;return 0;}
    unsigned i=KFS_CACHE_SLOTS;
    for(unsigned j=0;j<KFS_CACHE_SLOTS;j++)if(!fs->cache[j].valid){i=j;break;}
    if(i==KFS_CACHE_SLOTS)i=fs->cursor++%KFS_CACHE_SLOTS;
    if(fs->cache[i].valid&&fs->cache[i].dirty){
        int rc=kfs_cache_flush(fs);
        if(rc<0)return rc;
    }
    int rc=fs->dev.read(fs->dev.context,block,fs->cache[i].data);
    if(rc<0){fs->cache[i].valid=0;return rc;}
    fs->cache[i].block=block;fs->cache[i].valid=1;fs->cache[i].dirty=0;
    *out=i;return 0;
}
static uint32_t journal_checksum(const uint8_t *data){
    uint32_t hash=2166136261U;
    for(unsigned i=0;i<KFS_BLOCK;i++){
        hash^=data[i];
        hash*=16777619U;
    }
    return hash;
}
static uint32_t header_checksum(const uint8_t *header){
    uint32_t hash=2166136261U;
    unsigned count=kfs_u32(header+16);
    for(unsigned i=0;i<20;i++){
        hash^=header[i];
        hash*=16777619U;
    }
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<4;j++){
        hash^=header[32+i*4+j];
        hash*=16777619U;
    }
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<4;j++){
        hash^=header[160+i*4+j];
        hash*=16777619U;
    }
    return hash;
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

static int journal_header(struct kilofs *fs,unsigned state,
                          const unsigned *slots,unsigned count){
    kfs_zero(fs->scratch,KFS_BLOCK);
    kfs_p32(fs->scratch,KFS_JOURNAL_MAGIC);
    kfs_p32(fs->scratch+4,KFS_JOURNAL_VERSION);
    kfs_p32(fs->scratch+8,state);
    kfs_p32(fs->scratch+12,++fs->journal_sequence);
    kfs_p32(fs->scratch+16,count);
    for(unsigned i=0;i<count;i++){
        kfs_p32(fs->scratch+32+i*4,fs->cache[slots[i]].block);
        kfs_p32(fs->scratch+160+i*4,journal_checksum(fs->cache[slots[i]].data));
    }
    kfs_p32(fs->scratch+20,header_checksum(fs->scratch));
    return fs->dev.write(fs->dev.context,fs->journal_start,fs->scratch);
}
static int clear_journal(struct kilofs *fs){
    kfs_zero(fs->scratch,KFS_BLOCK);
    int rc=fs->dev.write(fs->dev.context,fs->journal_start,fs->scratch);
    if(rc<0)return rc;
    return fs->dev.flush(fs->dev.context);
}

/* Replay a committed transaction. Prepared records are intentionally
 * discarded: home blocks were not made visible before the commit marker. */
int kfs_journal_recover(struct kilofs *fs){
    if(!fs||!fs->dev.read||!fs->dev.flush)return -K_EINVAL;
    int rc=fs->dev.read(fs->dev.context,fs->journal_start,fs->scratch);
    if(rc<0)return rc;
    if(kfs_u32(fs->scratch)!=KFS_JOURNAL_MAGIC)return 0;
    if(kfs_u32(fs->scratch+4)!=KFS_JOURNAL_VERSION)return -K_EIO;
    unsigned state=kfs_u32(fs->scratch+8),count=kfs_u32(fs->scratch+16);
    uint32_t checksums[KFS_JOURNAL_MAX_RECORDS];
    if(count>KFS_JOURNAL_MAX_RECORDS)return -K_EIO;
    if(kfs_u32(fs->scratch+20)!=header_checksum(fs->scratch))return -K_EIO;
    for(unsigned i=0;i<count;i++){
        uint32_t target=kfs_u32(fs->scratch+32+i*4);
        if(target>=fs->dev.total_blocks||
           (target>=fs->journal_start&&target<fs->journal_start+fs->journal_blocks))return -K_EIO;
        for(unsigned j=0;j<i;j++)if(fs->journal_targets[j]==target)return -K_EIO;
        fs->journal_targets[i]=target;
        checksums[i]=kfs_u32(fs->scratch+160+i*4);
    }
    if(state!=KFS_JOURNAL_PREPARED&&state!=KFS_JOURNAL_COMMITTED)return -K_EIO;
    if(state==KFS_JOURNAL_PREPARED){
        if(fs->readonly)return 0;
        return clear_journal(fs);
    }
    if(fs->readonly)return -K_EROFS;
    for(unsigned i=0;i<count;i++){
        rc=fs->dev.read(fs->dev.context,fs->journal_start+1+i,fs->scratch);
        if(rc<0)return rc;
        if(journal_checksum(fs->scratch)!=checksums[i])return -K_EIO;
        rc=fs->dev.write(fs->dev.context,fs->journal_targets[i],fs->scratch);
        if(rc<0)return rc;
    }
    rc=fs->dev.flush(fs->dev.context);if(rc<0)return rc;
    return clear_journal(fs);
}

static int journal_commit(struct kilofs *fs,const unsigned *slots,unsigned count){
    if(!count)return fs->dev.flush(fs->dev.context);
    if(count>KFS_JOURNAL_MAX_RECORDS)return -K_ENOSPC;
    for(unsigned i=0;i<count;i++){
        fs->journal_targets[i]=fs->cache[slots[i]].block;
        int rc=fs->dev.write(fs->dev.context,fs->journal_start+1+i,
                             fs->cache[slots[i]].data);
        if(rc<0)return rc;
    }
    int rc=journal_header(fs,KFS_JOURNAL_PREPARED,slots,count);if(rc<0)return rc;
    rc=fs->dev.flush(fs->dev.context);if(rc<0)return rc;
    rc=journal_header(fs,KFS_JOURNAL_COMMITTED,slots,count);if(rc<0)return rc;
    rc=fs->dev.flush(fs->dev.context);if(rc<0)return rc;
    for(unsigned i=0;i<count;i++){
        rc=fs->dev.write(fs->dev.context,fs->journal_targets[i],fs->cache[slots[i]].data);
        if(rc<0)return rc;
    }
    rc=fs->dev.flush(fs->dev.context);if(rc<0)return rc;
    for(unsigned i=0;i<count;i++)fs->cache[slots[i]].dirty=0;
    return clear_journal(fs);
}

int kfs_cache_flush(struct kilofs *fs){
    if(!fs||!fs->dev.write||!fs->dev.flush)return -K_EINVAL;
    int rc=kfs_journal_recover(fs);if(rc<0)return rc;
    unsigned slots[KFS_JOURNAL_MAX_RECORDS],count=0;
    for(unsigned i=0;i<KFS_CACHE_SLOTS;i++)
        if(fs->cache[i].valid&&fs->cache[i].dirty){
            if(count>=KFS_JOURNAL_MAX_RECORDS)return -K_ENOSPC;
            slots[count++]=i;
        }
    return journal_commit(fs,slots,count);
}
int kfs_sync(struct kilofs *fs){
    if(!fs||!fs->mounted)return -K_EINVAL;
    return fs->readonly?0:kfs_cache_flush(fs);
}
static void reset(struct kilofs *fs,struct kfs_device dev){
    fs->dev=dev;fs->cursor=0;fs->journal_sequence=0;fs->mounted=0;
    fs->readonly=0;fs->uid=0;fs->gid=0;
    fs->journal_start=KFS_JOURNAL_START;fs->journal_blocks=KFS_JOURNAL_BLOCKS;
    fs->data_start=KFS_DATA_START;
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
    /* A new image must not begin with a stale transaction marker. */
    kfs_zero(fs->scratch,KFS_BLOCK);
    for(unsigned i=0;i<KFS_JOURNAL_BLOCKS;i++){
        rc=dev.write(dev.context,KFS_JOURNAL_START+i,fs->scratch);
        if(rc<0)return rc;
    }
    rc=dev.flush(dev.context);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);
    kfs_p32(fs->scratch,KFS_MAGIC);kfs_p32(fs->scratch+4,KFS_FORMAT_VERSION);
    kfs_p32(fs->scratch+8,KFS_BLOCK);kfs_p32(fs->scratch+12,dev.total_blocks);
    kfs_p32(fs->scratch+16,KFS_INODES);kfs_p32(fs->scratch+20,KFS_DATA_START);
    kfs_p32(fs->scratch+24,KFS_ROOT);kfs_p32(fs->scratch+28,KFS_JOURNAL_START);
    kfs_p32(fs->scratch+32,KFS_JOURNAL_BLOCKS);
    rc=kfs_cache_write(fs,0,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);
    for(unsigned i=0;i<KFS_DATA_START;i++)fs->scratch[i/8]|=(uint8_t)(1U<<(i%8));
    rc=kfs_cache_write(fs,1,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);fs->scratch[0]=0x03U; /* inode 0 invalid, inode 1=root */
    rc=kfs_cache_write(fs,2,fs->scratch);if(rc<0)return rc;
    kfs_zero(fs->scratch,KFS_BLOCK);
    for(unsigned i=3;i<7;i++){rc=kfs_cache_write(fs,i,fs->scratch);if(rc<0)return rc;}
    struct kfs_node root={0};root.kind=KFS_KIND_DIR;root.mode=0755U;
    root.times=file_created();root.links=1;root.parent=KFS_ROOT;
    rc=kfs_inode_save(fs,KFS_ROOT,&root);if(rc<0)return rc;
    rc=kfs_cache_flush(fs);if(rc<0)return rc;
    fs->mounted=1;return 0;
}
int kfs_mount(struct kilofs *fs,struct kfs_device dev,int readonly){
    if(!fs)return -K_EINVAL;
    int rc=valid_device(dev,!readonly);if(rc<0)return rc;
    reset(fs,dev);fs->readonly=!!readonly;
    rc=kfs_journal_recover(fs);if(rc<0)return rc;
    rc=kfs_cache_read(fs,0,fs->scratch);if(rc<0)return rc;
    unsigned version=kfs_u32(fs->scratch+4);
    unsigned journal_start=kfs_u32(fs->scratch+28);
    unsigned journal_blocks=kfs_u32(fs->scratch+32);
    if(kfs_u32(fs->scratch)!=KFS_MAGIC||version!=KFS_FORMAT_VERSION||
       kfs_u32(fs->scratch+8)!=KFS_BLOCK||kfs_u32(fs->scratch+12)!=dev.total_blocks||
       kfs_u32(fs->scratch+16)!=KFS_INODES||kfs_u32(fs->scratch+20)!=KFS_DATA_START||
       kfs_u32(fs->scratch+24)!=KFS_ROOT||journal_start!=KFS_JOURNAL_START||
       journal_blocks!=KFS_JOURNAL_BLOCKS)return -K_EINVAL;
    fs->journal_start=journal_start;fs->journal_blocks=journal_blocks;
    fs->data_start=kfs_u32(fs->scratch+20);
    struct kfs_node root;
    rc=kfs_inode_load(fs,KFS_ROOT,&root);
    if(rc<0||root.kind!=KFS_KIND_DIR)return -K_EIO;
    fs->mounted=1;return 0;
}

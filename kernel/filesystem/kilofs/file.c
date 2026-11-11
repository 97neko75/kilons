#include "internal.h"
#define MAX_SIZE ((uint64_t)KFS_BLOCK*KFS_MAX_FILE_BLOCKS)
/* Direct-block file backend: partial reads/writes report actual bytes.
 * Metadata is stored separately from file data and synced via kfs_sync(). */
int kfs_read_bytes(struct kilofs *fs,struct kfs_node *node,uint64_t pos,void *buf,size_t count){
    if(!fs||!node||(!buf&&count))return -K_EFAULT;
    if(pos>=node->size)return 0;
    if((uint64_t)count>node->size-pos)count=(size_t)(node->size-pos);
    if(count>0x7fffffffU)count=0x7fffffffU;
    size_t done=0;uint8_t *out=buf;
    while(done<count){
        uint32_t index=(uint32_t)(pos/KFS_BLOCK),within=(uint32_t)(pos%KFS_BLOCK);
        if(index>=KFS_MAX_FILE_BLOCKS)return done?(int)done:-K_EIO;
        size_t n=KFS_BLOCK-within;if(n>count-done)n=count-done;
        int rc=0;
        if(!node->direct[index])kfs_zero(fs->scratch,KFS_BLOCK);
        else rc=kfs_cache_read(fs,node->direct[index],fs->scratch);
        if(rc<0)return done?(int)done:rc;
        kfs_copy(out+done,fs->scratch+within,n);
        pos+=n;done+=n;
    }
    return (int)done;
}
int kfs_write_bytes(struct kilofs *fs,struct kfs_node *node,uint64_t pos,const void *buf,size_t count){
    if(!fs||!node||(!buf&&count))return -K_EFAULT;
    if(fs->readonly)return -K_EROFS;
    if(pos>=MAX_SIZE)return count?-K_ENOSPC:0;
    if((uint64_t)count>MAX_SIZE-pos)count=(size_t)(MAX_SIZE-pos);
    if(count>0x7fffffffU)count=0x7fffffffU;
    const uint8_t *src=buf;size_t done=0;
    while(done<count){
        uint32_t index=(uint32_t)(pos/KFS_BLOCK),within=(uint32_t)(pos%KFS_BLOCK);
        size_t n=KFS_BLOCK-within;if(n>count-done)n=count-done;
        if(!node->direct[index]){
            int rc=kfs_block_allocate(fs,&node->direct[index]);
            if(rc<0)return done?(int)done:rc;
        }
        int rc=kfs_cache_read(fs,node->direct[index],fs->scratch);
        if(rc<0)return done?(int)done:rc;
        kfs_copy(fs->scratch+within,src+done,n);
        rc=kfs_cache_write(fs,node->direct[index],fs->scratch);
        if(rc<0)return done?(int)done:rc;
        pos+=n;done+=n;
        if(pos>node->size)node->size=pos;
    }
    return (int)done;
}
int kfs_resize(struct kilofs *fs,uint32_t ino,struct kfs_node *node,uint64_t size){
    if(!fs||!node||fs->readonly)return -K_EROFS;
    if(size>MAX_SIZE)return -K_ENOSPC;
    uint64_t old=node->size;
    if(size<old){
        unsigned keep=(unsigned)((size+KFS_BLOCK-1)/KFS_BLOCK);
        /* On contraction, ensure bytes beyond new EOF cannot reappear. */
        if(size%KFS_BLOCK && node->direct[size/KFS_BLOCK]){
            int rc=kfs_cache_read(fs,node->direct[size/KFS_BLOCK],fs->scratch);if(rc<0)return rc;
            for(unsigned j=(unsigned)(size%KFS_BLOCK);j<KFS_BLOCK;j++)fs->scratch[j]=0;
            rc=kfs_cache_write(fs,node->direct[size/KFS_BLOCK],fs->scratch);if(rc<0)return rc;
        }
        for(unsigned i=keep;i<KFS_MAX_FILE_BLOCKS;i++)if(node->direct[i]){
            int rc=kfs_block_free(fs,node->direct[i]);if(rc<0)return rc;node->direct[i]=0;
        }
    } else if(size>old && old%KFS_BLOCK && node->direct[old/KFS_BLOCK]){
        /* Zero the extension already covered by the old last block. */
        unsigned index=(unsigned)(old/KFS_BLOCK),begin=(unsigned)(old%KFS_BLOCK);
        unsigned end=size/KFS_BLOCK>index?KFS_BLOCK:(unsigned)(size%KFS_BLOCK);
        int rc=kfs_cache_read(fs,node->direct[index],fs->scratch);if(rc<0)return rc;
        for(unsigned j=begin;j<end;j++)fs->scratch[j]=0;
        rc=kfs_cache_write(fs,node->direct[index],fs->scratch);if(rc<0)return rc;
    }
    node->size=size;
    return kfs_inode_save(fs,ino,node);
}

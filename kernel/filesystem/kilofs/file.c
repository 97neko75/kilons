#include "internal.h"

/* KILOFS uses the classic small-file layout: direct data blocks followed by
 * one single-indirect and one double-indirect tree. The pointer blocks are
 * ordinary KILOFS blocks and are covered by the allocation bitmap and
 * journal. This is an original layout, not an ext2 or Minix compatibility
 * implementation. */
static int valid_data_block(const struct kilofs *fs,uint32_t block){
    return block>=fs->data_start&&block<fs->dev.total_blocks;
}
static int pointer_read(struct kilofs *fs,uint32_t block,unsigned index,uint32_t *out){
    if(!valid_data_block(fs,block)||index>=KFS_POINTERS_PER_BLOCK)return -K_EIO;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    *out=kfs_u32(fs->scratch+index*4U);
    if(*out&&!valid_data_block(fs,*out))return -K_EIO;
    return 0;
}
static int pointer_write(struct kilofs *fs,uint32_t block,unsigned index,uint32_t value){
    if(!valid_data_block(fs,block)||index>=KFS_POINTERS_PER_BLOCK||
       (value&&!valid_data_block(fs,value)))return -K_EIO;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    kfs_p32(fs->scratch+index*4U,value);
    return kfs_cache_write(fs,block,fs->scratch);
}
static int pointer_table_empty(struct kilofs *fs,uint32_t block){
    if(!valid_data_block(fs,block))return -K_EIO;
    int rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
    for(unsigned i=0;i<KFS_POINTERS_PER_BLOCK;i++){
        uint32_t value=kfs_u32(fs->scratch+i*4U);
        if(value&&!valid_data_block(fs,value))return -K_EIO;
        if(value)return 0;
    }
    return 1;
}
static void discard_single_root(struct kilofs *fs,struct kfs_node *node,
                                uint32_t root,int owned){
    if(!owned)return;
    node->indirect=0;
    (void)kfs_block_free(fs,root);
}
static void discard_double_roots(struct kilofs *fs,struct kfs_node *node,
                                 uint32_t root,unsigned level_index,
                                 uint32_t level,int own_root,int own_level){
    if(own_level){
        if(pointer_write(fs,root,level_index,0)==0)(void)kfs_block_free(fs,level);
    }
    if(own_root){
        node->double_indirect=0;
        (void)kfs_block_free(fs,root);
    }
}

/* Resolve a logical file block. With allocate==0, a zero result is a sparse
 * hole. With allocate!=0, all missing pointer and data blocks are allocated
 * and zero-initialized. */
int kfs_data_block_get(struct kilofs *fs,struct kfs_node *node,uint32_t index,
                       int allocate,uint32_t *out){
    if(!fs||!node||!out)return -K_EFAULT;
    *out=0;
    if(index>=kfs_file_block_limit(fs))return allocate?-K_ENOSPC:-K_EIO;
    if(index<KFS_DIRECT_BLOCKS){
        uint32_t block=node->direct[index];
        if(block&&!valid_data_block(fs,block))return -K_EIO;
        if(!block&&allocate){
            int rc=kfs_block_allocate(fs,&block);if(rc<0)return rc;
            node->direct[index]=block;
        }
        *out=block;return 0;
    }

    index-=KFS_DIRECT_BLOCKS;
    if(index<KFS_POINTERS_PER_BLOCK){
        unsigned slot=index;uint32_t root=node->indirect;int own_root=0;
        if(!root){
            if(!allocate)return 0;
            int rc=kfs_block_allocate(fs,&root);if(rc<0)return rc;
            node->indirect=root;own_root=1;
        }
        uint32_t block;int rc=pointer_read(fs,root,slot,&block);
        if(rc<0){discard_single_root(fs,node,root,own_root);return rc;}
        if(!block&&allocate){
            rc=kfs_block_allocate(fs,&block);
            if(rc<0){discard_single_root(fs,node,root,own_root);return rc;}
            rc=pointer_write(fs,root,slot,block);
            if(rc<0){(void)kfs_block_free(fs,block);
                discard_single_root(fs,node,root,own_root);return rc;}
        }
        *out=block;return 0;
    }

    index-=KFS_POINTERS_PER_BLOCK;
    if(index>=KFS_POINTERS_PER_BLOCK*KFS_POINTERS_PER_BLOCK)return
        allocate?-K_ENOSPC:-K_EIO;
    unsigned outer=index/KFS_POINTERS_PER_BLOCK;
    unsigned inner=index%KFS_POINTERS_PER_BLOCK;
    uint32_t root=node->double_indirect,level=0;int own_root=0,own_level=0;
    if(!root){
        if(!allocate)return 0;
        int rc=kfs_block_allocate(fs,&root);if(rc<0)return rc;
        node->double_indirect=root;own_root=1;
    }
    int rc=pointer_read(fs,root,outer,&level);
    if(rc<0){discard_double_roots(fs,node,root,outer,level,own_root,own_level);return rc;}
    if(!level){
        if(!allocate)return 0;
        rc=kfs_block_allocate(fs,&level);
        if(rc<0){discard_double_roots(fs,node,root,outer,level,own_root,own_level);return rc;}
        own_level=1;
        rc=pointer_write(fs,root,outer,level);
        if(rc<0){(void)kfs_block_free(fs,level);
            discard_double_roots(fs,node,root,outer,level,own_root,0);return rc;}
    }
    uint32_t block;rc=pointer_read(fs,level,inner,&block);
    if(rc<0){discard_double_roots(fs,node,root,outer,level,own_root,own_level);return rc;}
    if(!block&&allocate){
        rc=kfs_block_allocate(fs,&block);
        if(rc<0){discard_double_roots(fs,node,root,outer,level,own_root,own_level);return rc;}
        rc=pointer_write(fs,level,inner,block);
        if(rc<0){(void)kfs_block_free(fs,block);
            discard_double_roots(fs,node,root,outer,level,own_root,own_level);return rc;}
    }
    *out=block;return 0;
}

static int release_single(struct kilofs *fs,struct kfs_node *node,unsigned slot){
    uint32_t root=node->indirect;if(!root)return 0;
    uint32_t block;int rc=pointer_read(fs,root,slot,&block);if(rc<0)return rc;
    if(block){
        rc=pointer_write(fs,root,slot,0);if(rc<0)return rc;
        rc=kfs_block_free(fs,block);
        if(rc<0){(void)pointer_write(fs,root,slot,block);return rc;}
    }
    int empty=pointer_table_empty(fs,root);if(empty<0)return empty;
    if(empty){
        node->indirect=0;
        rc=kfs_block_free(fs,root);if(rc<0){node->indirect=root;return rc;}
    }
    return 0;
}

/* Release one logical data block and prune empty indirect tables. */
int kfs_data_block_release(struct kilofs *fs,struct kfs_node *node,uint32_t index){
    if(!fs||!node||index>=kfs_file_block_limit(fs))return -K_EINVAL;
    if(index<KFS_DIRECT_BLOCKS){
        uint32_t block=node->direct[index];
        if(!block)return 0;
        int rc=kfs_block_free(fs,block);if(rc<0)return rc;
        node->direct[index]=0;return 0;
    }
    index-=KFS_DIRECT_BLOCKS;
    if(index<KFS_POINTERS_PER_BLOCK)return release_single(fs,node,index);
    index-=KFS_POINTERS_PER_BLOCK;
    if(index>=KFS_POINTERS_PER_BLOCK*KFS_POINTERS_PER_BLOCK)return -K_EINVAL;
    unsigned outer=index/KFS_POINTERS_PER_BLOCK,inner=index%KFS_POINTERS_PER_BLOCK;
    uint32_t root=node->double_indirect;if(!root)return 0;
    uint32_t level;int rc=pointer_read(fs,root,outer,&level);if(rc<0)return rc;
    if(level){
        uint32_t block;rc=pointer_read(fs,level,inner,&block);if(rc<0)return rc;
        if(block){
            rc=pointer_write(fs,level,inner,0);if(rc<0)return rc;
            rc=kfs_block_free(fs,block);
            if(rc<0){(void)pointer_write(fs,level,inner,block);return rc;}
        }
        int empty=pointer_table_empty(fs,level);if(empty<0)return empty;
        if(empty){
            rc=pointer_write(fs,root,outer,0);if(rc<0)return rc;
            rc=kfs_block_free(fs,level);
            if(rc<0){(void)pointer_write(fs,root,outer,level);return rc;}
        }
    }
    int empty=pointer_table_empty(fs,root);if(empty<0)return empty;
    if(empty){
        node->double_indirect=0;
        rc=kfs_block_free(fs,root);if(rc<0){node->double_indirect=root;return rc;}
    }
    return 0;
}

/* Count data and pointer blocks for POSIX st_blocks-style reporting. */
int kfs_node_block_count(struct kilofs *fs,const struct kfs_node *node,unsigned *out){
    if(!fs||!node||!out)return -K_EFAULT;
    unsigned count=0;uint32_t block;
    for(unsigned i=0;i<KFS_DIRECT_BLOCKS;i++)if(node->direct[i]){
        if(!valid_data_block(fs,node->direct[i]))return -K_EIO;
        count++;
    }
    if(node->indirect){
        if(!valid_data_block(fs,node->indirect))return -K_EIO;
        count++;
        for(unsigned i=0;i<KFS_POINTERS_PER_BLOCK;i++){
            int rc=pointer_read(fs,node->indirect,i,&block);if(rc<0)return rc;
            if(block)count++;
        }
    }
    if(node->double_indirect){
        if(!valid_data_block(fs,node->double_indirect))return -K_EIO;
        count++;
        for(unsigned outer=0;outer<KFS_POINTERS_PER_BLOCK;outer++){
            uint32_t level;int rc=pointer_read(fs,node->double_indirect,outer,&level);if(rc<0)return rc;
            if(!level)continue;
            count++;
            for(unsigned inner=0;inner<KFS_POINTERS_PER_BLOCK;inner++){
                rc=pointer_read(fs,level,inner,&block);if(rc<0)return rc;
                if(block)count++;
            }
        }
    }
    *out=count*(KFS_BLOCK/512U);return 0;
}

int kfs_read_bytes(struct kilofs *fs,struct kfs_node *node,uint64_t pos,
                   void *buf,size_t count){
    if(!fs||!node||(!buf&&count))return -K_EFAULT;
    if(pos>=node->size)return 0;
    if((uint64_t)count>node->size-pos)count=(size_t)(node->size-pos);
    if(count>0x7fffffffU)count=0x7fffffffU;
    size_t done=0;uint8_t *out=buf;
    while(done<count){
        uint32_t index=(uint32_t)(pos/KFS_BLOCK),within=(uint32_t)(pos%KFS_BLOCK);
        uint32_t block;int rc=kfs_data_block_get(fs,node,index,0,&block);
        if(rc<0)return done?(int)done:rc;
        size_t n=KFS_BLOCK-within;if(n>count-done)n=count-done;
        if(!block)kfs_zero(fs->scratch,KFS_BLOCK);
        else {rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return done?(int)done:rc;}
        kfs_copy(out+done,fs->scratch+within,n);
        pos+=n;done+=n;
    }
    return (int)done;
}

int kfs_write_bytes(struct kilofs *fs,struct kfs_node *node,uint64_t pos,
                    const void *buf,size_t count){
    if(!fs||!node||(!buf&&count))return -K_EFAULT;
    if(fs->readonly)return -K_EROFS;
    uint64_t max_size=kfs_file_size_limit(fs);
    if(pos>=max_size)return count?-K_ENOSPC:0;
    if((uint64_t)count>max_size-pos)count=(size_t)(max_size-pos);
    if(count>0x7fffffffU)count=0x7fffffffU;
    const uint8_t *src=buf;size_t done=0;
    while(done<count){
        uint32_t index=(uint32_t)(pos/KFS_BLOCK),within=(uint32_t)(pos%KFS_BLOCK);
        struct kfs_node before=*node;
        uint32_t previous=0;
        int rc=kfs_data_block_get(fs,&before,index,0,&previous);
        if(rc<0)return done?(int)done:rc;
        uint32_t block;rc=kfs_data_block_get(fs,node,index,1,&block);
        if(rc<0)return done?(int)done:rc;
        size_t n=KFS_BLOCK-within;if(n>count-done)n=count-done;
        rc=kfs_cache_read(fs,block,fs->scratch);
        if(rc<0){
            if(!previous)(void)kfs_data_block_release(fs,node,index);
            return done?(int)done:rc;
        }
        kfs_copy(fs->scratch+within,src+done,n);
        rc=kfs_cache_write(fs,block,fs->scratch);
        if(rc<0){
            if(!previous)(void)kfs_data_block_release(fs,node,index);
            return done?(int)done:rc;
        }
        pos+=n;done+=n;
        if(pos>node->size)node->size=pos;
    }
    return (int)done;
}

int kfs_resize(struct kilofs *fs,uint32_t ino,struct kfs_node *node,uint64_t size){
    if(!fs||!node||fs->readonly)return -K_EROFS;
    if(size>kfs_file_size_limit(fs))return -K_ENOSPC;
    uint64_t old=node->size;
    if(size<old){
        uint32_t keep=(uint32_t)((size+KFS_BLOCK-1U)/KFS_BLOCK);
        if(size%KFS_BLOCK){
            uint32_t block;int rc=kfs_data_block_get(fs,node,(uint32_t)(size/KFS_BLOCK),0,&block);
            if(rc<0)return rc;
            if(block){
                rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
                for(unsigned j=(unsigned)(size%KFS_BLOCK);j<KFS_BLOCK;j++)fs->scratch[j]=0;
                rc=kfs_cache_write(fs,block,fs->scratch);if(rc<0)return rc;
            }
        }
        uint32_t old_used=(uint32_t)((old+KFS_BLOCK-1U)/KFS_BLOCK);
        for(uint32_t i=keep;i<old_used;i++){
            int rc=kfs_data_block_release(fs,node,i);
            if(rc<0){
                /* Preserve a coherent inode if an earlier release succeeded.
                 * The logical size stays old; released tail blocks become
                 * sparse holes until a later retry completes the truncate. */
                node->size=old;
                (void)kfs_inode_save(fs,ino,node);
                return rc;
            }
        }
    } else if(size>old && old%KFS_BLOCK){
        uint32_t block;int rc=kfs_data_block_get(fs,node,(uint32_t)(old/KFS_BLOCK),0,&block);
        if(rc<0)return rc;
        if(block){
            unsigned begin=(unsigned)(old%KFS_BLOCK);
            unsigned end=size/KFS_BLOCK>(old/KFS_BLOCK)?KFS_BLOCK:(unsigned)(size%KFS_BLOCK);
            rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
            for(unsigned j=begin;j<end;j++)fs->scratch[j]=0;
            rc=kfs_cache_write(fs,block,fs->scratch);if(rc<0)return rc;
        }
    }
    if(fs->uid)node->mode&=~06000U;
    node->times.modify=node->times.change=file_now();
    node->size=size;
    return kfs_inode_save(fs,ino,node);
}

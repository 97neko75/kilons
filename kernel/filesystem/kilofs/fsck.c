#include "internal.h"

static int valid_data_block(const struct kilofs *fs,uint32_t block){
    return block>=fs->data_start&&block<fs->dev.total_blocks;
}
static int mark_block(const struct kilofs *fs,uint8_t *expected,uint32_t block){
    if(!valid_data_block(fs,block))return -K_EIO;
    if(expected[block/8]&(uint8_t)(1U<<(block%8)))return -K_EIO;
    expected[block/8]|=(uint8_t)(1U<<(block%8));return 0;
}
static int read_pointer_table(struct kilofs *fs,uint32_t block){
    if(!valid_data_block(fs,block))return -K_EIO;
    return kfs_cache_read(fs,block,fs->scratch);
}

/* Mark every block reachable from an inode, including the pointer blocks.
 * The logical EOF check is important: an indirect entry beyond EOF is an
 * allocation leak even when the data itself looks otherwise valid. */
static int mark_node_blocks(struct kilofs *fs,const struct kfs_node *node,
                            uint8_t *expected){
    uint32_t used=(uint32_t)((node->size+KFS_BLOCK-1U)/KFS_BLOCK);
    if(used>kfs_file_block_limit(fs))return -K_EIO;
    for(unsigned i=0;i<KFS_DIRECT_BLOCKS;i++)if(node->direct[i]){
        if(i>=used)return -K_EIO;
        int rc=mark_block(fs,expected,node->direct[i]);if(rc<0)return rc;
    }
    if(node->indirect){
        int rc=mark_block(fs,expected,node->indirect);if(rc<0)return rc;
        rc=read_pointer_table(fs,node->indirect);if(rc<0)return rc;
        unsigned present=0;
        for(unsigned i=0;i<KFS_POINTERS_PER_BLOCK;i++){
            uint32_t block=kfs_u32(fs->scratch+i*4U);
            if(!block)continue;
            present=1;
            if(KFS_DIRECT_BLOCKS+i>=used)return -K_EIO;
            rc=mark_block(fs,expected,block);if(rc<0)return rc;
        }
        if(!present)return -K_EIO;
    }
    if(node->double_indirect){
        int rc=mark_block(fs,expected,node->double_indirect);if(rc<0)return rc;
        unsigned root_present=0;
        for(unsigned outer=0;outer<KFS_POINTERS_PER_BLOCK;outer++){
            rc=read_pointer_table(fs,node->double_indirect);if(rc<0)return rc;
            uint32_t level=kfs_u32(fs->scratch+outer*4U);
            if(!level)continue;
            root_present=1;
            uint32_t base=KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK+
                          outer*KFS_POINTERS_PER_BLOCK;
            if(base>=used)return -K_EIO;
            rc=mark_block(fs,expected,level);if(rc<0)return rc;
            rc=read_pointer_table(fs,level);if(rc<0)return rc;
            unsigned level_present=0;
            for(unsigned inner=0;inner<KFS_POINTERS_PER_BLOCK;inner++){
                uint32_t block=kfs_u32(fs->scratch+inner*4U);
                if(!block)continue;
                level_present=1;
                if(base+inner>=used)return -K_EIO;
                rc=mark_block(fs,expected,block);if(rc<0)return rc;
            }
            if(!level_present)return -K_EIO;
        }
        if(!root_present)return -K_EIO;
    }
    return 0;
}

/* Diagnostic checker. Mount replays committed journal records before this is
 * called, so a cleanly recovered image is checked in its post-transaction
 * state. This entry point is read-only; repair is deliberately narrower. */
int kfs_fsck(struct kilofs *fs,unsigned *errors){
    if(!fs||!errors||!fs->mounted)return -K_EINVAL;
    unsigned bad=0;uint8_t *blocks=fs->audit_blocks,*inodes=fs->audit_inodes;
    uint8_t *seen=fs->audit_seen;
    uint32_t refs[KFS_INODES]={0},links[KFS_INODES]={0};
    uint32_t directory_parent[KFS_INODES]={0},inode_parent[KFS_INODES]={0};
    uint8_t inode_kind[KFS_INODES]={0},entry_kind[KFS_INODES]={0};
    int rc=kfs_cache_read(fs,1,blocks);if(rc<0)return rc;
    rc=kfs_cache_read(fs,2,inodes);if(rc<0)return rc;
    kfs_zero(seen,KFS_BLOCK);
    for(unsigned b=0;b<fs->data_start;b++)seen[b/8]|=(uint8_t)(1U<<(b%8));
    for(unsigned ino=1;ino<KFS_INODES;ino++){
        int allocated=(inodes[ino/8]>>(ino%8))&1;
        if(!allocated)continue;
        struct kfs_node n;rc=kfs_inode_load(fs,ino,&n);
        if(rc<0){bad++;continue;}
        inode_kind[ino]=(uint8_t)n.kind;inode_parent[ino]=n.parent;
        links[ino]=n.links;
        if(n.kind==KFS_KIND_DIR&&n.size%KFS_DIRENT_BYTES)bad++;
        rc=mark_node_blocks(fs,&n,seen);if(rc<0){bad++;continue;}
        if(n.kind==KFS_KIND_DIR){
            unsigned slots=(unsigned)(n.size/KFS_DIRENT_BYTES);
            for(unsigned j=0;j<slots;j++){
                uint32_t block;rc=kfs_data_block_get(fs,&n,j/32,0,&block);
                if(rc<0||!block){bad++;continue;}
                rc=kfs_cache_read(fs,block,fs->scratch);if(rc<0)return rc;
                uint8_t *p=fs->scratch+(j%32)*KFS_DIRENT_BYTES;
                uint32_t target=kfs_u32(p);
                if(!target){
                    if(p[4]||p[5])bad++;
                    continue;
                }
                if(target>=KFS_INODES||!(inodes[target/8]&(1U<<(target%8)))){
                    bad++;continue;
                }
                if(p[4]!=KFS_KIND_FILE&&p[4]!=KFS_KIND_DIR&&p[4]!=KFS_KIND_SYMLINK)
                    bad++;
                if(!kfs_name_bytes_valid(p+6,p[5]))bad++;
                if(target==KFS_ROOT||(refs[target]&&entry_kind[target]!=p[4]))bad++;
                refs[target]++;directory_parent[target]=ino;entry_kind[target]=p[4];
                if(p[5]==0||p[5]>KFS_NAME_MAX)bad++;
            }
        }
    }
    if(inode_kind[KFS_ROOT]!=KFS_KIND_DIR||inode_parent[KFS_ROOT]!=KFS_ROOT)bad++;
    for(unsigned ino=KFS_ROOT+1;ino<KFS_INODES;ino++){
        if(!inode_kind[ino])continue;
        if(!directory_parent[ino]){
            int opened=0;
            for(unsigned j=0;j<KFS_HANDLES;j++)
                if(fs->handles[j].used&&fs->handles[j].ino==ino)opened=1;
            struct kfs_node n;
            if(!opened||kfs_inode_load(fs,ino,&n)<0||n.links!=0)bad++;
            continue;
        }
        if(refs[ino]!=links[ino]||entry_kind[ino]!=inode_kind[ino])bad++;
        if(inode_kind[ino]==KFS_KIND_DIR&&
           (refs[ino]!=1||directory_parent[ino]!=inode_parent[ino]))bad++;
        unsigned parent=ino;
        for(unsigned hops=0;parent>KFS_ROOT&&hops<KFS_INODES;hops++)
            parent=directory_parent[parent];
        if(parent!=KFS_ROOT)bad++;
    }
    if(!(inodes[KFS_ROOT/8]&(1U<<(KFS_ROOT%8))))bad++;
    for(unsigned b=0;b<fs->dev.total_blocks;b++){
        int actual=(blocks[b/8]>>(b%8))&1;
        int expected=(seen[b/8]>>(b%8))&1;
        if(actual!=expected)bad++;
    }
    for(unsigned b=fs->dev.total_blocks;b<KFS_MAX_BLOCKS;b++)
        if((blocks[b/8]>>(b%8))&1)bad++;
    for(unsigned ino=KFS_INODES;ino<KFS_BLOCK*8U;ino++)
        if((inodes[ino/8]>>(ino%8))&1)bad++;
    *errors=bad;return 0;
}

/* Repair only restores allocation bits proven by the reachable tree. Extra
 * bits, malformed pointers, duplicate blocks and orphan inodes are refused;
 * repair never guesses how to destroy potentially live data. */
int kfs_fsck_repair(struct kilofs *fs,unsigned *errors){
    if(!fs||!errors||!fs->mounted)return -K_EINVAL;
    *errors=0;
    if(fs->readonly)return -K_EROFS;
    for(unsigned i=0;i<KFS_HANDLES;i++)if(fs->handles[i].used)return -K_EBUSY;
    int rc=kfs_cache_flush(fs);if(rc<0)return rc;
    uint8_t *actual_blocks=fs->audit_blocks,*actual_inodes=fs->audit_inodes;
    uint8_t *expected_blocks=fs->audit_seen;
    uint8_t reachable[KFS_INODES]={0},kind[KFS_INODES]={0};
    uint32_t refs[KFS_INODES]={0},links[KFS_INODES]={0},parents[KFS_INODES]={0};
    uint32_t queue[KFS_INODES];unsigned head=0,tail=0;
    uint8_t dir_block[KFS_BLOCK];
    rc=kfs_cache_read(fs,1,actual_blocks);if(rc<0)return rc;
    rc=kfs_cache_read(fs,2,actual_inodes);if(rc<0)return rc;
    kfs_zero(expected_blocks,KFS_BLOCK);
    for(unsigned b=0;b<fs->data_start;b++)expected_blocks[b/8]|=(uint8_t)(1U<<(b%8));
    struct kfs_node root;
    rc=kfs_inode_load(fs,KFS_ROOT,&root);
    if(rc<0||root.kind!=KFS_KIND_DIR||root.parent!=KFS_ROOT){*errors=1;return -K_EIO;}
    reachable[KFS_ROOT]=1;kind[KFS_ROOT]=KFS_KIND_DIR;
    links[KFS_ROOT]=root.links;queue[tail++]=KFS_ROOT;
    rc=mark_node_blocks(fs,&root,expected_blocks);
    if(rc<0){*errors=1;return rc;}
    while(head<tail){
        uint32_t dir_ino=queue[head++];struct kfs_node dir;
        rc=kfs_inode_load(fs,dir_ino,&dir);
        if(rc<0||dir.kind!=KFS_KIND_DIR||dir.size%KFS_DIRENT_BYTES){*errors=1;return -K_EIO;}
        unsigned slots=(unsigned)(dir.size/KFS_DIRENT_BYTES);
        for(unsigned slot=0;slot<slots;slot++){
            uint32_t block;rc=kfs_data_block_get(fs,&dir,slot/32,0,&block);
            if(rc<0||!block){*errors=1;return -K_EIO;}
            rc=kfs_cache_read(fs,block,dir_block);if(rc<0)return rc;
            const uint8_t *p=dir_block+(slot%32)*KFS_DIRENT_BYTES;
            uint32_t target=kfs_u32(p);if(!target)continue;
            unsigned name_len=p[5];uint8_t entry_kind=p[4];
            if(target>=KFS_INODES||target==KFS_ROOT||!name_len||name_len>KFS_NAME_MAX||
               (entry_kind!=KFS_KIND_FILE&&entry_kind!=KFS_KIND_DIR&&entry_kind!=KFS_KIND_SYMLINK)||
               !kfs_name_bytes_valid(p+6,name_len)){
                *errors=1;return -K_EIO;
            }
            struct kfs_node child;rc=kfs_inode_load(fs,target,&child);
            if(rc<0||child.kind!=entry_kind){*errors=1;return -K_EIO;}
            if(!reachable[target]){
                reachable[target]=1;kind[target]=(uint8_t)child.kind;links[target]=child.links;
                parents[target]=dir_ino;
                if(tail>=KFS_INODES){*errors=1;return -K_EIO;}
                if(child.kind==KFS_KIND_DIR){
                    if(child.parent!=dir_ino){*errors=1;return -K_EIO;}
                    queue[tail++]=target;
                }
                rc=mark_node_blocks(fs,&child,expected_blocks);
                if(rc<0){*errors=1;return rc;}
            }else if(child.kind==KFS_KIND_DIR){
                *errors=1;return -K_EIO;
            }
            refs[target]++;
        }
    }
    int changed=0;
    for(unsigned ino=1;ino<KFS_INODES;ino++){
        int actual=(actual_inodes[ino/8]>>(ino%8))&1;
        if(reachable[ino]){
            if(ino!=KFS_ROOT&&(refs[ino]!=links[ino]||
               (kind[ino]==KFS_KIND_DIR&&parents[ino]==0))){*errors=1;return -K_EIO;}
            if(!actual){actual_inodes[ino/8]|=(uint8_t)(1U<<(ino%8));changed=1;}
        }else if(actual){*errors=1;return -K_EIO;}
    }
    for(unsigned ino=KFS_INODES;ino<KFS_BLOCK*8U;ino++)
        if((actual_inodes[ino/8]>>(ino%8))&1){*errors=1;return -K_EIO;}
    for(unsigned b=0;b<fs->dev.total_blocks;b++){
        int actual=(actual_blocks[b/8]>>(b%8))&1;
        int expected=(expected_blocks[b/8]>>(b%8))&1;
        if(actual&&!expected){*errors=1;return -K_EIO;}
        if(expected&&!actual){actual_blocks[b/8]|=(uint8_t)(1U<<(b%8));changed=1;}
    }
    for(unsigned b=fs->dev.total_blocks;b<KFS_MAX_BLOCKS;b++)
        if((actual_blocks[b/8]>>(b%8))&1){*errors=1;return -K_EIO;}
    if(changed){
        rc=kfs_cache_write(fs,1,actual_blocks);if(rc<0)return rc;
        rc=kfs_cache_write(fs,2,actual_inodes);if(rc<0)return rc;
        rc=kfs_sync(fs);if(rc<0)return rc;
    }
    return kfs_fsck(fs,errors);
}

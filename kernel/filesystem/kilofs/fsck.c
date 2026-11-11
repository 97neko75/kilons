#include "internal.h"
/* Diagnostic, non-repairing checker. Does not touch media. Validates reachable
 * inodes and direct block references against allocation bitmaps. Does not
 * establish atomic consistency after interrupted rename or allocation. */
int kfs_fsck(struct kilofs *fs,unsigned *errors){
    if(!fs||!errors||!fs->mounted)return -K_EINVAL;
    unsigned bad=0;uint8_t *blocks=fs->audit_blocks,*inodes=fs->audit_inodes,*seen=fs->audit_seen;
    /* kilofs v1 has a single parent and no hard links. Record the actual
     * directory edge separately from the parent stored in each inode. */
    uint32_t directory_parent[KFS_INODES]={0},inode_parent[KFS_INODES]={0};
    uint8_t inode_kind[KFS_INODES]={0},entry_kind[KFS_INODES]={0};
    int rc=kfs_cache_read(fs,1,blocks);if(rc<0)return rc;
    rc=kfs_cache_read(fs,2,inodes);if(rc<0)return rc;
    kfs_zero(seen,KFS_BLOCK);
    for(unsigned b=0;b<KFS_DATA_START;b++)seen[b/8]|=(uint8_t)(1U<<(b%8));
    for(unsigned ino=1;ino<KFS_INODES;ino++){
        int allocated=(inodes[ino/8]>>(ino%8))&1;
        if(!allocated)continue;
        struct kfs_node n;rc=kfs_inode_load(fs,ino,&n);
        if(rc<0){bad++;continue;}
        inode_kind[ino]=(uint8_t)n.kind;
        inode_parent[ino]=n.parent;
        if(n.kind==KFS_KIND_DIR && n.size%KFS_DIRENT_BYTES)bad++;
        if(n.size>(uint64_t)KFS_BLOCK*KFS_MAX_FILE_BLOCKS)bad++;
        for(unsigned j=0;j<KFS_MAX_FILE_BLOCKS;j++)if(n.direct[j]){
            unsigned b=n.direct[j];
            if(b<KFS_DATA_START||b>=fs->dev.total_blocks){bad++;continue;}
            if(seen[b/8]&(1U<<(b%8)))bad++; /* duplicate block ownership */
            seen[b/8]|=(uint8_t)(1U<<(b%8));
        }
        if(n.kind==KFS_KIND_DIR){
            unsigned slots=(unsigned)(n.size/KFS_DIRENT_BYTES);
            for(unsigned j=0;j<slots;j++){
                uint32_t blk=n.direct[j/32];
                if(!blk||blk>=fs->dev.total_blocks){bad++;continue;}
                rc=kfs_cache_read(fs,blk,fs->scratch);if(rc<0)return rc;
                uint8_t *p=fs->scratch+(j%32)*KFS_DIRENT_BYTES;
                uint32_t target=kfs_u32(p);
                if(!target)continue;
                if(target>=KFS_INODES || !(inodes[target/8]&(1U<<(target%8)))){
                    bad++;continue;
                }
                if(target==KFS_ROOT || directory_parent[target])bad++;
                directory_parent[target]=ino;
                entry_kind[target]=p[4];
                if(p[5]==0||p[5]>KFS_NAME_MAX)bad++;
            }
        }
    }
    if(inode_kind[KFS_ROOT]!=KFS_KIND_DIR || inode_parent[KFS_ROOT]!=KFS_ROOT)bad++;
    for(unsigned ino=KFS_ROOT+1;ino<KFS_INODES;ino++){
        if(!inode_kind[ino])continue;
        if(!directory_parent[ino]){
            /* An unlinked file may still be held open on a live mount. */
            int opened=0;
            for(unsigned j=0;j<KFS_HANDLES;j++)
                if(fs->handles[j].used && fs->handles[j].ino==ino)opened=1;
            struct kfs_node n;
            if(!opened || kfs_inode_load(fs,ino,&n)<0 || n.links!=0)bad++;
            continue;
        }
        if(directory_parent[ino]!=inode_parent[ino] || entry_kind[ino]!=inode_kind[ino])bad++;
        unsigned parent=ino;
        for(unsigned hops=0;parent>KFS_ROOT && hops<KFS_INODES;hops++)
            parent=directory_parent[parent];
        if(parent!=KFS_ROOT)bad++;
    }
    if(!(inodes[KFS_ROOT/8]&(1U<<(KFS_ROOT%8))))bad++;
    for(unsigned b=0;b<fs->dev.total_blocks;b++){
        int actual=(blocks[b/8]>>(b%8))&1;
        int expected=(seen[b/8]>>(b%8))&1;
        if(actual!=expected)bad++;
    }
    *errors=bad;return 0;
}

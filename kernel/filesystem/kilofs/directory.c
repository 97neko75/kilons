#include "internal.h"
/* Directory records are fixed 128-byte slots: ino(u32), kind(u8),
 * name_len(u8), name[122]. Deleted slots have ino=0. */
static int read_slot(struct kilofs *fs,struct kfs_node *dir,uint32_t slot,struct kfs_dirent *out){
    if(dir->kind!=KFS_KIND_DIR)return -K_ENOTDIR;
    if((uint64_t)(slot+1)*KFS_DIRENT_BYTES>dir->size)return 0;
    uint32_t index=slot/32,offset=(slot%32)*KFS_DIRENT_BYTES;
    if(index>=KFS_MAX_FILE_BLOCKS||!dir->direct[index])return -K_EIO;
    int rc=kfs_cache_read(fs,dir->direct[index],fs->scratch);if(rc<0)return rc;
    const uint8_t *p=fs->scratch+offset;
    out->ino=kfs_u32(p);out->kind=p[4];
    unsigned len=p[5];if(len>KFS_NAME_MAX)return -K_EIO;
    for(unsigned j=0;j<len;j++)out->name[j]=(char)p[6+j];
    out->name[len]=0;
    return 1;
}
static int write_slot(struct kilofs *fs,uint32_t dir_ino,struct kfs_node *dir,
                      uint32_t slot,const struct kfs_dirent *entry){
    if(fs->readonly)return -K_EROFS;
    if(slot>=KFS_MAX_FILE_BLOCKS*32)return -K_ENOSPC;
    uint32_t index=slot/32,offset=(slot%32)*KFS_DIRENT_BYTES;
    if(!dir->direct[index]){
        int rc=kfs_block_allocate(fs,&dir->direct[index]);if(rc<0)return rc;
    }
    int rc=kfs_cache_read(fs,dir->direct[index],fs->scratch);if(rc<0)return rc;
    uint8_t *p=fs->scratch+offset;kfs_zero(p,KFS_DIRENT_BYTES);
    kfs_p32(p,entry->ino);p[4]=entry->kind;
    unsigned len=0;while(entry->name[len]&&len<=KFS_NAME_MAX)len++;
    if(len>KFS_NAME_MAX)return -K_ENAMETOOLONG;
    p[5]=(uint8_t)len;kfs_copy(p+6,entry->name,len);
    rc=kfs_cache_write(fs,dir->direct[index],fs->scratch);if(rc<0)return rc;
    uint64_t end=(uint64_t)(slot+1)*KFS_DIRENT_BYTES;
    if(dir->size<end)dir->size=end;
    return kfs_inode_save(fs,dir_ino,dir);
}
int kfs_dir_find(struct kilofs *fs,uint32_t parent,const char *name,
                 uint32_t *ino,uint8_t *kind){
    struct kfs_node dir;int rc=kfs_inode_load(fs,parent,&dir);if(rc<0)return rc;
    if(dir.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
    unsigned slots=(unsigned)(dir.size/KFS_DIRENT_BYTES);
    for(unsigned i=0;i<slots;i++){
        struct kfs_dirent e;rc=read_slot(fs,&dir,i,&e);if(rc<0)return rc;
        if(e.ino&&kfs_equal(e.name,name)){
            if(ino)*ino=e.ino;
            if(kind)*kind=e.kind;
            return 0;
        }
    }
    return -K_ENOENT;
}
int kfs_dir_add(struct kilofs *fs,uint32_t parent,const char *name,uint32_t ino,uint8_t kind){
    if(!fs||!name||!ino||ino>=KFS_INODES)return -K_EINVAL;
    unsigned len=0;while(name[len]&&len<=KFS_NAME_MAX)len++;
    if(!len||len>KFS_NAME_MAX)return -K_ENAMETOOLONG;
    if(kfs_equal(name,".")||kfs_equal(name,".."))return -K_EINVAL;
    int rc=kfs_dir_find(fs,parent,name,0,0);if(rc==0)return -K_EEXIST;
    if(rc!=-K_ENOENT)return rc;
    struct kfs_node dir;rc=kfs_inode_load(fs,parent,&dir);if(rc<0)return rc;
    unsigned slots=(unsigned)(dir.size/KFS_DIRENT_BYTES),target=slots;
    for(unsigned i=0;i<slots;i++){
        struct kfs_dirent e;rc=read_slot(fs,&dir,i,&e);if(rc<0)return rc;
        if(e.ino==0){target=i;break;}
    }
    struct kfs_dirent e={0};e.ino=ino;e.kind=kind;
    for(unsigned i=0;i<len;i++)e.name[i]=name[i];
    e.name[len]=0;
    return write_slot(fs,parent,&dir,target,&e);
}
int kfs_dir_remove(struct kilofs *fs,uint32_t parent,const char *name){
    struct kfs_node dir;int rc=kfs_inode_load(fs,parent,&dir);if(rc<0)return rc;
    if(dir.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
    unsigned slots=(unsigned)(dir.size/KFS_DIRENT_BYTES);
    for(unsigned i=0;i<slots;i++){
        struct kfs_dirent e;rc=read_slot(fs,&dir,i,&e);if(rc<0)return rc;
        if(e.ino&&kfs_equal(e.name,name)){
            e.ino=0;e.kind=0;e.name[0]=0;
            return write_slot(fs,parent,&dir,i,&e);
        }
    }
    return -K_ENOENT;
}
int kfs_dir_index(struct kilofs *fs,uint32_t parent,unsigned index,struct kfs_dirent *out){
    if(!out)return -K_EFAULT;
    struct kfs_node dir;int rc=kfs_inode_load(fs,parent,&dir);if(rc<0)return rc;
    if(dir.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
    unsigned slots=(unsigned)(dir.size/KFS_DIRENT_BYTES),seen=0;
    for(unsigned i=0;i<slots;i++){
        struct kfs_dirent e;rc=read_slot(fs,&dir,i,&e);if(rc<0)return rc;
        if(e.ino&&seen++==index){*out=e;return 1;}
    }
    return 0;
}
static int component(const char **ptr,char out[KFS_NAME_MAX+1]){
    const char *p=*ptr;while(*p=='/')p++;
    if(!*p){*ptr=p;return 0;}
    unsigned n=0;
    while(*p&&*p!='/'){
        if(n>=KFS_NAME_MAX)return -K_ENAMETOOLONG;
        out[n++]=*p++;
    }
    out[n]=0;*ptr=p;return 1;
}
int kfs_path(struct kilofs *fs,const char *path,uint32_t *ino){
    if(!fs||!path||!ino)return -K_EINVAL;
    const char *p=path;uint32_t cur=KFS_ROOT;char name[KFS_NAME_MAX+1];
    for(;;){
        int rc=component(&p,name);if(rc<0)return rc;if(rc==0)break;
        struct kfs_node node;rc=kfs_inode_load(fs,cur,&node);if(rc<0)return rc;
        if(node.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
        if(kfs_equal(name,"."))continue;
        if(kfs_equal(name,"..")){cur=node.parent;continue;}
        rc=kfs_dir_find(fs,cur,name,&cur,0);if(rc<0)return rc;
    }
    *ino=cur;return 0;
}
int kfs_parent(struct kilofs *fs,const char *path,uint32_t *parent,
               char name[KFS_NAME_MAX+1]){
    if(!fs||!path||!parent||!name)return -K_EINVAL;
    const char *p=path;uint32_t cur=KFS_ROOT;
    for(;;){
        char part[KFS_NAME_MAX+1];int rc=component(&p,part);if(rc<=0)return rc<0?rc:-K_EINVAL;
        const char *next=p;while(*next=='/')next++;
        if(!*next){
            if(kfs_equal(part,".")||kfs_equal(part,".."))return -K_EINVAL;
            unsigned i=0;while(part[i]){name[i]=part[i];i++;}name[i]=0;
            *parent=cur;return 0;
        }
        struct kfs_node node;rc=kfs_inode_load(fs,cur,&node);if(rc<0)return rc;
        if(node.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
        if(kfs_equal(part,"."))continue;
        if(kfs_equal(part,"..")){cur=node.parent;continue;}
        rc=kfs_dir_find(fs,cur,part,&cur,0);if(rc<0)return rc;
    }
}

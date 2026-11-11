#include "internal.h"
/* Process-global backend instance: one kilofs mount in the initial release.
 * The generic VFS owns pathname permissions and open-description lifetime.
 * These calls are serialized under one FS lock by their caller. */
static int get_node(struct kilofs *fs,const char *path,uint32_t *ino,struct kfs_node *node){
    int rc=kfs_path(fs,path,ino);if(rc<0)return rc;
    return kfs_inode_load(fs,*ino,node);
}
static int busy_inode(struct kilofs *fs,uint32_t ino){
    for(unsigned i=0;i<KFS_HANDLES;i++)if(fs->handles[i].used&&fs->handles[i].ino==ino)return 1;
    return 0;
}
int kfs_open(struct kilofs *fs,const char *path,unsigned flags,int create,unsigned mode){
    if(!fs||!fs->mounted||!path||!(flags&(KFS_READ|KFS_WRITE))||
       (flags&~(KFS_READ|KFS_WRITE)))return -K_EINVAL;
    if(fs->readonly&&(flags&KFS_WRITE))return -K_EROFS;
    unsigned slot=KFS_HANDLES;
    for(unsigned i=0;i<KFS_HANDLES;i++)if(!fs->handles[i].used){slot=i;break;}
    if(slot==KFS_HANDLES)return -K_ENFILE;
    uint32_t ino;struct kfs_node node;
    int rc=get_node(fs,path,&ino,&node);
    if(rc==-K_ENOENT&&create){
        if(fs->readonly)return -K_EROFS;
        uint32_t parent;char name[KFS_NAME_MAX+1];
        rc=kfs_parent(fs,path,&parent,name);if(rc<0)return rc;
        struct kfs_node pnode;rc=kfs_inode_load(fs,parent,&pnode);if(rc<0)return rc;
        if(pnode.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
        rc=kfs_inode_allocate(fs,&ino);if(rc<0)return rc;
        node=(struct kfs_node){0};node.kind=KFS_KIND_FILE;
        node.mode=(uint16_t)(mode&0777U);node.links=1;node.parent=parent;
        node.uid=fs->uid;node.gid=fs->gid;
        rc=kfs_inode_save(fs,ino,&node);
        if(rc<0){(void)kfs_inode_free(fs,ino);return rc;}
        rc=kfs_dir_add(fs,parent,name,ino,KFS_KIND_FILE);
        if(rc<0){(void)kfs_drop_inode(fs,ino,&node);return rc;}
    } else if(rc<0)return rc;
    if(node.kind!=KFS_KIND_FILE)return -K_EISDIR;
    fs->handles[slot]=(struct kfs_handle){ino,0,flags,1};
    return (int)slot;
}
int kfs_close(struct kilofs *fs,int fd){
    if(!fs||fd<0||fd>=(int)KFS_HANDLES||!fs->handles[fd].used)return -K_EBADF;
    uint32_t ino=fs->handles[fd].ino;fs->handles[fd].used=0;
    struct kfs_node n;int rc=kfs_inode_load(fs,ino,&n);
    if(rc<0)return rc;
    if(n.links==0&&!busy_inode(fs,ino))return kfs_drop_inode(fs,ino,&n);
    return 0;
}
int kfs_read(struct kilofs *fs,int fd,void *buf,size_t count){
    if(!fs||fd<0||fd>=(int)KFS_HANDLES||!fs->handles[fd].used)return -K_EBADF;
    struct kfs_handle *h=&fs->handles[fd];
    if(!(h->flags&KFS_READ))return -K_EBADF;
    struct kfs_node n;int rc=kfs_inode_load(fs,h->ino,&n);if(rc<0)return rc;
    rc=kfs_read_bytes(fs,&n,h->offset,buf,count);
    if(rc>0)h->offset+=(uint64_t)rc;
    return rc;
}
int kfs_write(struct kilofs *fs,int fd,const void *buf,size_t count){
    if(!fs||fd<0||fd>=(int)KFS_HANDLES||!fs->handles[fd].used)return -K_EBADF;
    struct kfs_handle *h=&fs->handles[fd];
    if(!(h->flags&KFS_WRITE))return -K_EBADF;
    struct kfs_node n;int rc=kfs_inode_load(fs,h->ino,&n);if(rc<0)return rc;
    rc=kfs_write_bytes(fs,&n,h->offset,buf,count);
    if(rc>=0){
        int save=kfs_inode_save(fs,h->ino,&n);
        if(save<0)return save;
        h->offset+=(uint64_t)rc;
    }
    return rc;
}
int kfs_seek(struct kilofs *fs,int fd,uint64_t offset){
    if(!fs||fd<0||fd>=(int)KFS_HANDLES||!fs->handles[fd].used)return -K_EBADF;
    if(offset>KFS_MAX_FILE_BLOCKS*(uint64_t)KFS_BLOCK)return -K_EINVAL;
    fs->handles[fd].offset=offset;return 0;
}
static void info_from_node(uint32_t ino,const struct kfs_node *n,struct kfs_info *out){
    out->ino=ino;out->uid=n->uid;out->gid=n->gid;
    out->kind=n->kind;out->mode=n->mode;out->size=n->size;
}
int kfs_fstat(struct kilofs *fs,int fd,struct kfs_info *out){
    if(!fs||!out)return -K_EFAULT;
    if(fd<0||fd>=(int)KFS_HANDLES||!fs->handles[fd].used)return -K_EBADF;
    struct kfs_node n;uint32_t ino=fs->handles[fd].ino;
    int rc=kfs_inode_load(fs,ino,&n);if(rc<0)return rc;
    info_from_node(ino,&n,out);return 0;
}
int kfs_stat(struct kilofs *fs,const char *path,struct kfs_info *out){
    if(!fs||!out||!path)return -K_EINVAL;
    uint32_t ino;struct kfs_node n;
    int rc=get_node(fs,path,&ino,&n);if(rc<0)return rc;
    info_from_node(ino,&n,out);return 0;
}
int kfs_readdir(struct kilofs *fs,const char *path,unsigned idx,struct kfs_dirent *out){
    if(!out)return -K_EFAULT;
    uint32_t ino;struct kfs_node n;int rc=get_node(fs,path,&ino,&n);if(rc<0)return rc;
    return kfs_dir_index(fs,ino,idx,out);
}
int kfs_mkdir(struct kilofs *fs,const char *path,unsigned mode){
    if(!fs||fs->readonly)return -K_EROFS;
    uint32_t parent;char name[KFS_NAME_MAX+1];
    int rc=kfs_parent(fs,path,&parent,name);if(rc<0)return rc;
    uint32_t exists;rc=kfs_dir_find(fs,parent,name,&exists,0);
    if(rc==0)return -K_EEXIST;
    if(rc!=-K_ENOENT)return rc;
    struct kfs_node pn;rc=kfs_inode_load(fs,parent,&pn);if(rc<0)return rc;
    if(pn.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
    uint32_t ino;rc=kfs_inode_allocate(fs,&ino);if(rc<0)return rc;
    struct kfs_node n={0};n.kind=KFS_KIND_DIR;n.mode=(uint16_t)(mode&07777U);
    n.links=1;n.parent=parent;n.uid=fs->uid;n.gid=fs->gid;
    rc=kfs_inode_save(fs,ino,&n);if(rc<0){(void)kfs_inode_free(fs,ino);return rc;}
    rc=kfs_dir_add(fs,parent,name,ino,KFS_KIND_DIR);
    if(rc<0){(void)kfs_drop_inode(fs,ino,&n);return rc;}
    return 0;
}
static int delete_entry(struct kilofs *fs,const char *path,int directory){
    if(!fs||fs->readonly)return -K_EROFS;
    uint32_t parent;char name[KFS_NAME_MAX+1];
    int rc=kfs_parent(fs,path,&parent,name);if(rc<0)return rc;
    uint32_t ino;rc=kfs_dir_find(fs,parent,name,&ino,0);if(rc<0)return rc;
    struct kfs_node n;rc=kfs_inode_load(fs,ino,&n);if(rc<0)return rc;
    if(directory && n.kind!=KFS_KIND_DIR)return -K_ENOTDIR;
    if(!directory && n.kind==KFS_KIND_DIR)return -K_EISDIR;
    if(directory){
        struct kfs_dirent e;rc=kfs_dir_index(fs,ino,0,&e);
        if(rc<0)return rc;
        if(rc>0)return -K_ENOTEMPTY;
    }
    rc=kfs_dir_remove(fs,parent,name);if(rc<0)return rc;
    n.links=0;
    rc=kfs_inode_save(fs,ino,&n);if(rc<0)return rc;
    if(!busy_inode(fs,ino))return kfs_drop_inode(fs,ino,&n);
    return 0;
}
int kfs_unlink(struct kilofs *fs,const char *path){return delete_entry(fs,path,0);}
int kfs_rmdir(struct kilofs *fs,const char *path){return delete_entry(fs,path,1);}
int kfs_rename(struct kilofs *fs,const char *oldpath,const char *newpath){
    if(!fs||fs->readonly)return -K_EROFS;
    uint32_t oldparent,newparent;char oldname[KFS_NAME_MAX+1],newname[KFS_NAME_MAX+1];
    int rc=kfs_parent(fs,oldpath,&oldparent,oldname);if(rc<0)return rc;
    rc=kfs_parent(fs,newpath,&newparent,newname);if(rc<0)return rc;
    if(oldparent==newparent&&kfs_equal(oldname,newname))return 0;
    uint32_t ino;uint8_t kind;
    rc=kfs_dir_find(fs,oldparent,oldname,&ino,&kind);if(rc<0)return rc;
    uint32_t unused;rc=kfs_dir_find(fs,newparent,newname,&unused,0);
    if(rc==0)return -K_EEXIST;
    if(rc!=-K_ENOENT)return rc;
    struct kfs_node n;rc=kfs_inode_load(fs,ino,&n);if(rc<0)return rc;
    if(kind==KFS_KIND_DIR){
        uint32_t p=newparent;
        for(unsigned i=0;i<KFS_INODES;i++){
            if(p==ino)return -K_EINVAL;
            if(p==KFS_ROOT)break;
            struct kfs_node parent_node;rc=kfs_inode_load(fs,p,&parent_node);if(rc<0)return rc;
            if(parent_node.parent==p)return -K_EIO;
            p=parent_node.parent;
        }
    }
    /* This ordered update is NOT crash-atomic: on interruption fsck is needed. */
    rc=kfs_dir_add(fs,newparent,newname,ino,kind);if(rc<0)return rc;
    rc=kfs_dir_remove(fs,oldparent,oldname);
    if(rc<0){(void)kfs_dir_remove(fs,newparent,newname);return rc;}
    n.parent=newparent;return kfs_inode_save(fs,ino,&n);
}
int kfs_truncate(struct kilofs *fs,const char *path,uint64_t size){
    uint32_t ino;struct kfs_node n;int rc=get_node(fs,path,&ino,&n);if(rc<0)return rc;
    if(n.kind!=KFS_KIND_FILE)return -K_EISDIR;
    return kfs_resize(fs,ino,&n,size);
}
int kfs_ftruncate(struct kilofs *fs,int fd,uint64_t size){
    if(!fs||fd<0||fd>=(int)KFS_HANDLES||!fs->handles[fd].used)return -K_EBADF;
    if(!(fs->handles[fd].flags&KFS_WRITE))return -K_EBADF;
    uint32_t ino=fs->handles[fd].ino;
    struct kfs_node n;int rc=kfs_inode_load(fs,ino,&n);if(rc<0)return rc;
    return kfs_resize(fs,ino,&n,size);
}
int kfs_chmod(struct kilofs *fs,const char *path,unsigned mode){
    if(!fs||fs->readonly)return -K_EROFS;
    uint32_t ino;struct kfs_node n;int rc=get_node(fs,path,&ino,&n);if(rc<0)return rc;
    n.mode=(uint16_t)(mode&07777U);return kfs_inode_save(fs,ino,&n);
}

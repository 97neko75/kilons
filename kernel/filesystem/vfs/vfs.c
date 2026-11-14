#include <kilons/vfs.h>
#include <kilons/record_lock.h>
#include "../../../include/dirent.h"
#include <kilons/ramfs.h>
#include <kilons/iso9660.h>
#include <kilons/kerrno.h>
#include <kilons/fat32_vfs.h>
#include <kilons/kilofs.h>
#include <kilons/kilofs_block.h>
#include <stdint.h>
/* Small mount-based kernel VFS. Mount selection and descriptors belong here;
 * backing filesystems implement the operations and know no absolute paths. */
#define FD_COUNT 64
#define MOUNT_COUNT 4
struct fs_ops {
    unsigned dev; /* stable filesystem instance ID, also shared by mount aliases */
    int (*open)(const char *, unsigned, int, unsigned);
    int (*close)(int);
    int (*read)(int, void *, size_t);
    int (*write)(int, const void *, size_t);
    int (*seek)(int, size_t);
    int (*unlink)(const char *);
    int (*mkdir)(const char *, unsigned);
    int (*rmdir)(const char *);
    int (*rename)(const char *, const char *);
    int (*truncate)(const char *, size_t);
    int (*ftruncate)(int, size_t);
    int (*fstat)(int, struct ramfs_info *);
    int (*stat)(const char *, struct ramfs_info *);
    int (*readdir)(const char *, unsigned, struct ramfs_dirent *);
    int (*symlink)(const char *,const char *);
    int (*readlink)(const char *,char *,size_t);
};
struct mount { const char *path; const struct fs_ops *ops; int active; int readonly; int keep_prefix; };
struct descriptor { unsigned refs; uint64_t inode; int backend_fd; unsigned flags; const struct fs_ops *ops; size_t offset; unsigned append,nonblock,sync_flags; char path[256]; uint64_t detached_id; struct ramfs_info directory; };
static struct mount mounts[MOUNT_COUNT];
static struct descriptor fds[FD_COUNT];
static const struct fs_ops ram_ops = {
    1,
    ramfs_open_mode, ramfs_close, ramfs_read, ramfs_write, ramfs_seek, ramfs_unlink, ramfs_mkdir_mode, ramfs_rmdir, ramfs_rename, ramfs_truncate, ramfs_ftruncate, ramfs_fstat, ramfs_stat, ramfs_readdir, ramfs_symlink, ramfs_readlink
};
static int iso_stat_adapter(const char *p,struct ramfs_info *out) {
    struct vfs_info v; if(iso9660_stat(p,&v)<0)return -1;
    out->is_dir=v.is_dir;out->size=v.size;out->mode=v.is_dir?0555U:0444U;out->uid=0;out->gid=0;
    uint64_t hash=1469598103934665603ULL;while(*p){hash^=(unsigned char)*p++;hash*=1099511628211ULL;}out->times=(struct file_times){0};out->blocks=(out->size+511)/512;out->ino=hash;out->links=1;return 0;
}
static int iso_dir_adapter(const char *p,unsigned i,struct ramfs_dirent *out) {
    struct vfs_dirent v;int rc=iso9660_readdir(p,i,&v);
    if(rc!=1)return rc;
    unsigned j=0;while(v.name[j]&&j<sizeof(out->name)-1){out->name[j]=v.name[j];j++;}
    out->name[j]=0;out->is_dir=v.is_dir;return 1;
}
static int iso_open_adapter(const char *path,unsigned flags,int create,unsigned mode) {
    (void)mode;
    return iso9660_open(path,flags,create);
}
static int fat_open_adapter(const char *path,unsigned flags,int create,unsigned mode) {
    (void)mode;
    return fat32_vfs_open(path,flags,create);
}
static const struct fs_ops iso_ops __attribute__((unused))={
    2,
    iso_open_adapter,iso9660_close,iso9660_read,0,iso9660_seek,0,0,0,0,0,0,0,iso_stat_adapter,iso_dir_adapter,0,0
};
static const struct fs_ops fat_ops={
    3,
    fat_open_adapter,fat32_vfs_close,fat32_vfs_read,0,fat32_vfs_seek,
    0,0,0,0,0,0,fat32_vfs_fstat,fat32_vfs_stat,fat32_vfs_readdir,0,0
};
static char fat_mount_path[256];
static char kilofs_mount_path[256];
static struct kilofs diskfs;
static struct kfs_block_context diskctx;
static int kfs_open_adapter(const char *p,unsigned f,int create,unsigned mode){return kfs_open(&diskfs,p,f,create,mode);}
static int kfs_close_adapter(int fd){return kfs_close(&diskfs,fd);}
static int kfs_read_adapter(int fd,void *buf,size_t n){return kfs_read(&diskfs,fd,buf,n);}
static int kfs_write_adapter(int fd,const void *buf,size_t n){return kfs_write(&diskfs,fd,buf,n);}
static int kfs_seek_adapter(int fd,size_t n){return kfs_seek(&diskfs,fd,n);}
static int kfs_unlink_adapter(const char *p){return kfs_unlink(&diskfs,p);}
static int kfs_mkdir_adapter(const char *p,unsigned mode){return kfs_mkdir(&diskfs,p,mode);}
static int kfs_rmdir_adapter(const char *p){return kfs_rmdir(&diskfs,p);}
static int kfs_rename_adapter(const char *a,const char *b){return kfs_rename(&diskfs,a,b);}
static int kfs_trunc_adapter(const char *p,size_t n){return kfs_truncate(&diskfs,p,n);}
static int kfs_ftrunc_adapter(int fd,size_t n){return kfs_ftruncate(&diskfs,fd,n);}
static void kfs_copy_info(struct ramfs_info *dst,const struct kfs_info *src){
    dst->times=src->times;dst->blocks=src->blocks;
    dst->is_dir=src->kind==KFS_KIND_DIR;dst->size=(size_t)src->size;
    dst->mode=src->mode | (src->kind==KFS_KIND_SYMLINK?0120000U:0U);dst->uid=src->uid;dst->gid=src->gid;dst->ino=src->ino;dst->links=src->links;
}
static int kfs_fstat_adapter(int fd,struct ramfs_info *out){
    struct kfs_info in;int rc=kfs_fstat(&diskfs,fd,&in);
    if(rc<0)return rc;
    kfs_copy_info(out,&in);return 0;
}
static int kfs_stat_adapter(const char *p,struct ramfs_info *out){
    struct kfs_info in;int rc=kfs_stat(&diskfs,p,&in);
    if(rc<0)return rc;
    kfs_copy_info(out,&in);return 0;
}
static int kfs_readdir_adapter(const char *p,unsigned i,struct ramfs_dirent *out){
    struct kfs_dirent in;int rc=kfs_readdir(&diskfs,p,i,&in);if(rc!=1)return rc;
    unsigned j=0;while(in.name[j]&&j<sizeof(out->name)-1){out->name[j]=in.name[j];j++;}
    out->name[j]=0;out->is_dir=in.kind==KFS_KIND_DIR;return 1;
}
static int kfs_symlink_adapter(const char *t,const char *p){return kfs_symlink(&diskfs,t,p);}
static int kfs_readlink_adapter(const char *p,char *b,size_t n){return kfs_readlink(&diskfs,p,b,n);}
static const struct fs_ops kfs_ops={
    4,
    kfs_open_adapter,kfs_close_adapter,kfs_read_adapter,kfs_write_adapter,
    kfs_seek_adapter,kfs_unlink_adapter,kfs_mkdir_adapter,kfs_rmdir_adapter,
    kfs_rename_adapter,kfs_trunc_adapter,kfs_ftrunc_adapter,kfs_fstat_adapter,
    kfs_stat_adapter,kfs_readdir_adapter,kfs_symlink_adapter,kfs_readlink_adapter
};
static int same(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
/* Canonicalize absolute paths; reject escaping above / and bounded overflow.
 * This helper only performs lexical normalization. */
static int canonical(const char *path, char *out, size_t cap) {
    if (!path || path[0] != '/' || cap < 2) return -1;
    size_t n = 1;
    out[0] = '/'; out[1] = 0;
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        const char *start = path;
        while (*path && *path != '/') path++;
        size_t len = (size_t)(path - start);
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (n > 1) {
                while (n > 1 && out[n-1] != '/') n--;
                if (n > 1) n--;
                out[n] = 0;
            }
            continue;
        }
        if (n + (n > 1 ? 1U : 0U) + len >= cap) return -1;
        if (n > 1) out[n++] = '/';
        for (size_t i=0;i<len;i++) out[n++] = start[i];
        out[n] = 0;
    }
    return 0;
}
int vfs_normalize(const char *path,char *out,size_t capacity) {
    return canonical(path,out,capacity);
}
static size_t mount_match(const char *path, const char *mount_path) {
    size_t n = 0;
    while (mount_path[n] && path[n] == mount_path[n]) n++;
    if (mount_path[n]) return 0;
    if (n == 1 && mount_path[0] == '/') return n;
    return path[n] == 0 || path[n] == '/' ? n : 0;
}
static const struct mount *resolve(const char *path, const char **relative) {
    const struct mount *best = 0;
    size_t longest = 0;
    for (int i=0;i<MOUNT_COUNT;i++) if (mounts[i].active) {
        size_t n = mount_match(path, mounts[i].path);
        if (n > longest) { longest = n; best = &mounts[i]; }
    }
    if (!best) return 0;
    const char *p = best->keep_prefix ? path : path + longest;
    if (*p == '/') p++;
    *relative = p;
    return best;
}
static int mount_fs(const char *path, const struct fs_ops *ops) {
    if (!path || !ops || path[0] != '/') return -1;
    for (int i=0;i<MOUNT_COUNT;i++) if (mounts[i].active && same(mounts[i].path,path)) return -1;
    for (int i=0;i<MOUNT_COUNT;i++) if (!mounts[i].active) {
        mounts[i].path=path; mounts[i].ops=ops; mounts[i].active=1; mounts[i].readonly=0;mounts[i].keep_prefix=0;
        return 0;
    }
    return -1;
}
/* A bounded, allocation-free dentry and inode metadata cache. The backing
 * filesystem owns authoritative inodes and file data. All namespace/data
 * mutations invalidate cached metadata, including aliases after rename. */
#define DENTRY_SLOTS 32
#define INODE_SLOTS 32
struct inode_cache { const struct mount *mnt; uint64_t ino; struct ramfs_info info; int valid; };
struct dentry_cache { char path[256]; unsigned inode_slot; int valid; };
static struct inode_cache inode_cache[INODE_SLOTS];
static struct dentry_cache dentry_cache[DENTRY_SLOTS];
static unsigned inode_hand,dentry_hand;
static uint32_t cred_uid,cred_gid;
static int last_errno;
static int fail(int err) {last_errno=err;return -1;}
/* Backends use -1 for an unspecified failure and negative K_errno values
 * when they know the exact cause. Preserve the latter at the VFS boundary. */
static int backend_errno(int rc,int fallback) {
    return rc<-1?-rc:fallback;
}
int vfs_errno(void) {return last_errno;}
void vfs_set_credentials(uint32_t uid,uint32_t gid) {
    cred_uid=uid;cred_gid=gid;ramfs_set_identity(uid,gid);
    diskfs.uid=uid;diskfs.gid=gid;
}
static void cache_invalidate(void) {
    for(unsigned i=0;i<DENTRY_SLOTS;i++)dentry_cache[i].valid=0;
    for(unsigned i=0;i<INODE_SLOTS;i++)inode_cache[i].valid=0;
}
static void copy_path(char *dst,const char *src) {
    unsigned i=0;while(src[i] && i<255){dst[i]=src[i];i++;}dst[i]=0;
}
static int raw_stat(const char *full,struct ramfs_info *info) {
    const char *relative;const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->stat)return -1;
    for(unsigned i=0;i<DENTRY_SLOTS;i++)if(dentry_cache[i].valid && same(dentry_cache[i].path,full)) {
        unsigned slot=dentry_cache[i].inode_slot;
        if(slot<INODE_SLOTS && inode_cache[slot].valid && inode_cache[slot].mnt==mnt) {
            *info=inode_cache[slot].info;return 0;
        }
    }
    int rc=mnt->ops->stat(relative,info);
    if(rc<0)return rc;
    unsigned slot=INODE_SLOTS;
    for(unsigned i=0;i<INODE_SLOTS;i++)if(inode_cache[i].valid && inode_cache[i].mnt==mnt && inode_cache[i].ino==info->ino) {slot=i;break;}
    if(slot==INODE_SLOTS) {
        slot=inode_hand++%INODE_SLOTS;
        for(unsigned i=0;i<DENTRY_SLOTS;i++)if(dentry_cache[i].valid && dentry_cache[i].inode_slot==slot)dentry_cache[i].valid=0;
        inode_cache[slot]=(struct inode_cache){mnt,info->ino,*info,1};
    }
    unsigned d=dentry_hand++%DENTRY_SLOTS;
    copy_path(dentry_cache[d].path,full);dentry_cache[d].inode_slot=slot;dentry_cache[d].valid=1;
    return 0;
}
/* Evaluate R/W/X with standard owner, group, other bits. Root bypasses
 * ordinary R/W checks, but executing still requires at least one X bit. */
static int permitted(const struct ramfs_info *info,unsigned mask) {
    if(cred_uid==0) return !(mask&1U) || (info->mode&0111U);
    unsigned bits=cred_uid==info->uid?(info->mode>>6):cred_gid==info->gid?(info->mode>>3):info->mode;
    return (bits&mask)==mask;
}
static int parent_permission(const char *full,unsigned mask) {
    char parent[256];copy_path(parent,full);
    unsigned n=0;while(parent[n])n++;
    while(n>1 && parent[n-1]!='/')n--;
    if(n>1)parent[n-1]=0;else {parent[0]='/';parent[1]=0;}
    struct ramfs_info info;
    return raw_stat(parent,&info)==0 && info.is_dir && permitted(&info,mask);
}
/* In a sticky directory (e.g. /tmp), removal and rename require root,
 * ownership of the directory, or ownership of the entry. Merely having
 * write permission on /tmp must not allow deleting another user's file. */
static int sticky_denied(const char *full,const struct ramfs_info *target) {
    char parent[256];copy_path(parent,full);
    unsigned n=0;while(parent[n])n++;
    while(n>1 && parent[n-1]!='/')n--;
    if(n>1)parent[n-1]=0;else {parent[0]='/';parent[1]=0;}
    struct ramfs_info dir;
    if(raw_stat(parent,&dir)<0)return 1;
    return (dir.mode&01000U) && cred_uid!=0 && cred_uid!=dir.uid && cred_uid!=target->uid;
}
/* Component-by-component traversal matters: /missing/../good must not
 * silently bypass a nonexistent directory; /file/../good is ENOTDIR.
 * A user-level relative pathname has already been joined to its CWD. */
enum final_link { FOLLOW, LINK_ITSELF, ENTRY };
static int walk_at(struct vfs_base base,const char *path,char *out,int allow_missing_leaf,int *exists,enum final_link policy) {
    char expanded[256];
    unsigned hops=0;
    if(exists)*exists=1;
    if(!path || !*path)return fail(K_ENOENT);
    char full[256];
    const char *p=path;
    size_t n;
    struct ramfs_info cur;
    int granted=base.search;
    if(*p=='/') {
root:
        full[0]='/';full[1]=0;
        granted=0;
    } else {
        if(!base.dir)return fail(K_EINVAL);
        copy_path(full,base.dir);
    }
    n=0;while(full[n])n++;
    int rc=raw_stat(full,&cur);if(rc<0)return fail(backend_errno(rc,K_ENOENT));
    while(*p) {
        while(*p=='/')p++;
        if(!*p)break;
        const char *start=p;while(*p && *p!='/')p++;
        size_t len=(size_t)(p-start);
        const char *next=p;while(*next=='/')next++;
        int last=!*next;
        if(!cur.is_dir)return fail(K_ENOTDIR);
        if(!granted && !permitted(&cur,1U))return fail(K_EACCES);
        if(len==1 && start[0]=='.')continue;
        if(len==2 && start[0]=='.' && start[1]=='.') {
            granted=0;
            if(n>1) {while(n>1 && full[n-1]!='/')n--;if(n>1)n--;full[n]=0;}
            rc=raw_stat(full,&cur);if(rc<0)return fail(backend_errno(rc,K_ENOENT));
            continue;
        }
        if(len>127 || n+(n>1?1U:0U)+len>=sizeof(full))return fail(K_ENAMETOOLONG);
        size_t parent_len=n;
        if(n>1)full[n++]='/';
        for(size_t i=0;i<len;i++)full[n++]=start[i];
        full[n]=0;
        int rc=raw_stat(full,&cur);
        if(rc==0 && (cur.mode&0170000)==0120000) {
            if(last && *p && policy==ENTRY)return fail(K_ENOTDIR);
            if(!last || *p || policy==FOLLOW) {
                if(++hops>40)return fail(K_ELOOP);
                char target[256], joined[256];
                const char *relative;
                const struct mount *mnt=resolve(full,&relative);
                if(!mnt || !mnt->ops->readlink)return fail(K_EIO);
                int bytes=mnt->ops->readlink(relative,target,sizeof target);
                if(bytes<0)return fail(backend_errno(bytes,K_EIO));
                if(!bytes || bytes>=(int)sizeof target)return fail(K_EIO);
                for(int i=0;i<bytes;i++)if(!target[i])return fail(K_EIO);
                size_t at=0;
                if(at+(size_t)bytes>=sizeof joined)return fail(K_ENAMETOOLONG);
                for(int i=0;i<bytes;i++)joined[at++]=target[i];
                while(*p) {
                    if(at+1>=sizeof joined)return fail(K_ENAMETOOLONG);
                    joined[at++]=*p++;
                }
                joined[at]=0;
                copy_path(expanded,joined);
                p=expanded;
                if(*p=='/')goto root;
                n=parent_len;full[n]=0;
                rc=raw_stat(full,&cur);if(rc<0)return fail(backend_errno(rc,K_ENOENT));
                continue;
            }
        }
        granted=0;
        if(rc<0) {
            if(rc<-1 && rc!=-K_ENOENT)return fail(-rc);
            if(last && allow_missing_leaf) {
                /* A trailing slash requires a directory: mkdir may create
                 * that missing directory, but open(O_CREAT) cannot create
                 * a regular file with a trailing slash. */
                if(*p && allow_missing_leaf!=2)return fail(K_ENOTDIR);
                copy_path(out,full);if(exists)*exists=0;return 0;
            }
            return fail(K_ENOENT);
        }
    }
    if(p[-1]=='/' && !cur.is_dir)return fail(K_ENOTDIR);
    copy_path(out,full);return 0;
}
static int walk(const char *path,char *out,int missing,int *exists,enum final_link policy) {
    return walk_at((struct vfs_base){0},path,out,missing,exists,policy);
}
static int checked(const char *path,char *out,int missing,int *exists) {
    return walk(path,out,missing,exists,FOLLOW);
}
int vfs_resolve_path(const char *path,char out[256]) {
    return checked(path,out,0,0);
}
int vfs_readlink_from(struct vfs_base base,const char *path,char *buf,size_t size) {
    char full[256];
    if(!buf&&size)return fail(K_EFAULT);
    if(walk_at(base,path,full,0,0,LINK_ITSELF)<0)return -1;
    const char *relative;
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->readlink)return fail(K_EINVAL);
    int rc=mnt->ops->readlink(relative,buf,size);
    return rc<0?fail(backend_errno(rc,K_EIO)):rc;
}
int vfs_readlink(const char *path,char *buf,size_t size) {
    return vfs_readlink_from((struct vfs_base){0},path,buf,size);
}
int vfs_symlink_from(struct vfs_base base,const char *target,const char *path) {
    char full[256];
    int exists;
    if(!target)return fail(K_EFAULT);
    if(walk_at(base,path,full,1,&exists,ENTRY)<0)return -1;
    if(exists)return fail(K_EEXIST);
    if(!parent_permission(full,2U))return fail(K_EACCES);
    const char *relative;
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || mnt->readonly)return fail(K_EROFS);
    if(!mnt->ops->symlink)return fail(K_ENOTSUP);
    int rc=mnt->ops->symlink(target,relative);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    cache_invalidate();
    return 0;
}
int vfs_symlink(const char *target,const char *path) {
    return vfs_symlink_from((struct vfs_base){0},target,path);
}
int vfs_mount_fat32(const char *path,int block_id) {
    if(cred_uid!=0)return fail(K_EACCES);
    char full[256];struct ramfs_info info;
    if(checked(path,full,0,0)<0)return -1;
    int rc=raw_stat(full,&info);if(rc<0)return fail(backend_errno(rc,K_ENOENT));
    if(!info.is_dir)return fail(K_ENOTDIR);
    if(fat_mount_path[0])return fail(K_EEXIST);
    if(fat32_vfs_attach(block_id)<0)return fail(K_EINVAL);
    copy_path(fat_mount_path,full);
    if(mount_fs(fat_mount_path,&fat_ops)<0){fat_mount_path[0]=0;return fail(K_ENOSPC);}
    cache_invalidate();last_errno=0;return 0;
}
static int slot(int fd) { return fd>=3 && fd<3+FD_COUNT && fds[fd-3].refs; }
/* Only a named kilofs filesystem image may be mounted. Physical disks are
 * always read-only. RW testing is restricted to block device 0 (RAM disk).
 * This does not add a userspace mount syscall. */
int vfs_mount_kilofs(const char *path,int block_id,int rw_test_ramdisk){
    char full[256];struct ramfs_info info;
    if(cred_uid!=0)return fail(K_EACCES);
    if(checked(path,full,0,0)<0)return -1;
    int rc=raw_stat(full,&info);if(rc<0)return fail(backend_errno(rc,K_ENOENT));
    if(!info.is_dir)return fail(K_ENOTDIR);
    if(kilofs_mount_path[0])return fail(K_EEXIST);
    if(rw_test_ramdisk && block_id!=0)return fail(K_EROFS);
    struct kfs_device dev;
    rc=kfs_bind_block(&dev,&diskctx,block_id,rw_test_ramdisk);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    rc=kfs_mount(&diskfs,dev,!rw_test_ramdisk);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    copy_path(kilofs_mount_path,full);
    if(mount_fs(kilofs_mount_path,&kfs_ops)<0){
        kilofs_mount_path[0]=0;
        diskfs.mounted=0;
        return fail(K_ENOSPC);
    }
    for(int i=0;i<MOUNT_COUNT;i++)if(mounts[i].active&&mounts[i].ops==&kfs_ops)
        mounts[i].readonly=!rw_test_ramdisk;
    cache_invalidate();last_errno=0;return 0;
}
/* Called once before userspace. Keep the module accessible at its original
 * mount, and retain the RAMFS /tmp subtree for volatile files. */
int vfs_use_boot_root(void) {
    struct kfs_info init,tmp;
    if(!diskfs.mounted || !diskfs.readonly ||
       kfs_stat(&diskfs,"sbin/init",&init)<0 || init.kind!=KFS_KIND_FILE ||
       !(init.mode&0111) || kfs_stat(&diskfs,"tmp",&tmp)<0 || tmp.kind!=KFS_KIND_DIR)
        return fail(K_EINVAL);
    if(mount_fs("/tmp",&ram_ops)<0)return fail(K_ENOSPC);
    for(unsigned i=0;i<MOUNT_COUNT;i++){
        if(!mounts[i].active)continue;
        if(same(mounts[i].path,"/tmp"))mounts[i].keep_prefix=1;
        if(same(mounts[i].path,"/")){
            mounts[i].ops=&kfs_ops;
            mounts[i].readonly=1;
        }
    }
    cache_invalidate();return 0;
}
int vfs_set_nonblock(int fd,int enabled) {
    if(!slot(fd))return fail(K_EBADF);
    fds[fd-3].nonblock=!!enabled;
    return 0;
}
int vfs_get_nonblock(int fd) {
    if(!slot(fd))return fail(K_EBADF);
    return (int)fds[fd-3].nonblock;
}
int vfs_set_append(int fd,int enabled){
    if(!slot(fd))return fail(K_EBADF);
    fds[fd-3].append=!!enabled;last_errno=0;return 0;
}
void vfs_set_sync(int fd,unsigned flags) {
    if(slot(fd))fds[fd-3].sync_flags=flags;
}
unsigned vfs_get_sync(int fd) {
    return slot(fd)?fds[fd-3].sync_flags:0;
}
int vfs_get_append(int fd){
    if(!slot(fd)){(void)fail(K_EBADF);return -1;}
    last_errno=0;return !!fds[fd-3].append;
}
int vfs_fsync(int fd){
    if(!slot(fd))return fail(K_EBADF);
    struct descriptor *d=&fds[fd-3];
    if(d->ops==&kfs_ops){int rc=kfs_sync(&diskfs);if(rc<0)return fail(backend_errno(rc,K_EIO));}
    /* Volatile RAMFS has no persistent device. Read-only disk has no dirtied data. */
    last_errno=0;return 0;
}

void vfs_init(void) {
    for(int i=0;i<FD_COUNT;i++) fds[i].refs=0;
    for(int i=0;i<MOUNT_COUNT;i++) mounts[i].active=0;
    inode_hand=0;dentry_hand=0;last_errno=0;fat_mount_path[0]=0;kilofs_mount_path[0]=0;cache_invalidate();vfs_set_credentials(0,0);
    /* Early root filesystem: one RAMFS instance with real directories. */
    (void)mount_fs("/", &ram_ops);
    /* ISO9660/ATAPI probe suspended; keep driver source for future work. */
}
int vfs_open(const char *path, unsigned flags, int create) {
    return vfs_open_mode(path,flags,create,0644U);
}
int vfs_open_mode_from(struct vfs_base base,const char *path,unsigned flags,int create,unsigned mode) {
    char full[256];const char *relative;int exists;struct ramfs_info info;
    unsigned exec=flags&VFS_EXEC;
    flags&=~VFS_EXEC;
    unsigned exclusive=flags&VFS_EXCLUSIVE;
    unsigned nofollow=flags&VFS_NOFOLLOW;
    unsigned directory=flags&VFS_DIRECTORY;
    flags&=~(VFS_EXCLUSIVE|VFS_NOFOLLOW|VFS_DIRECTORY);
    if(!flags || (flags & ~(VFS_READ|VFS_WRITE)))return fail(K_EINVAL);
    if(walk_at(base,path,full,create,&exists,exclusive?ENTRY:nofollow?LINK_ITSELF:FOLLOW)<0)return -1;
    if(exclusive && exists)return fail(K_EEXIST);
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt)return fail(K_ENOENT);
    if(exists) {
        int stat_rc=raw_stat(full,&info);
        if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
        if(nofollow && (info.mode&0170000U)==0120000U)return fail(K_ELOOP);
        if(exec && !directory && info.is_dir)return fail(K_EACCES);
        if(directory && !info.is_dir)return fail(K_ENOTDIR);
        if(info.is_dir && (flags&VFS_WRITE))return fail(K_EISDIR);
        unsigned required=exec?1U:(((flags&VFS_READ)?4U:0U)|((flags&VFS_WRITE)?2U:0U));
        if(!permitted(&info,required))return fail(K_EACCES);
    } else {
        if(!parent_permission(full,2U))return fail(K_EACCES);
    }
    if(((flags&VFS_WRITE) || (!exists && create)) && (!mnt->ops->write||mnt->readonly))return fail(K_EROFS);
    int free_slot=-1;for(int i=0;i<FD_COUNT;i++)if(!fds[i].refs){free_slot=i;break;}
    if(free_slot<0)return fail(K_ENFILE);
    if(exists && info.is_dir){
        struct descriptor *d=&fds[free_slot];
        *d=(struct descriptor){.refs=1,.flags=flags,.ops=mnt->ops,.directory=info};
        copy_path(d->path,full);last_errno=0;return free_slot+3;
    }
    if(!mnt->ops->open)return fail(K_EACCES);
    int backend=mnt->ops->open(relative,flags,create,mode&07777U);
    if(backend<0)return fail(backend<-1?-backend:(exists?K_EIO:K_ENOSPC));
    if(!exists) {
        int stat_rc=mnt->ops->fstat?mnt->ops->fstat(backend,&info):-K_EIO;
        if(stat_rc<0) {
            if(mnt->ops->close)(void)mnt->ops->close(backend);
            cache_invalidate();
            return fail(backend_errno(stat_rc,K_EIO));
        }
    }
    fds[free_slot]=(struct descriptor){.refs=1,.inode=info.ino,.backend_fd=backend,.flags=flags,.ops=mnt->ops};
    if(!exists)cache_invalidate();
    last_errno=0;return free_slot+3;
}
int vfs_open_mode(const char *path,unsigned flags,int create,unsigned mode) {
    return vfs_open_mode_from((struct vfs_base){0},path,flags,create,mode);
}
int vfs_lock_file(int fd,struct lock_file *file) {
    if(!slot(fd) || fds[fd-3].directory.is_dir)return 0;
    const struct descriptor *d=&fds[fd-3];
    /* Each backend currently owns one filesystem instance. Boot root can
     * expose the same kilofs instance through two different mounts. */
    *file=(struct lock_file){d->ops,d->inode};
    return 1;
}
int64_t vfs_offset(int fd) {
    return slot(fd)?(int64_t)fds[fd-3].offset:-K_EBADF;
}
int vfs_last_reference(int fd) {
    return slot(fd) && fds[fd-3].refs==1;
}
int vfs_retain(int fd) {
    if(!slot(fd))return fail(K_EBADF);
    if(fds[fd-3].refs==0xffffU)return fail(K_ENFILE);
    fds[fd-3].refs++;last_errno=0;return 0;
}
int vfs_close(int fd) {
    if(!slot(fd))return fail(K_EBADF);
    struct descriptor *d=&fds[fd-3];
    if(d->refs>1) {d->refs--;last_errno=0;return 0;}
    if(!d->directory.is_dir) {
        if(!d->ops->close)return fail(K_EIO);
        int rc=d->ops->close(d->backend_fd);
        if(rc<0)return fail(backend_errno(rc,K_EIO));
    }
    d->refs=0;last_errno=0;return 0;
}
size_t vfs_directory_record_size(int fd){
    return slot(fd) && fds[fd-3].directory.is_dir?sizeof(struct dirent):0;
}
/* Directory reads yield one complete public dirent. The open description
 * owns the cursor; dup and fork therefore share it. */
static int read_directory(struct descriptor *d,void *buf,size_t size){
    if(!size || !d->path[0])return 0;
    if(size<sizeof(struct dirent))return fail(K_EINVAL);
    const char *rel;
    const struct mount *m=resolve(d->path,&rel);
    if(!m || !m->ops->readdir)return fail(K_EIO);
    struct ramfs_dirent e;
    int rc=m->ops->readdir(rel,(unsigned)d->offset,&e);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    if(!rc)return 0;
    char child[256];
    size_t n=0;
    while(rel[n]){child[n]=rel[n];n++;}
    if(n && n+1<sizeof child)child[n++]='/';
    size_t j=0;
    while(e.name[j] && n+1<sizeof child)child[n++]=e.name[j++];
    if(e.name[j])return fail(K_ENAMETOOLONG);
    child[n]=0;
    struct ramfs_info info;
    rc=m->ops->stat(child,&info);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    struct dirent entry;
    for(j=0;j<sizeof entry;j++)((unsigned char *)&entry)[j]=0;
    entry.d_ino=info.ino;
    for(j=0;e.name[j];j++)entry.d_name[j]=e.name[j];
    for(j=0;j<sizeof entry;j++)((unsigned char *)buf)[j]=((unsigned char *)&entry)[j];
    d->offset++;return sizeof entry;
}
int vfs_read(int fd,void *buf,size_t n) {
    if(!slot(fd))return fail(K_EBADF);
    struct descriptor *d=&fds[fd-3];
    if(!(d->flags&VFS_READ))return fail(K_EBADF);
    if(!buf && n)return fail(K_EFAULT);
    if(d->directory.is_dir)return read_directory(d,buf,n);
    if(!d->ops->read)return fail(K_EISDIR);
    int rc=d->ops->read(d->backend_fd,buf,n);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    if(n)cache_invalidate();
    d->offset+=(size_t)rc;last_errno=0;return rc;
}
int vfs_write(int fd, const void *buf, size_t n) {
    if(!slot(fd))return fail(K_EBADF);
    struct descriptor *d=&fds[fd-3];
    if(!(d->flags&VFS_WRITE))return fail(K_EBADF);
    if(!buf && n)return fail(K_EFAULT);
    if(!d->ops->write)return fail(K_EROFS);
    if(!n){last_errno=0;return 0;}
    if(d->append){
        struct ramfs_info info;
        if(!d->ops->fstat || !d->ops->seek)return fail(K_EIO);
        int append_rc=d->ops->fstat(d->backend_fd,&info);
        if(append_rc<0)return fail(backend_errno(append_rc,K_EIO));
        append_rc=d->ops->seek(d->backend_fd,info.size);
        if(append_rc<0)return fail(backend_errno(append_rc,K_EIO));
        d->offset=info.size;
    }
    int rc=d->ops->write(d->backend_fd,buf,n);
    if(rc<0 || (!rc && n))return fail(rc<0?backend_errno(rc,K_ENOSPC):K_ENOSPC);
    d->offset+=(size_t)rc;
    if(rc)cache_invalidate();
    if(d->sync_flags && vfs_fsync(fd)<0)return -1;
    last_errno=0;return rc;
}
int vfs_seek(int fd, size_t offset) {
    if(!slot(fd))return fail(K_EBADF);
    struct descriptor *d=&fds[fd-3];
    if(d->directory.is_dir || !d->ops->seek)return fail(K_ESPIPE);
    int rc=d->ops->seek(d->backend_fd,offset);
    if(rc<0)return fail(backend_errno(rc,K_EINVAL));
    d->offset=offset;last_errno=0;return 0;
}
/* A mount and every directory containing it must remain reachable. */
static int is_mount_ancestor(const char *full) {
    for(int i=0;i<MOUNT_COUNT;i++)
        if(mounts[i].active && mount_match(mounts[i].path,full)) return 1;
    return 0;
}
int vfs_unlink_from(struct vfs_base base,const char *path) {
    char full[256];const char *relative;struct ramfs_info info;
    if(walk_at(base,path,full,0,0,ENTRY)<0)return -1;
    if(is_mount_ancestor(full))return fail(K_EACCES);
    int stat_rc=raw_stat(full,&info);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if(info.is_dir)return fail(K_EISDIR);
    if(!parent_permission(full,2U))return fail(K_EACCES);
    if(sticky_denied(full,&info))return fail(K_EPERM);
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->unlink||mnt->readonly)return fail(K_EROFS);
    int rc=mnt->ops->unlink(relative);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    cache_invalidate();last_errno=0;return 0;
}
int vfs_unlink(const char *path) {
    return vfs_unlink_from((struct vfs_base){0},path);
}
int vfs_mkdir(const char *path) {
    return vfs_mkdir_mode(path,0755U);
}
int vfs_mkdir_mode_from(struct vfs_base base,const char *path,unsigned mode) {
    char full[256];const char *relative;int exists;
    if(walk_at(base,path,full,2,&exists,ENTRY)<0)return -1;
    if(exists)return fail(K_EEXIST);
    if(!parent_permission(full,2U))return fail(K_EACCES);
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->mkdir||mnt->readonly)return fail(K_EROFS);
    int rc=mnt->ops->mkdir(relative,mode&07777U);
    if(rc<0)return fail(rc<-1?-rc:K_ENOSPC);
    cache_invalidate();last_errno=0;return 0;
}
int vfs_mkdir_mode(const char *path,unsigned mode) {
    return vfs_mkdir_mode_from((struct vfs_base){0},path,mode);
}
/* Preserve the terminal component before checked() collapses dot entries. */
static int dot_leaf(const char *path) {
    const char *end=path;
    while(*end)end++;
    while(end>path && end[-1]=='/')end--;
    const char *start=end;
    while(start>path && start[-1]!='/')start--;
    return (end-start==1 && start[0]=='.') ||
           (end-start==2 && start[0]=='.' && start[1]=='.');
}
/* Backend directories may immediately reuse the removed inode number. Keep
 * independent open descriptions tied to this particular removed object. */
static void detach_directory(const char *path,const struct ramfs_info *info) {
    static uint64_t generation;
    uint64_t id=++generation;
    for(unsigned i=0;i<FD_COUNT;i++) {
        struct descriptor *d=&fds[i];
        if(!d->refs || !d->directory.is_dir || !same(d->path,path))continue;
        d->path[0]=0;
        d->detached_id=id;
        d->directory=*info;
    }
}
int vfs_rmdir_from(struct vfs_base base,const char *path) {
    char full[256];const char *relative;struct ramfs_info info;
    if(walk_at(base,path,full,0,0,ENTRY)<0)return -1;
    if(dot_leaf(path))return fail(K_EINVAL);
    if(is_mount_ancestor(full))return fail(K_EACCES);
    int stat_rc=raw_stat(full,&info);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if(!info.is_dir)return fail(K_ENOTDIR);
    if(!parent_permission(full,2U))return fail(K_EACCES);
    if(sticky_denied(full,&info))return fail(K_EPERM);
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->rmdir||mnt->readonly)return fail(K_EROFS);
    int rc=mnt->ops->rmdir(relative);
    if(rc<0)return fail(backend_errno(rc,K_ENOTEMPTY));
    detach_directory(full,&info);
    cache_invalidate();last_errno=0;return 0;
}
int vfs_rmdir(const char *path) {
    return vfs_rmdir_from((struct vfs_base){0},path);
}
int vfs_link_from(struct vfs_base base,struct vfs_base destination,const char *source,const char *dest,int follow) {
    char oldpath[256],newpath[256];
    int exists;
    if(walk_at(base,source,oldpath,0,0,follow?FOLLOW:LINK_ITSELF)<0 ||
       walk_at(destination,dest,newpath,1,&exists,ENTRY)<0)return -1;
    if(exists)return fail(K_EEXIST);
    struct ramfs_info st;
    int stat_rc=raw_stat(oldpath,&st);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if(st.is_dir)return fail(K_EPERM);
    if(!parent_permission(newpath,2U))return fail(K_EACCES);
    const char *oldrel,*newrel;
    const struct mount *a=resolve(oldpath,&oldrel),*b=resolve(newpath,&newrel);
    if(!a || a!=b)return fail(K_EXDEV);
    if(a->readonly)return fail(K_EROFS);
    int rc;
    if(a->ops==&ram_ops)rc=ramfs_link(oldrel,newrel);
    else if(a->ops==&kfs_ops)rc=kfs_link(&diskfs,oldrel,newrel);
    else return fail(K_EROFS);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    cache_invalidate();
    return 0;
}
int vfs_link(const char *source,const char *dest,int follow) {
    return vfs_link_from((struct vfs_base){0},(struct vfs_base){0},source,dest,follow);
}
int vfs_rename_from(struct vfs_base base,struct vfs_base destination,const char *oldpath,const char *newpath) {
    char oldfull[256],newfull[256];const char *oldrel,*newrel;int exists;
    if(walk_at(base,oldpath,oldfull,0,0,ENTRY)<0 || walk_at(destination,newpath,newfull,1,&exists,ENTRY)<0)return -1;
    if(dot_leaf(oldpath) || dot_leaf(newpath))return fail(K_EINVAL);
    const struct mount *a=resolve(oldfull,&oldrel),*b=resolve(newfull,&newrel);
    if(!a || a!=b)return fail(K_EXDEV);
    if(!*oldrel || !*newrel)return fail(K_EACCES);
    for(int i=0;i<MOUNT_COUNT;i++)if(mounts[i].active &&
        (mount_match(mounts[i].path,oldfull) || mount_match(mounts[i].path,newfull)))return fail(K_EACCES);
    if(!parent_permission(oldfull,2U) || !parent_permission(newfull,2U))return fail(K_EACCES);
    struct ramfs_info source;
    int stat_rc=raw_stat(oldfull,&source);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if(sticky_denied(oldfull,&source))return fail(K_EPERM);
    struct ramfs_info dest;
    if(exists) {
        stat_rc=raw_stat(newfull,&dest);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
        if(sticky_denied(newfull,&dest))return fail(K_EPERM);
    }
    if(!a->ops->rename||a->readonly)return fail(K_EROFS);
    size_t oldlen=0,newlen=0;
    while(oldfull[oldlen])oldlen++;
    while(newfull[newlen])newlen++;
    for(unsigned i=0;i<FD_COUNT;i++){
        const struct descriptor *d=&fds[i];
        if(!d->refs || !d->directory.is_dir || !mount_match(d->path,oldfull))continue;
        size_t tail=0;while(d->path[oldlen+tail])tail++;
        if(newlen+tail>=sizeof d->path)return fail(K_ENAMETOOLONG);
    }
    int rc=a->ops->rename(oldrel,newrel);
    if(rc<0)return fail(rc<-1?-rc:(exists?K_EEXIST:K_EINVAL));
    if(exists && dest.is_dir && !same(oldfull,newfull))detach_directory(newfull,&dest);
    if(!same(oldfull,newfull))for(unsigned i=0;i<FD_COUNT;i++){
        struct descriptor *d=&fds[i];
        if(!d->refs || !d->directory.is_dir)continue;
        if(mount_match(d->path,oldfull)){
            char next[256];copy_path(next,newfull);
            size_t k=0;do{next[newlen+k]=d->path[oldlen+k];}while(d->path[oldlen+k++]);
            copy_path(d->path,next);
        }
    }
    cache_invalidate();last_errno=0;return 0;
}
int vfs_rename(const char *oldpath,const char *newpath) {
    return vfs_rename_from((struct vfs_base){0},(struct vfs_base){0},oldpath,newpath);
}
int vfs_truncate_from(struct vfs_base base,const char *path,size_t size) {
    char full[256];const char *relative;struct ramfs_info info;
    if(walk_at(base,path,full,0,0,FOLLOW)<0)return -1;
    int stat_rc=raw_stat(full,&info);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if(info.is_dir)return fail(K_EISDIR);
    if(!permitted(&info,2U))return fail(K_EACCES);
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->truncate||mnt->readonly)return fail(K_EROFS);
    int rc=mnt->ops->truncate(relative,size);
    if(rc<0)return fail(backend_errno(rc,K_EINVAL));
    cache_invalidate();last_errno=0;return 0;
}
int vfs_truncate(const char *path,size_t size) {
    return vfs_truncate_from((struct vfs_base){0},path,size);
}
int vfs_ftruncate(int fd, size_t size) {
    if(!slot(fd))return fail(K_EBADF);
    struct descriptor *d=&fds[fd-3];
    if(!(d->flags&VFS_WRITE))return fail(K_EBADF);
    if(!d->ops->ftruncate)return fail(K_EROFS);
    int rc=d->ops->ftruncate(d->backend_fd,size);
    if(rc<0)return fail(backend_errno(rc,K_EINVAL));
    cache_invalidate();last_errno=0;return 0;
}
const char *vfs_directory_path(int fd) {
    if(!slot(fd)){fail(K_EBADF);return NULL;}
    struct descriptor *d=&fds[fd-3];
    if(!d->directory.is_dir){fail(K_ENOTDIR);return NULL;}
    if(!d->path[0]){fail(K_ENOENT);return NULL;}
    return d->path;
}
/* The dispatcher serializes filesystem operations; restore the shared
 * open description before returning, including on short reads/errors. */
int vfs_exec_image(int fd,void *buffer,size_t capacity) {
    if(!slot(fd))return -K_EBADF;
    if(!buffer&&capacity)return -K_EFAULT;
    struct descriptor *d=&fds[fd-3];
    if(d->directory.is_dir || !(d->flags&VFS_READ))return -K_EACCES;
    struct ramfs_info info;
    if(!d->ops->fstat)return -K_EIO;
    int rc=d->ops->fstat(d->backend_fd,&info);
    if(rc<0)return -backend_errno(rc,K_EIO);
    if(!permitted(&info,1U))return -K_EACCES;
    if(!info.size || info.size>capacity)return -8;
    size_t saved=d->offset;
    if(vfs_seek(fd,0)<0)return -vfs_errno();
    size_t used=0;
    int error=0;
    while(used<info.size) {
        int count=vfs_read(fd,(char *)buffer+used,info.size-used);
        if(count<=0){error=count<0?-vfs_errno():-8;break;}
        used+=(size_t)count;
    }
    if(vfs_seek(fd,saved)<0)return -vfs_errno();
    return error?error:(int)used;
}
int vfs_fstat(int fd,struct vfs_info *info) {
    if(!slot(fd))return fail(K_EBADF);
    if(!info)return fail(K_EFAULT);
    struct descriptor *d=&fds[fd-3];
    if(d->directory.is_dir){
        struct ramfs_info entry=d->directory;
        if(d->path[0]){
            int stat_rc=raw_stat(d->path,&entry);
            if(stat_rc<0)return fail(backend_errno(stat_rc,K_EIO));
        }
        *info=(struct vfs_info){1,entry.size,entry.mode,entry.uid,entry.gid,entry.ino,d->path[0]?entry.links:0,d->ops->dev,entry.times,entry.blocks};
        return 0;
    }
    if(!d->ops->fstat)return fail(K_EBADF);
    struct ramfs_info result;
    int rc=d->ops->fstat(d->backend_fd,&result);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    *info=(struct vfs_info){result.is_dir,result.size,result.mode,result.uid,result.gid,result.ino,result.links,d->ops->dev,result.times,result.blocks};last_errno=0;return 0;
}
static int stat_resolved(const char *full,struct vfs_info *info) {
    struct ramfs_info backend;
    int stat_rc=raw_stat(full,&backend);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    const char *relative;
    const struct mount *mnt=resolve(full,&relative);
    *info=(struct vfs_info){backend.is_dir,backend.size,backend.mode,backend.uid,backend.gid,backend.ino,backend.links,mnt->ops->dev,backend.times,backend.blocks};
    last_errno=0;return 0;
}
int vfs_stat_from(struct vfs_base base,const char *path,struct vfs_info *info,int nofollow) {
    char full[256];
    if(!info)return fail(K_EFAULT);
    if(walk_at(base,path,full,0,0,nofollow?LINK_ITSELF:FOLLOW)<0)return -1;
    return stat_resolved(full,info);
}
int vfs_stat(const char *path,struct vfs_info *info) {return vfs_stat_from((struct vfs_base){0},path,info,0);}
int vfs_lstat(const char *path,struct vfs_info *info) {return vfs_stat_from((struct vfs_base){0},path,info,1);}
int vfs_readdir(const char *path,unsigned index,struct vfs_dirent *entry) {
    char full[256];const char *relative;struct ramfs_info info;
    if(!entry)return fail(K_EFAULT);
    if(checked(path,full,0,0)<0)return -1;
    int stat_rc=raw_stat(full,&info);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if(!info.is_dir)return fail(K_ENOTDIR);
    /* Reading directory entries requires R; pathname lookup uses X.
     * checked() has already enforced search on every ancestor. */
    if(!permitted(&info,4U))return fail(K_EACCES);
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->readdir)return fail(K_EACCES);
    struct ramfs_dirent backend;int rc=mnt->ops->readdir(relative,index,&backend);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    if(rc==1){size_t i=0;while(backend.name[i] && i<sizeof(entry->name)-1){entry->name[i]=backend.name[i];i++;}entry->name[i]=0;entry->is_dir=backend.is_dir;}
    last_errno=0;return rc;
}
int vfs_accessat_from(struct vfs_base base,const char *path,unsigned mask,int nofollow) {
    char full[256];struct ramfs_info info;
    if(mask&~7U)return fail(K_EINVAL);
    if(walk_at(base,path,full,0,0,nofollow?LINK_ITSELF:FOLLOW)<0)return -1;
    int stat_rc=raw_stat(full,&info);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if((mask&2U)) {
        const char *relative;const struct mount *mnt=resolve(full,&relative);
        if(!mnt || !mnt->ops->write||mnt->readonly)return fail(K_EROFS);
    }
    if(!permitted(&info,mask))return fail(K_EACCES);
    last_errno=0;return 0;
}
int vfs_accessat(const char *path,unsigned mask,int nofollow) {
    return vfs_accessat_from((struct vfs_base){0},path,mask,nofollow);
}
int vfs_access(const char *path,unsigned mask) {return vfs_accessat(path,mask,0);}
int vfs_faccess(int fd,unsigned mask) {
    struct vfs_info st;
    if(vfs_fstat(fd,&st)<0)return -1;
    struct ramfs_info info={st.is_dir,st.size,st.mode,st.uid,st.gid,st.ino,st.links,st.times,st.blocks};
    return permitted(&info,mask)?0:fail(K_EACCES);
}
static int chmod_resolved(const char *full,unsigned mode) {
    const char *relative;
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || mnt->readonly)return fail(K_EROFS);
    if(mnt->ops==&ram_ops){if(ramfs_chmod(relative,mode)<0)return fail(K_EPERM);}
    else if(mnt->ops==&kfs_ops){int rc=kfs_chmod(&diskfs,relative,mode);if(rc<0)return fail(backend_errno(rc,K_EIO));}
    else return fail(K_EROFS);
    cache_invalidate();last_errno=0;return 0;
}
int vfs_chmodat_from(struct vfs_base base,const char *path,unsigned mode,int nofollow) {
    char full[256];struct ramfs_info info;
    if(mode>07777U)return fail(K_EINVAL);
    if(walk_at(base,path,full,0,0,nofollow?LINK_ITSELF:FOLLOW)<0)return -1;
    int stat_rc=raw_stat(full,&info);if(stat_rc<0)return fail(backend_errno(stat_rc,K_ENOENT));
    if(cred_uid!=0 && cred_uid!=info.uid)return fail(K_EPERM);
    if(cred_uid && cred_gid!=info.gid)mode&=~02000U;
    if((info.mode&0170000U)==0120000U)return fail(K_ENOTSUP);
    return chmod_resolved(full,mode);
}
int vfs_chmodat(const char *path,unsigned mode,int nofollow) {
    return vfs_chmodat_from((struct vfs_base){0},path,mode,nofollow);
}
int vfs_chmod(const char *path,unsigned mode) {return vfs_chmodat(path,mode,0);}
int vfs_update_metadata_from(struct vfs_base base,const char *path,int fd,int nofollow,const struct inode_update *update) {
    struct vfs_info st;
    const struct fs_ops *ops;
    const char *relative=NULL;
    char full[256];
    int backend=-1;
    struct descriptor *d=NULL;
    if(!update)return fail(K_EFAULT);
    if(path) {
        if(walk_at(base,path,full,0,0,nofollow?LINK_ITSELF:FOLLOW)<0)return -1;
        if(stat_resolved(full,&st)<0)return -1;
        path=full;
    } else {
        if(vfs_fstat(fd,&st)<0)return -1;
        d=&fds[fd-3];
        if(st.is_dir && d->path[0])path=d->path;
    }
    struct inode_update resolved=*update;
    if(update->ownership) {
        if(resolved.uid==UINT32_MAX)resolved.uid=st.uid;
        if(resolved.gid==UINT32_MAX)resolved.gid=st.gid;
        if(cred_uid && (st.uid!=cred_uid || resolved.uid!=st.uid ||
           (resolved.gid!=st.gid && resolved.gid!=cred_gid)))return fail(K_EPERM);
        update=&resolved;
    } else if(!update->mask)return 0;
    else if(cred_uid && cred_uid!=st.uid) {
        if(update->explicit_time)return fail(K_EPERM);
        const struct ramfs_info info={.mode=st.mode,.uid=st.uid,.gid=st.gid};
        if(!permitted(&info,2))return fail(K_EACCES);
    }
    if(path) {
        const struct mount *mnt=resolve(path,&relative);
        if(!mnt || mnt->readonly)return fail(K_EROFS);
        ops=mnt->ops;
    } else if(d->detached_id) {
        for(unsigned i=0;i<FD_COUNT;i++)
            if(fds[i].refs && fds[i].detached_id==d->detached_id) {
                if(update->ownership){fds[i].directory.uid=update->uid;fds[i].directory.gid=update->gid;}
                change_times(&fds[i].directory.times,update);
            }
        return 0;
    } else {
        ops=d->ops;
        backend=d->backend_fd;
    }
    int rc;
    if(ops==&ram_ops)rc=ramfs_update_metadata(relative,backend,update);
    else if(ops==&kfs_ops)rc=kfs_update_metadata(&diskfs,relative,backend,update);
    else return fail(K_EROFS);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    cache_invalidate();
    return 0;
}
int vfs_update_metadata(const char *path,int fd,int nofollow,const struct inode_update *update) {
    return vfs_update_metadata_from((struct vfs_base){0},path,fd,nofollow,update);
}
int vfs_fchmod(int fd,unsigned mode) {
    struct vfs_info st;
    if(mode>07777U)return fail(K_EINVAL);
    if(vfs_fstat(fd,&st)<0)return -1;
    if(cred_uid!=0 && cred_uid!=st.uid)return fail(K_EPERM);
    if(cred_uid && cred_gid!=st.gid)mode&=~02000U;
    struct descriptor *d=&fds[fd-3];
    if(st.is_dir) {
        if(d->path[0])return chmod_resolved(d->path,mode);
        for(unsigned i=0;i<FD_COUNT;i++)
            if(fds[i].refs && fds[i].detached_id==d->detached_id)
                fds[i].directory.mode=mode;
        return 0;
    }
    int rc;
    if(d->ops==&ram_ops)rc=ramfs_fchmod(d->backend_fd,mode);
    else if(d->ops==&kfs_ops)rc=kfs_fchmod(&diskfs,d->backend_fd,mode);
    else return fail(K_EROFS);
    if(rc<0)return fail(backend_errno(rc,K_EIO));
    cache_invalidate();return 0;
}
int64_t vfs_lseek(int fd,int64_t offset,unsigned whence) {
    if(!slot(fd))return -(int64_t)K_EBADF;
    struct descriptor *d=&fds[fd-3];
    if(d->directory.is_dir) {
        /* Directory streams use the backend enumeration index as a cookie. */
        if(whence==1 && !offset)return (int64_t)d->offset;
        if(whence!=0 || offset<0 || (uint64_t)offset>UINT32_MAX)return -K_EINVAL;
        d->offset=(size_t)offset;
        return offset;
    }
    if(!d->ops->seek)return -(int64_t)K_ESPIPE;
    if(whence==3 || whence==4) {
        if(offset<0)return -K_EINVAL;
        struct vfs_info st;
        if(vfs_fstat(fd,&st)<0)return -vfs_errno();
        if((uint64_t)offset>st.size)return -K_ENXIO;
        if(whence==3 && (uint64_t)offset==st.size)return -K_ENXIO;
        /* Every byte before EOF may be reported as data. */
        if(whence==4)offset=(int64_t)st.size;
        whence=0;
    }
    uint64_t base=0;
    if(whence==1)base=d->offset;
    else if(whence==2) {
        struct ramfs_info info;
        if(!d->ops->fstat)return -(int64_t)K_EIO;
        int rc=d->ops->fstat(d->backend_fd,&info);
        if(rc<0)return -(int64_t)backend_errno(rc,K_EIO);
        base=info.size;
    } else if(whence!=0)return -(int64_t)K_EINVAL;
    /* Safe signed arithmetic for SEEK_SET/CUR/END and negative offsets. */
    if(base>0x7fffffffffffffffULL)return -(int64_t)K_EOVERFLOW;
    uint64_t target;
    if(offset<0) {
        uint64_t mag=(uint64_t)(-(offset+1))+1U;
        if(mag>base)return -(int64_t)K_EINVAL;
        target=base-mag;
    } else {
        if((uint64_t)offset>0x7fffffffffffffffULL-base)return -(int64_t)K_EOVERFLOW;
        target=base+(uint64_t)offset;
    }
    if(vfs_seek(fd,(size_t)target)<0)return -(int64_t)vfs_errno();
    return (int64_t)target;
}
int vfs_mount_info(unsigned index, struct vfs_mount_info *out) {
    if(!out) return -1;
    for(int i=0;i<MOUNT_COUNT;i++) if(mounts[i].active) {
        if(index) { index--; continue; }
        const char *p=mounts[i].path;
        size_t j=0;
        while(p[j] && j<sizeof(out->path)-1) { out->path[j]=p[j]; j++; }
        out->path[j]=0;
        out->read_only=mounts[i].readonly||mounts[i].ops->write==0;
        unsigned opened=0;
        for(int k=0;k<FD_COUNT;k++)
            if(fds[k].refs && fds[k].ops==mounts[i].ops) opened++;
        out->open_descriptors=opened;
        return 1;
    }
    return 0;
}
static unsigned vfs_failure_line;
unsigned vfs_selftest_failure_line(void) { return vfs_failure_line; }
int vfs_selftest(void) {
    vfs_failure_line=0;
#define TEST_FAIL() do { ok=0; if(!vfs_failure_line) vfs_failure_line=__LINE__; } while(0)
    static const char message[]="vfs check";
    char readback[sizeof(message)]={0};
    int fd=vfs_open("/tmp/./../tmp/.vfs-check",VFS_READ|VFS_WRITE,1);
    if(fd<3) return 0;
    int ok=vfs_write(fd,message,sizeof(message))==(int)sizeof(message)
        && vfs_seek(fd,0)==0
        && vfs_read(fd,readback,sizeof(readback))==(int)sizeof(readback);
    for(size_t i=0;i<sizeof(message);i++) if(readback[i]!=message[i]) TEST_FAIL();
    if(vfs_close(fd)!=0) TEST_FAIL();
    if(vfs_read(fd,readback,1)!=-1) TEST_FAIL();
    if(vfs_unlink("/tmp/.vfs-check")!=0) TEST_FAIL();
    if(vfs_open("/invalid",VFS_READ,0)!=-1) TEST_FAIL();
    if(vfs_open("/tmpish/no",VFS_READ,0)!=-1) TEST_FAIL();
    struct vfs_info info;
    struct vfs_dirent ent;
    if(vfs_stat("/",&info)!=0 || !info.is_dir) TEST_FAIL();
    if(vfs_stat("/tmp",&info)!=0 || !info.is_dir) TEST_FAIL();
    if(vfs_stat("/tmp/missing",&info)!=-1) TEST_FAIL();
    if(vfs_readdir("/",0,&ent)!=1 || !ent.is_dir) TEST_FAIL();
    if(vfs_readdir("/tmp/missing",0,&ent)!=-1) TEST_FAIL();
    if(vfs_mkdir("/tmp/vfs-dir")!=0) TEST_FAIL();
    if(vfs_mkdir("/tmp/vfs-dir/child")!=0) TEST_FAIL();
    if(vfs_rmdir("/tmp/vfs-dir")!=-1) TEST_FAIL();
    if(vfs_stat("/tmp/vfs-dir",&info)!=0 || !info.is_dir) TEST_FAIL();
    if(vfs_readdir("/tmp/vfs-dir",0,&ent)!=1 || !ent.is_dir) TEST_FAIL();
    int nested=vfs_open("/tmp/vfs-dir/child/nested",VFS_READ|VFS_WRITE,1);
    if(nested<3) TEST_FAIL();
    else {
        if(vfs_write(nested,message,sizeof(message))!=(int)sizeof(message)) TEST_FAIL();
        if(vfs_stat("/tmp/vfs-dir/child/nested",&info)!=0 || info.is_dir || info.size!=sizeof(message)) TEST_FAIL();
        if(vfs_readdir("/tmp/vfs-dir/child",0,&ent)!=1 || ent.is_dir || !same(ent.name,"nested")) TEST_FAIL();
        if(vfs_readdir("/tmp/vfs-dir/child",1,&ent)!=0) TEST_FAIL();
        if(vfs_unlink("/tmp/vfs-dir/child/nested")!=0) TEST_FAIL();
        if(vfs_open("/tmp/vfs-dir/child/nested",VFS_READ,0)!=-1) TEST_FAIL();
        if(vfs_seek(nested,0)!=0 || vfs_read(nested,readback,sizeof(readback))!=(int)sizeof(readback)) TEST_FAIL();
        for(size_t i=0;i<sizeof(message);i++) if(readback[i]!=message[i]) TEST_FAIL();
        if(vfs_close(nested)!=0) TEST_FAIL();
    }
    if(vfs_rmdir("/tmp/vfs-dir/child")!=0 || vfs_rmdir("/tmp/vfs-dir")!=0) TEST_FAIL();
    /* Access flags and invalid descriptors must be enforced by the VFS path. */
    int ro=vfs_open("/tmp/vfs-access",VFS_READ,1);
    if(ro<3) TEST_FAIL();
    else {
        if(vfs_write(ro,message,1)!=-1) TEST_FAIL();
        if(vfs_close(ro)!=0) TEST_FAIL();
    }
    int wo=vfs_open("/tmp/vfs-access",VFS_WRITE,0);
    if(wo<3) TEST_FAIL();
    else {
        if(vfs_read(wo,readback,1)!=-1) TEST_FAIL();
        if(vfs_close(wo)!=0) TEST_FAIL();
    }
    if(vfs_unlink("/tmp/vfs-access")!=0) TEST_FAIL();
    if(vfs_open("/tmp/vfs-missing/file",VFS_WRITE,1)!=-1) TEST_FAIL();
    if(vfs_close(2)!=-1 || vfs_read(2,readback,1)!=-1) TEST_FAIL();
    /* Rename files, directory trees and open handles without losing data. */
    if(vfs_mkdir("/tmp/vfs-move")!=0) TEST_FAIL();
    int moved=vfs_open("/tmp/vfs-move/before",VFS_READ|VFS_WRITE,1);
    if(moved<3) TEST_FAIL();
    else {
        if(vfs_write(moved,message,sizeof(message))!=(int)sizeof(message)) TEST_FAIL();
        if(vfs_rename("/tmp/vfs-move/before","/tmp/vfs-move/after")!=0) TEST_FAIL();
        if(vfs_stat("/tmp/vfs-move/before",&info)!=-1 || vfs_stat("/tmp/vfs-move/after",&info)!=0) TEST_FAIL();
        if(vfs_rename("/tmp/vfs-move","/tmp/vfs-moved")!=0) TEST_FAIL();
        if(vfs_stat("/tmp/vfs-moved/after",&info)!=0) TEST_FAIL();
        if(vfs_rename("/tmp/vfs-moved","/tmp/vfs-moved/child")!=-1) TEST_FAIL();
        if(vfs_rename("/tmp/vfs-moved/after","/dev/vfs-move")!=0) TEST_FAIL();
        if(vfs_seek(moved,0)!=0 || vfs_read(moved,readback,sizeof(readback))!=(int)sizeof(readback)) TEST_FAIL();
        if(vfs_close(moved)!=0) TEST_FAIL();
        if(vfs_unlink("/dev/vfs-move")!=0) TEST_FAIL();
    }
    if(vfs_rmdir("/tmp/vfs-moved")!=0) TEST_FAIL();
    /* Seeking beyond EOF must produce a zero-filled hole on write. */
    int sparse=vfs_open("/tmp/vfs-sparse",VFS_READ|VFS_WRITE,1);
    if(sparse<3) TEST_FAIL();
    else {
        char hole[8];
        if(vfs_seek(sparse,6)!=0 || vfs_write(sparse,"Z",1)!=1 || vfs_seek(sparse,0)!=0 || vfs_read(sparse,hole,7)!=7) TEST_FAIL();
        else { for(int i=0;i<6;i++) if(hole[i]!=0) TEST_FAIL(); if(hole[6]!='Z') TEST_FAIL(); }
        if(vfs_close(sparse)!=0) TEST_FAIL();
        if(vfs_unlink("/tmp/vfs-sparse")!=0) TEST_FAIL();
    }
    /* Truncation, zero-fill on extension, descriptor lifetime and bounds. */
    int truncfd=vfs_open("/tmp/vfs-truncate",VFS_READ|VFS_WRITE,1);
    if(truncfd<3) TEST_FAIL();
    else {
        char sample[8];
        if(vfs_write(truncfd,"abcdef",6)!=6) TEST_FAIL();
        if(vfs_truncate("/tmp/vfs-truncate",3)!=0) TEST_FAIL();
        if(vfs_stat("/tmp/vfs-truncate",&info)!=0 || info.size!=3) TEST_FAIL();
        if(vfs_ftruncate(truncfd,8)!=0) TEST_FAIL();
        if(vfs_seek(truncfd,0)!=0 || vfs_read(truncfd,sample,8)!=8) TEST_FAIL();
        if(sample[0]!='a'||sample[1]!='b'||sample[2]!='c') TEST_FAIL();
        for(int i=3;i<8;i++) if(sample[i]!=0) TEST_FAIL();
        if(vfs_ftruncate(truncfd,RAMFS_FILE_CAPACITY+1)!=-1) TEST_FAIL();
        if(vfs_stat("/tmp/vfs-truncate",&info)!=0 || info.size!=8) TEST_FAIL();
        if(vfs_unlink("/tmp/vfs-truncate")!=0) TEST_FAIL();
        if(vfs_ftruncate(truncfd,2)!=0) TEST_FAIL(); /* unlinked, still open */
        if(vfs_close(truncfd)!=0) TEST_FAIL();
    }
    if(vfs_truncate("/tmp/vfs-truncate",0)!=-1 || vfs_truncate("/tmp",0)!=-1) TEST_FAIL();
    int readonly=vfs_open("/tmp/vfs-readonly",VFS_READ,1);
    if(readonly<3) TEST_FAIL();
    else {
        if(vfs_ftruncate(readonly,0)!=-1) TEST_FAIL();
        if(vfs_close(readonly)!=0) TEST_FAIL();
        if(vfs_unlink("/tmp/vfs-readonly")!=0) TEST_FAIL();
    }
    /* Mount roots and ancestors cannot be removed even when empty. */
    /* /mnt is an ordinary RAMFS directory when ISO9660 is not mounted.
     * Never remove it just to test mount protection. */
    struct vfs_mount_info cd_mount;
    if(vfs_mount_info(1,&cd_mount)==1 && same(cd_mount.path,"/mnt/cdrom")) {
        if(vfs_rmdir("/mnt")!=-1 || vfs_unlink("/mnt")!=-1) TEST_FAIL();
        if(vfs_rmdir("/mnt/cdrom")!=-1 || vfs_unlink("/mnt/cdrom")!=-1) TEST_FAIL();
    } else if(vfs_stat("/mnt",&info)!=0 || !info.is_dir) TEST_FAIL();
    struct vfs_mount_info mount_info;
    if(vfs_mount_info(0,&mount_info)!=1 || !same(mount_info.path,"/") || mount_info.read_only) TEST_FAIL();
    if(vfs_mount_info(MOUNT_COUNT,&mount_info)!=0 || vfs_mount_info(0,0)!=-1) TEST_FAIL();
    /* Normalize repeated separators, dot components and parent traversal. */
    if(vfs_stat("/tmp//./../tmp",&info)!=0 || !info.is_dir) TEST_FAIL();
    if(vfs_open("relative",VFS_READ,0)!=-1) TEST_FAIL();
    if(vfs_open("/tmp/vfs-bad-flags",4U,1)!=-1) TEST_FAIL();

    if(vfs_rmdir("/")!=-1 || vfs_unlink("/")!=-1) TEST_FAIL();
    return ok;
#undef TEST_FAIL
}

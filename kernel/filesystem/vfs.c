#include <kilons/vfs.h>
#include <kilons/ramfs.h>
#include <kilons/iso9660.h>
/* Small mount-based kernel VFS. Mount selection and descriptors belong here;
 * backing filesystems implement the operations and know no absolute paths. */
#define FD_COUNT 64
#define MOUNT_COUNT 4
struct fs_ops {
    int (*open)(const char *, unsigned, int);
    int (*close)(int);
    int (*read)(int, void *, size_t);
    int (*write)(int, const void *, size_t);
    int (*seek)(int, size_t);
    int (*unlink)(const char *);
    int (*mkdir)(const char *);
    int (*rmdir)(const char *);
    int (*rename)(const char *, const char *);
    int (*truncate)(const char *, size_t);
    int (*ftruncate)(int, size_t);
    int (*fstat)(int, struct ramfs_info *);
    int (*stat)(const char *, struct ramfs_info *);
    int (*readdir)(const char *, unsigned, struct ramfs_dirent *);
};
struct mount { const char *path; const struct fs_ops *ops; int active; };
struct descriptor { unsigned refs; int backend_fd; unsigned flags; const struct fs_ops *ops; };
static struct mount mounts[MOUNT_COUNT];
static struct descriptor fds[FD_COUNT];
static const struct fs_ops ram_ops = {
    ramfs_open, ramfs_close, ramfs_read, ramfs_write, ramfs_seek, ramfs_unlink, ramfs_mkdir, ramfs_rmdir, ramfs_rename, ramfs_truncate, ramfs_ftruncate, ramfs_fstat, ramfs_stat, ramfs_readdir
};
static int iso_stat_adapter(const char *p,struct ramfs_info *out) {
    struct vfs_info v; if(iso9660_stat(p,&v)<0)return -1;
    out->is_dir=v.is_dir;out->size=v.size;return 0;
}
static int iso_dir_adapter(const char *p,unsigned i,struct ramfs_dirent *out) {
    struct vfs_dirent v;int rc=iso9660_readdir(p,i,&v);
    if(rc!=1)return rc;
    unsigned j=0;while(v.name[j]&&j<sizeof(out->name)-1){out->name[j]=v.name[j];j++;}
    out->name[j]=0;out->is_dir=v.is_dir;return 1;
}
static const struct fs_ops iso_ops __attribute__((unused))={
    iso9660_open,iso9660_close,iso9660_read,0,iso9660_seek,0,0,0,0,0,0,0,iso_stat_adapter,iso_dir_adapter
};
static int same(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
/* Canonicalize absolute paths; reject escaping above / and bounded overflow.
 * No symbolic links yet. The caller owns the returned buffer. */
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
    const char *p = path + longest;
    if (*p == '/') p++;
    *relative = p;
    return best;
}
static int mount_fs(const char *path, const struct fs_ops *ops) {
    if (!path || !ops || path[0] != '/') return -1;
    for (int i=0;i<MOUNT_COUNT;i++) if (mounts[i].active && same(mounts[i].path,path)) return -1;
    for (int i=0;i<MOUNT_COUNT;i++) if (!mounts[i].active) {
        mounts[i].path=path; mounts[i].ops=ops; mounts[i].active=1;
        return 0;
    }
    return -1;
}
static int slot(int fd) { return fd>=3 && fd<3+FD_COUNT && fds[fd-3].refs; }
void vfs_init(void) {
    for(int i=0;i<FD_COUNT;i++) fds[i].refs=0;
    for(int i=0;i<MOUNT_COUNT;i++) mounts[i].active=0;
    /* Early root filesystem: one RAMFS instance with real directories. */
    (void)mount_fs("/", &ram_ops);
    /* ISO9660/ATAPI probe suspended; keep driver source for future work. */
}
int vfs_open(const char *path, unsigned flags, int create) {
    char full[256]; const char *relative;
    if (canonical(path,full,sizeof(full))<0 || !flags || (flags & ~(VFS_READ|VFS_WRITE))) return -1;
    const struct mount *mnt=resolve(full,&relative);
    if (!mnt || !*relative) return -1;
    int free_slot=-1;
    for(int i=0;i<FD_COUNT;i++) if(!fds[i].refs) { free_slot=i; break; }
    if(free_slot<0) return -1;
    if(!mnt->ops->open) return -1;
    int backend=mnt->ops->open(relative,flags,create);
    if(backend<0) return -1;
    fds[free_slot].backend_fd=backend;
    fds[free_slot].flags=flags;
    fds[free_slot].ops=mnt->ops;
    fds[free_slot].refs=1;
    return free_slot+3;
}
int vfs_retain(int fd) {
    if(!slot(fd) || fds[fd-3].refs==0xffffU)return -1;
    fds[fd-3].refs++;return 0;
}
int vfs_close(int fd) {
    if(!slot(fd)) return -1;
    struct descriptor *d=&fds[fd-3];
    if(d->refs>1) {d->refs--;return 0;}
    int result=d->ops->close(d->backend_fd);
    if(result==0) d->refs=0;
    return result;
}
int vfs_read(int fd, void *buf, size_t n) {
    if (!slot(fd)) return -1;
    struct descriptor *d=&fds[fd-3];
    return (d->flags & VFS_READ) && (buf || !n) && d->ops->read ? d->ops->read(d->backend_fd,buf,n) : -1;
}
int vfs_write(int fd, const void *buf, size_t n) {
    if (!slot(fd)) return -1;
    struct descriptor *d=&fds[fd-3];
    return (d->flags & VFS_WRITE) && (buf || !n) && d->ops->write ? d->ops->write(d->backend_fd,buf,n) : -1;
}
int vfs_seek(int fd, size_t offset) {
    if (!slot(fd)) return -1;
    struct descriptor *d=&fds[fd-3];
    return d->ops->seek ? d->ops->seek(d->backend_fd,offset) : -1;
}
/* A mount and every directory containing it must remain reachable. */
static int is_mount_ancestor(const char *full) {
    for(int i=0;i<MOUNT_COUNT;i++)
        if(mounts[i].active && mount_match(mounts[i].path,full)) return 1;
    return 0;
}
int vfs_unlink(const char *path) {
    char full[256]; const char *relative;
    if(canonical(path,full,sizeof(full))<0) return -1;
    const struct mount *mnt=resolve(full,&relative);
    return !is_mount_ancestor(full) && mnt && *relative && mnt->ops->unlink ? mnt->ops->unlink(relative) : -1;
}
int vfs_mkdir(const char *path) {
    char full[256]; const char *relative;
    if(canonical(path,full,sizeof(full))<0) return -1;
    const struct mount *mnt=resolve(full,&relative);
    return !is_mount_ancestor(full) && mnt && *relative && mnt->ops->mkdir ? mnt->ops->mkdir(relative) : -1;
}
int vfs_rmdir(const char *path) {
    char full[256]; const char *relative;
    if(canonical(path,full,sizeof(full))<0) return -1;
    const struct mount *mnt=resolve(full,&relative);
    return !is_mount_ancestor(full) && mnt && *relative && mnt->ops->rmdir ? mnt->ops->rmdir(relative) : -1;
}
int vfs_rename(const char *oldpath, const char *newpath) {
    char oldfull[256],newfull[256];const char *oldrel,*newrel;
    if(canonical(oldpath,oldfull,sizeof(oldfull))<0 || canonical(newpath,newfull,sizeof(newfull))<0) return -1;
    const struct mount *a=resolve(oldfull,&oldrel),*b=resolve(newfull,&newrel);
    if(!a || a!=b || !*oldrel || !*newrel || !a->ops->rename) return -1;
    /* A mount point or its ancestor must not be renamed. */
    for(int i=0;i<MOUNT_COUNT;i++) if(mounts[i].active &&
        (mount_match(mounts[i].path,oldfull) || mount_match(mounts[i].path,newfull))) return -1;
    return a->ops->rename(oldrel,newrel);
}
/* Truncation is supported only by writable backing filesystems. */
int vfs_truncate(const char *path, size_t size) {
    char full[256]; const char *relative;
    if(canonical(path,full,sizeof(full))<0) return -1;
    const struct mount *mnt=resolve(full,&relative);
    return mnt && *relative && mnt->ops->truncate ? mnt->ops->truncate(relative,size) : -1;
}
int vfs_ftruncate(int fd, size_t size) {
    if(!slot(fd)) return -1;
    struct descriptor *d=&fds[fd-3];
    return (d->flags & VFS_WRITE) && d->ops->ftruncate ? d->ops->ftruncate(d->backend_fd,size) : -1;
}
int vfs_fstat(int fd,struct vfs_info *info) {
    if(!slot(fd) || !info)return -1;
    struct descriptor *d=&fds[fd-3];
    if(!d->ops->fstat)return -1;
    struct ramfs_info result;
    if(d->ops->fstat(d->backend_fd,&result)<0)return -1;
    info->is_dir=result.is_dir;info->size=result.size;return 0;
}
int vfs_stat(const char *path, struct vfs_info *info) {
    char full[256]; const char *relative;
    if(!info || canonical(path,full,sizeof(full))<0) return -1;
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->stat) return -1;
    struct ramfs_info backend;
    if(mnt->ops->stat(relative,&backend)<0) return -1;
    info->is_dir=backend.is_dir; info->size=backend.size;
    return 0;
}
int vfs_readdir(const char *path, unsigned index, struct vfs_dirent *entry) {
    char full[256]; const char *relative;
    if(!entry || canonical(path,full,sizeof(full))<0) return -1;
    const struct mount *mnt=resolve(full,&relative);
    if(!mnt || !mnt->ops->readdir) return -1;
    struct ramfs_dirent backend;
    int rc=mnt->ops->readdir(relative,index,&backend);
    if(rc!=1) return rc;
    size_t i=0;
    while(backend.name[i] && i<sizeof(entry->name)-1) { entry->name[i]=backend.name[i]; i++; }
    entry->name[i]=0; entry->is_dir=backend.is_dir;
    return 1;
}
int vfs_mount_info(unsigned index, struct vfs_mount_info *out) {
    if(!out) return -1;
    for(int i=0;i<MOUNT_COUNT;i++) if(mounts[i].active) {
        if(index) { index--; continue; }
        const char *p=mounts[i].path;
        size_t j=0;
        while(p[j] && j<sizeof(out->path)-1) { out->path[j]=p[j]; j++; }
        out->path[j]=0;
        out->read_only=mounts[i].ops->write==0;
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
    int fd=vfs_open("/tmp/./sub/../.vfs-check",VFS_READ|VFS_WRITE,1);
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

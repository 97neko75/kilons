#ifndef KILONS_KILOFS_H
#define KILONS_KILOFS_H
#include <stddef.h>
#include <stdint.h>
#define KFS_BLOCK 4096U
#define KFS_MAX_BLOCKS 32768U
#define KFS_INODES 128U
#define KFS_MAX_FILE_BLOCKS 16U
#define KFS_NAME_MAX 121U
#define KFS_HANDLES 48U
#define KFS_CACHE_SLOTS 8U
#define KFS_READ 1U
#define KFS_WRITE 2U
#define KFS_KIND_FILE 1U
#define KFS_KIND_DIR 2U
/* Each callback returns 0 on success, a negative errno on failure.
 * flush must establish durability for a writable mount. Never access a
 * user pointer from these callbacks; they run strictly in Ring 0. */
struct kfs_device {
    void *context;
    uint32_t total_blocks;
    int (*read)(void *, uint32_t, void *);
    int (*write)(void *, uint32_t, const void *);
    int (*flush)(void *);
};
struct kfs_node {
    uint16_t kind,mode;
    uint32_t uid,gid,links,parent;
    uint64_t size;
    uint32_t direct[KFS_MAX_FILE_BLOCKS];
};
struct kfs_info {
    uint32_t ino,uid,gid;
    uint16_t kind,mode;
    uint64_t size;
};
struct kfs_dirent {char name[KFS_NAME_MAX+1];uint32_t ino;uint8_t kind;};
struct kfs_cache_slot {uint32_t block;uint8_t data[KFS_BLOCK];uint8_t valid,dirty;};
struct kfs_handle {uint32_t ino;uint64_t offset;unsigned flags;unsigned used;};
struct kilofs {
    struct kfs_device dev;
    struct kfs_cache_slot cache[KFS_CACHE_SLOTS];
    struct kfs_handle handles[KFS_HANDLES];
    uint8_t scratch[KFS_BLOCK];
    uint8_t audit_blocks[KFS_BLOCK],audit_inodes[KFS_BLOCK],audit_seen[KFS_BLOCK];
    uint32_t cursor;
    unsigned mounted,readonly;
    uint32_t uid,gid;
};
int kfs_format(struct kilofs *,struct kfs_device);
int kfs_mount(struct kilofs *,struct kfs_device,int readonly);
int kfs_sync(struct kilofs *);
int kfs_fsck(struct kilofs *,unsigned *errors);
int kfs_open(struct kilofs *,const char *,unsigned,int create,unsigned mode);
int kfs_close(struct kilofs *,int);
int kfs_read(struct kilofs *,int,void *,size_t);
int kfs_write(struct kilofs *,int,const void *,size_t);
int kfs_seek(struct kilofs *,int,uint64_t);
int kfs_fstat(struct kilofs *,int,struct kfs_info *);
int kfs_stat(struct kilofs *,const char *,struct kfs_info *);
int kfs_readdir(struct kilofs *,const char *,unsigned,struct kfs_dirent *);
int kfs_mkdir(struct kilofs *,const char *,unsigned mode);
int kfs_rmdir(struct kilofs *,const char *);
int kfs_unlink(struct kilofs *,const char *);
int kfs_rename(struct kilofs *,const char *,const char *);
int kfs_truncate(struct kilofs *,const char *,uint64_t);
int kfs_ftruncate(struct kilofs *,int,uint64_t);
int kfs_chmod(struct kilofs *,const char *,unsigned);
#endif

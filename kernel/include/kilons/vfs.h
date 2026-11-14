#ifndef KILONS_VFS_H
#define KILONS_VFS_H
#include <stddef.h>
#include <kilons/file_time.h>
#include <stdint.h>
/* Kernel-only prototype. Descriptor numbers 0..2 are reserved for the
 * process-local terminal descriptors; Ring 3 syscalls use this layer. */
#define VFS_DIRECTORY 32U
#define VFS_NOFOLLOW 16U
#define VFS_EXEC 8U
#define VFS_EXCLUSIVE 4U
#define VFS_READ 1U
#define VFS_WRITE 2U
struct vfs_info { int is_dir; size_t size; unsigned mode; uint32_t uid,gid; uint64_t ino; unsigned links,dev; struct file_times times; unsigned blocks; };
struct vfs_base { const char *dir; int search; };
int vfs_stat_from(struct vfs_base,const char *,struct vfs_info *,int);
int vfs_readlink_from(struct vfs_base base,const char *path,char *buf,size_t size);
int vfs_symlink_from(struct vfs_base base,const char *target,const char *path);
int vfs_open_mode_from(struct vfs_base base,const char *path,unsigned flags,int create,unsigned mode);
int vfs_unlink_from(struct vfs_base base,const char *path);
int vfs_mkdir_mode_from(struct vfs_base base,const char *path,unsigned mode);
int vfs_rmdir_from(struct vfs_base base,const char *path);
int vfs_link_from(struct vfs_base base,struct vfs_base destination,const char *source,const char *dest,int follow);
int vfs_rename_from(struct vfs_base base,struct vfs_base destination,const char *oldpath,const char *newpath);
int vfs_accessat_from(struct vfs_base base,const char *path,unsigned mask,int nofollow);
int vfs_chmodat_from(struct vfs_base base,const char *path,unsigned mode,int nofollow);
int vfs_update_metadata_from(struct vfs_base base,const char *path,int fd,int nofollow,const struct inode_update *update);
struct vfs_dirent { char name[128]; int is_dir; };
struct lock_file;
int vfs_lock_file(int fd,struct lock_file *file);
int64_t vfs_offset(int fd);
int vfs_resolve_path(const char *,char [256]);
int vfs_lstat(const char *,struct vfs_info *);
int vfs_symlink(const char *,const char *);
int vfs_readlink(const char *,char *,size_t);
int vfs_stat(const char *path, struct vfs_info *info);
/* Last VFS failure, as a positive errno number (until next VFS operation). */
int vfs_errno(void);
void vfs_set_credentials(uint32_t uid,uint32_t gid);
int vfs_faccess(int,unsigned);
int vfs_accessat(const char *,unsigned,int);
int vfs_update_metadata(const char *,int,int,const struct inode_update *);
int vfs_fchmod(int,unsigned);
int vfs_chmodat(const char *,unsigned,int);
int vfs_chmod(const char *path,unsigned mode);
/* Mount a validated FAT32 block partition (read-only) at an existing directory. */
int vfs_mount_fat32(const char *path,int block_id);
int vfs_mount_kilofs(const char *path,int block_id,int rw_test_ramdisk);
int vfs_fsync(int fd);
int vfs_set_nonblock(int,int);
int vfs_get_nonblock(int);
void vfs_set_sync(int,unsigned);
unsigned vfs_get_sync(int);
int vfs_set_append(int fd,int enabled);
int vfs_get_append(int fd);
int vfs_access(const char *path,unsigned mask); /* mask: R=4 W=2 X=1 */
/* SEEK_SET(0), SEEK_CUR(1), SEEK_END(2): returns new offset or -errno. */
int64_t vfs_lseek(int fd,int64_t offset,unsigned whence);
const char *vfs_directory_path(int fd);
int vfs_exec_image(int fd,void *buffer,size_t capacity);
int vfs_fstat(int fd,struct vfs_info *info);
int vfs_normalize(const char *path,char *out,size_t capacity);
/* 1=entry, 0=end, -1=error */
int vfs_readdir(const char *path, unsigned index, struct vfs_dirent *entry);
void vfs_init(void);
int vfs_use_boot_root(void);
int vfs_open(const char *path, unsigned flags, int create);
int vfs_open_mode(const char *path,unsigned flags,int create,unsigned mode);
int vfs_mkdir_mode(const char *path,unsigned mode);
int vfs_close(int fd);
/* One ref per process-local descriptor; dup/fork share file offset. */
int vfs_last_reference(int fd);
int vfs_retain(int fd);
size_t vfs_directory_record_size(int fd);
int vfs_read(int fd, void *buffer, size_t count);
int vfs_write(int fd, const void *buffer, size_t count);
int vfs_seek(int fd, size_t offset);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_link(const char *,const char *,int);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_truncate_from(struct vfs_base,const char *,size_t);
int vfs_truncate(const char *path, size_t size);
int vfs_ftruncate(int fd, size_t size);
struct vfs_mount_info { char path[256]; int read_only; unsigned open_descriptors; };
/* 1=mount, 0=end, -1=invalid output */
int vfs_mount_info(unsigned index, struct vfs_mount_info *out);
int vfs_selftest(void);
/* First failing source line of the last VFS selftest, or zero on success. */
unsigned vfs_selftest_failure_line(void);
#endif

#ifndef KILONS_VFS_H
#define KILONS_VFS_H
#include <stddef.h>
#include <stdint.h>
/* Kernel-only prototype. Descriptor numbers 0..2 are reserved; no syscalls yet. */
#define VFS_READ 1U
#define VFS_WRITE 2U
struct vfs_info { int is_dir; size_t size; unsigned mode; uint32_t uid,gid; uint64_t ino; };
struct vfs_dirent { char name[128]; int is_dir; };
int vfs_stat(const char *path, struct vfs_info *info);
/* Last VFS failure, as a positive errno number (until next VFS operation). */
int vfs_errno(void);
void vfs_set_credentials(uint32_t uid,uint32_t gid);
int vfs_chmod(const char *path,unsigned mode);
/* Mount a validated FAT32 block partition (read-only) at an existing directory. */
int vfs_mount_fat32(const char *path,int block_id);
int vfs_mount_kilofs(const char *path,int block_id,int rw_test_ramdisk);
int vfs_fsync(int fd);
int vfs_set_append(int fd,int enabled);
int vfs_get_append(int fd);
int vfs_access(const char *path,unsigned mask); /* mask: R=4 W=2 X=1 */
/* SEEK_SET(0), SEEK_CUR(1), SEEK_END(2): returns new offset or -errno. */
int64_t vfs_lseek(int fd,int64_t offset,unsigned whence);
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
int vfs_retain(int fd);
size_t vfs_directory_record_size(int fd);
int vfs_read(int fd, void *buffer, size_t count);
int vfs_write(int fd, const void *buffer, size_t count);
int vfs_seek(int fd, size_t offset);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_truncate(const char *path, size_t size);
int vfs_ftruncate(int fd, size_t size);
struct vfs_mount_info { char path[256]; int read_only; unsigned open_descriptors; };
/* 1=mount, 0=end, -1=invalid output */
int vfs_mount_info(unsigned index, struct vfs_mount_info *out);
int vfs_selftest(void);
/* First failing source line of the last VFS selftest, or zero on success. */
unsigned vfs_selftest_failure_line(void);
#endif

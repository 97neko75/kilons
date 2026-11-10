#ifndef KILONS_VFS_H
#define KILONS_VFS_H
#include <stddef.h>
/* Kernel-only prototype. Descriptor numbers 0..2 are reserved; no syscalls yet. */
#define VFS_READ 1U
#define VFS_WRITE 2U
struct vfs_info { int is_dir; size_t size; };
struct vfs_dirent { char name[128]; int is_dir; };
int vfs_stat(const char *path, struct vfs_info *info);
int vfs_fstat(int fd,struct vfs_info *info);
int vfs_normalize(const char *path,char *out,size_t capacity);
/* 1=entry, 0=end, -1=error */
int vfs_readdir(const char *path, unsigned index, struct vfs_dirent *entry);
void vfs_init(void);
int vfs_open(const char *path, unsigned flags, int create);
int vfs_close(int fd);
/* One ref per process-local descriptor; dup/fork share file offset. */
int vfs_retain(int fd);
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

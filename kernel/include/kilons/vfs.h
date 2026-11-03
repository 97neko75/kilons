#ifndef KILONS_VFS_H
#define KILONS_VFS_H
#include <stddef.h>
/* Kernel-only prototype. Descriptor numbers 0..2 are reserved; no syscalls yet. */
#define VFS_READ 1U
#define VFS_WRITE 2U
void vfs_init(void);
int vfs_open(const char *path, unsigned flags, int create);
int vfs_close(int fd);
int vfs_read(int fd, void *buffer, size_t count);
int vfs_write(int fd, const void *buffer, size_t count);
int vfs_seek(int fd, size_t offset);
int vfs_unlink(const char *path);
int vfs_selftest(void);
#endif

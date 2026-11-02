#ifndef KILONS_RAMFS_H
#define KILONS_RAMFS_H
#include <stddef.h>
#include <stdint.h>
/* Experimental in-memory file API. Not a POSIX implementation yet. */
#define RAMFS_MAX_FILES 16
#define RAMFS_MAX_OPEN 16
#define RAMFS_FILE_CAPACITY 4096
#define RAMFS_READ 1
#define RAMFS_WRITE 2
void ramfs_init(void);
int ramfs_open(const char *name, unsigned flags, int create);
int ramfs_close(int fd);
int ramfs_read(int fd, void *buffer, size_t count);
int ramfs_write(int fd, const void *buffer, size_t count);
int ramfs_seek(int fd, size_t offset);
int ramfs_unlink(const char *name);
size_t ramfs_size(const char *name);
unsigned ramfs_file_count(void);
int ramfs_selftest(void);
#endif

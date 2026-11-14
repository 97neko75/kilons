#ifndef KILONS_RAMFS_H
#define KILONS_RAMFS_H
#include <stddef.h>
#include <kilons/file_time.h>
#include <stdint.h>
/* Experimental in-memory file API. Not a POSIX implementation yet. */
#define RAMFS_MAX_FILES 24
#define RAMFS_MAX_OPEN 48
#define RAMFS_FILE_CAPACITY 32768
#define RAMFS_READ 1
#define RAMFS_WRITE 2
struct ramfs_info { int is_dir; size_t size; unsigned mode; uint32_t uid,gid; uint64_t ino; unsigned links; struct file_times times; unsigned blocks; };
struct ramfs_dirent { char name[128]; int is_dir; };
int ramfs_stat(const char *name, struct ramfs_info *info);
void ramfs_set_identity(uint32_t uid,uint32_t gid);
int ramfs_fchmod(int,unsigned);
int ramfs_chmod(const char *name,unsigned mode);
/* 1=entry, 0=end, -1=error */
int ramfs_readdir(const char *name, unsigned index, struct ramfs_dirent *entry);
void ramfs_init(void);
int ramfs_mkdir(const char *name);
int ramfs_mkdir_mode(const char *name,unsigned mode);
int ramfs_open_mode(const char *name,unsigned flags,int create,unsigned mode);
int ramfs_rmdir(const char *name);
int ramfs_open(const char *name, unsigned flags, int create);
int ramfs_symlink(const char *,const char *);
int ramfs_readlink(const char *,char *,size_t);
int ramfs_close(int fd);
int ramfs_read(int fd, void *buffer, size_t count);
int ramfs_write(int fd, const void *buffer, size_t count);
int ramfs_seek(int fd, size_t offset);
int ramfs_update_metadata(const char *,int,const struct inode_update *);
int ramfs_link(const char *,const char *);
int ramfs_unlink(const char *name);
int ramfs_rename(const char *oldname, const char *newname);
int ramfs_truncate(const char *name, size_t size);
int ramfs_ftruncate(int fd, size_t size);
int ramfs_fstat(int fd,struct ramfs_info *out);
size_t ramfs_size(const char *name);
unsigned ramfs_file_count(void);
int ramfs_selftest(void);
#endif

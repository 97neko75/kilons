#ifndef KILONS_ISO9660_H
#define KILONS_ISO9660_H
#include <stddef.h>
#include <kilons/vfs.h>
/* Read-only ISO 9660 primary-volume reader, backed by ATAPI PIO. */
int iso9660_init(void);
int iso9660_available(void);
int iso9660_open(const char *, unsigned, int);
int iso9660_close(int);
int iso9660_read(int, void *, size_t);
int iso9660_seek(int, size_t);
int iso9660_stat(const char *, struct vfs_info *);
int iso9660_readdir(const char *, unsigned, struct vfs_dirent *);
#endif

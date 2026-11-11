#ifndef KILONS_FAT32_VFS_H
#define KILONS_FAT32_VFS_H
#include <stddef.h>
#include <kilons/ramfs.h>
/* Single-instance, read-only FAT32 8.3 filesystem over a 512-byte block device.
 * The block device may be an MBR primary partition. No disk-sector writes. */
int fat32_vfs_attach(int device);
int fat32_vfs_open(const char *,unsigned,int);
int fat32_vfs_close(int);
int fat32_vfs_read(int,void *,size_t);
int fat32_vfs_seek(int,size_t);
int fat32_vfs_stat(const char *,struct ramfs_info *);
int fat32_vfs_fstat(int,struct ramfs_info *);
int fat32_vfs_readdir(const char *,unsigned,struct ramfs_dirent *);
#endif

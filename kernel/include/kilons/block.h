#ifndef KILONS_BLOCK_H
#define KILONS_BLOCK_H
#include <stdbool.h>
#include <stdint.h>
#define BLOCK_SECTOR_SIZE 512U
/* Minimal one-sector block API. Drivers are registered independently of VFS.
 * Device 0 is a temporary test ramdisk. Hardware disks are read-only until
 * an explicitly configured filesystem and journaling layer are available. */
typedef bool (*block_read_fn)(void *ctx,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]);
typedef bool (*block_write_fn)(void *ctx,uint64_t lba,const uint8_t in[BLOCK_SECTOR_SIZE]);
struct block_device {uint64_t sectors;block_read_fn read;block_write_fn write;void *ctx;};
void block_init(void);
int block_register(const struct block_device *dev);
bool block_read(int id,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]);
bool block_write(int id,uint64_t lba,const uint8_t in[BLOCK_SECTOR_SIZE]);
bool block_info(int id,uint64_t *sectors,bool *writable);
bool block_selftest(void);
#endif

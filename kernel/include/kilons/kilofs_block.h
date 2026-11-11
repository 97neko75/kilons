#ifndef KILONS_KILOFS_BLOCK_H
#define KILONS_KILOFS_BLOCK_H
#include <kilons/kilofs.h>
struct kfs_block_context {int block_id;};
/* Convert existing 512-byte sectors into kilofs 4096-byte logical blocks.
 * rw=1 requires writable device; for hardware disks use dedicated test media.
 * Does not perform write operations unless kilofs explicitly requests them. */
int kfs_bind_block(struct kfs_device *out,struct kfs_block_context *ctx,
                   int block_id,int rw);
#endif

#ifndef KILONS_ATA_PIO_H
#define KILONS_ATA_PIO_H
#include <stdbool.h>
#include <stdint.h>
/* Explicit, bounded LBA28 read. No probe or hardware writes during boot. */
bool ata_pio_read28(uint32_t lba,uint8_t out[512]);
/* Explicit primary-master IDENTIFY; registers a read-only block device.
 * Returns a block id, or -1 when no supported PATA disk exists. */
int ata_pio_register_block(void);
#endif

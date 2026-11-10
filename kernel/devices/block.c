#include <kilons/block.h>
/* All bounds are enforced at the common layer. Never send writes to a
 * read-only disk. No implicit hardware probing in normal boot. */
#define DEVICES 4
#define RAM_SECTORS 128
static struct block_device devices[DEVICES];
static unsigned count;
static uint8_t ram_sectors[RAM_SECTORS][BLOCK_SECTOR_SIZE];
static bool ram_read(void *ctx,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]) {
    (void)ctx;
    for(unsigned i=0;i<BLOCK_SECTOR_SIZE;i++)out[i]=ram_sectors[lba][i];
    return true;
}
static bool ram_write(void *ctx,uint64_t lba,const uint8_t in[BLOCK_SECTOR_SIZE]) {
    (void)ctx;
    for(unsigned i=0;i<BLOCK_SECTOR_SIZE;i++)ram_sectors[lba][i]=in[i];
    return true;
}
void block_init(void) {
    count=0;
    struct block_device ram={RAM_SECTORS,ram_read,ram_write,0};
    (void)block_register(&ram);
}
int block_register(const struct block_device *dev) {
    if(!dev || !dev->sectors || !dev->read || count==DEVICES)return -1;
    devices[count]=*dev;return (int)count++;
}
bool block_read(int id,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]) {
    if(id<0 || (unsigned)id>=count || lba>=devices[id].sectors || !out)return false;
    return devices[id].read(devices[id].ctx,lba,out);
}
bool block_write(int id,uint64_t lba,const uint8_t in[BLOCK_SECTOR_SIZE]) {
    if(id<0 || (unsigned)id>=count || lba>=devices[id].sectors || !in || !devices[id].write)return false;
    return devices[id].write(devices[id].ctx,lba,in);
}
bool block_info(int id,uint64_t *sectors,bool *writable) {
    if(id<0 || (unsigned)id>=count || !sectors || !writable)return false;
    *sectors=devices[id].sectors;*writable=devices[id].write!=0;return true;
}
bool block_selftest(void) {
    uint8_t original[BLOCK_SECTOR_SIZE],test[BLOCK_SECTOR_SIZE],readback[BLOCK_SECTOR_SIZE];
    uint64_t sectors=0;bool writable=false;
    if(!block_info(0,&sectors,&writable) || sectors!=RAM_SECTORS || !writable ||
       !block_read(0,0,original))return false;
    for(unsigned i=0;i<BLOCK_SECTOR_SIZE;i++)test[i]=(uint8_t)(i^0xa5);
    bool ok=block_write(0,0,test) && block_read(0,0,readback);
    for(unsigned i=0;i<BLOCK_SECTOR_SIZE;i++)if(readback[i]!=test[i])ok=false;
    if(block_read(0,sectors,readback) || block_write(0,sectors,test))ok=false;
    if(!block_write(0,0,original))ok=false;
    return ok;
}

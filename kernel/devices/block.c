#include <kilons/block.h>
/* All bounds are enforced at the common layer. Never send writes to a
 * read-only disk. No implicit hardware probing in normal boot. */
#define DEVICES 12
#define RAM_SECTORS 512
static struct block_device devices[DEVICES];
static unsigned count;
static block_flush_fn flush_ops[DEVICES];
static void *flush_contexts[DEVICES];
static bool ram_flush(void *ctx){(void)ctx;return true;}
struct partition {int disk;uint64_t first;uint64_t sectors;};
static struct partition parts[4];
static unsigned part_count;
static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static bool partition_flush(void *ctx){struct partition *p=ctx;return block_flush(p->disk);}
static bool partition_read(void *ctx,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]) {
    struct partition *p=(struct partition *)ctx;
    return lba<p->sectors && block_read(p->disk,p->first+lba,out);
}

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
    count=0;part_count=0;
    for(unsigned i=0;i<DEVICES;i++){flush_ops[i]=0;flush_contexts[i]=0;}
    struct block_device ram={RAM_SECTORS,ram_read,ram_write,0};
    int ramid=block_register(&ram);
    if(ramid>=0)(void)block_set_flush(ramid,ram_flush,0);
}
int block_register(const struct block_device *dev) {
    if(!dev || !dev->sectors || !dev->read || count==DEVICES)return -1;
    devices[count]=*dev;flush_ops[count]=0;flush_contexts[count]=0;return (int)count++;
}
bool block_read(int id,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]) {
    if(id<0 || (unsigned)id>=count || lba>=devices[id].sectors || !out)return false;
    return devices[id].read(devices[id].ctx,lba,out);
}
bool block_write(int id,uint64_t lba,const uint8_t in[BLOCK_SECTOR_SIZE]) {
    if(id<0 || (unsigned)id>=count || lba>=devices[id].sectors || !in || !devices[id].write)return false;
    return devices[id].write(devices[id].ctx,lba,in);
}
bool block_set_flush(int id,block_flush_fn flush,void *ctx){
    if(id<0||(unsigned)id>=count||!flush)return false;
    flush_ops[id]=flush;flush_contexts[id]=ctx;return true;
}
bool block_flush(int id){
    if(id<0||(unsigned)id>=count||!flush_ops[id])return false;
    return flush_ops[id](flush_contexts[id]);
}
unsigned block_sector_size(int id){return id>=0&&(unsigned)id<count?BLOCK_SECTOR_SIZE:0;}
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

int block_register_mbr_partition(int disk,unsigned primary_index) {
    if(primary_index>=4 || part_count>=4)return -1;
    uint64_t capacity=0;bool writable=false;uint8_t sector[BLOCK_SECTOR_SIZE];
    if(!block_info(disk,&capacity,&writable) || !block_read(disk,0,sector))return -1;
    if(sector[510]!=0x55 || sector[511]!=0xaa)return -1;
    const uint8_t *e=sector+446+16*primary_index;
    if(e[0]!=0 && e[0]!=0x80)return -1;
    /* 0xEE is a protective GPT MBR, not a mountable primary partition. */
    if(e[4]==0 || e[4]==0xee || e[4]==0x05 || e[4]==0x0f)return -1;
    uint64_t start=rd32(e+8),size=rd32(e+12);
    if(!start || !size || start>=capacity || size>capacity-start)return -1;
    struct partition *p=&parts[part_count];
    *p=(struct partition){disk,start,size};
    struct block_device dev={size,partition_read,0,p};
    int id=block_register(&dev);
    if(id>=0){(void)block_set_flush(id,partition_flush,p);part_count++;}
    return id;
}

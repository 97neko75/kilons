#include <kilons/ata_pio.h>
#include <kilons/block.h>
#include <kilons/io.h>
#define ATA 0x1f0U
#define CTRL 0x3f6U
#define WAIT_LIMIT 200000U
static uint32_t identified_sectors;
static void settle(void){for(unsigned i=0;i<4;i++)(void)inb(CTRL);}
static int not_busy(void) {
    for(unsigned i=0;i<WAIT_LIMIT;i++){
        uint8_t s=inb(CTRL);
        if(!s || s==0xff)return 0;
        if(!(s&0x80U))return 1;
    }
    return 0;
}
static int data_ready(void){
    for(unsigned i=0;i<WAIT_LIMIT;i++){
        uint8_t s=inb(CTRL);
        if(!s || s==0xff)return 0;
        if(s&0x80U)continue;
        if(s&0x21U)return 0;
        if(s&0x08U)return 1;
    }
    return 0;
}
static int identify(uint32_t *sectors){
    outb(ATA+6,0xa0);settle();
    if(!not_busy())return 0;
    outb(ATA+2,0);outb(ATA+3,0);outb(ATA+4,0);outb(ATA+5,0);
    outb(ATA+7,0xec); /* IDENTIFY DEVICE (never writes disk sectors) */
    uint8_t status=inb(ATA+7);
    if(status==0 || status==0xff)return 0;
    if(inb(ATA+4)!=0 || inb(ATA+5)!=0)return 0; /* ATAPI, not ATA */
    if(!data_ready())return 0;
    uint16_t words[256];
    for(unsigned i=0;i<256;i++)words[i]=inw(ATA);
    if(!(words[49]&(1U<<9)))return 0; /* LBA supported */
    uint32_t n=(uint32_t)words[60]|((uint32_t)words[61]<<16);
    if(!n || n>0x10000000U)return 0;
    *sectors=n;return 1;
}
bool ata_pio_read28(uint32_t lba,uint8_t dst[512]) {
    if(!dst || lba>=0x10000000U)return false;
    if(identified_sectors && lba>=identified_sectors)return false;
    if(!not_busy())return false;
    outb(ATA+6,(uint8_t)(0xe0U|((lba>>24)&15U)));settle();
    outb(ATA+1,0);outb(ATA+2,1);
    outb(ATA+3,(uint8_t)lba);outb(ATA+4,(uint8_t)(lba>>8));outb(ATA+5,(uint8_t)(lba>>16));
    outb(ATA+7,0x20); /* READ SECTORS */
    if(!data_ready())return false;
    for(unsigned i=0;i<256;i++){uint16_t word=inw(ATA);dst[i*2]=(uint8_t)word;dst[i*2+1]=(uint8_t)(word>>8);}
    settle();return true;
}
static bool ata_block_read(void *ctx,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]) {
    (void)ctx;return lba<identified_sectors && ata_pio_read28((uint32_t)lba,out);
}
int ata_pio_register_block(void) {
    uint32_t sectors=0;identified_sectors=0;
    if(!identify(&sectors))return -1;
    identified_sectors=sectors;
    struct block_device disk={sectors,ata_block_read,0,0};
    int id=block_register(&disk);
    if(id<0)identified_sectors=0;
    return id;
}

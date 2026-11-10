#include <kilons/ata_pio.h>
#include <kilons/io.h>
#define ATA 0x1f0
static bool ready(void) {
    for(unsigned i=0;i<1000000;i++) {
        uint8_t status=inb(ATA+7);
        if(!status || status==0xff)return false;
        if(!(status&0x80)) {
            if(status&0x21)return false;
            if(status&0x08)return true;
        }
    }
    return false;
}
bool ata_pio_read28(uint32_t lba,uint8_t dst[512]) {
    if(!dst || lba>=0x10000000U)return false;
    outb(ATA+6,(uint8_t)(0xe0|((lba>>24)&15)));
    outb(ATA+2,1);
    outb(ATA+3,(uint8_t)lba);
    outb(ATA+4,(uint8_t)(lba>>8));
    outb(ATA+5,(uint8_t)(lba>>16));
    outb(ATA+7,0x20);
    if(!ready())return false;
    for(unsigned i=0;i<256;i++) {
        uint16_t value=inw(ATA);
        dst[i*2]=(uint8_t)value;dst[i*2+1]=(uint8_t)(value>>8);
    }
    return true;
}

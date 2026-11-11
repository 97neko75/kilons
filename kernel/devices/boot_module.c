#include <kilons/boot_module.h>
#include <kilons/block.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define PHYS_LIMIT (1ULL << 30)
#define MAX_MODULE_BYTES (128U * 1024U * 1024U)
struct image { const uint8_t *data; uint32_t bytes; };
static struct image rootfs;
static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static bool sector_read(void *ctx,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]){
    const struct image *im=ctx;
    if(lba>=im->bytes/BLOCK_SECTOR_SIZE)return false;
    const uint8_t *src=im->data+(size_t)lba*BLOCK_SECTOR_SIZE;
    for(unsigned i=0;i<BLOCK_SECTOR_SIZE;i++)out[i]=src[i];
    return true;
}
int boot_module_register_rootfs(uint32_t mbi_phys){
    if((mbi_phys&7U)||mbi_phys<4096||mbi_phys>=PHYS_LIMIT-8)return -1;
    const uint8_t *mbi=(const uint8_t *)(uintptr_t)mbi_phys;
    uint32_t total=read32(mbi);
    if(total<16||total>16U*1024U*1024U||(uint64_t)mbi_phys+total>PHYS_LIMIT)return -1;
    for(uint32_t off=8;off<=total-8;){
        const uint8_t *tag=mbi+off;
        uint32_t type=read32(tag),size=read32(tag+4);
        if(size<8||size>total-off)return -1;
        if(type==0)break;
        if(type==3&&size>=17){
            uint32_t start=read32(tag+8),end=read32(tag+12);
            uint32_t n=size-16;
            const char *name=(const char *)(tag+16);
            /* Exact command line: rootfs, including its terminating NUL. */
            if(n==7&&name[0]=='r'&&name[1]=='o'&&name[2]=='o'&&name[3]=='t'&&name[4]=='f'&&name[5]=='s'&&name[6]==0){
                if(end<=start||(uint64_t)end>PHYS_LIMIT||end-start>MAX_MODULE_BYTES||
                   ((end-start)%BLOCK_SECTOR_SIZE)!=0)return -1;
                rootfs.data=(const uint8_t *)(uintptr_t)start;
                rootfs.bytes=end-start;
                struct block_device dev={rootfs.bytes/BLOCK_SECTOR_SIZE,sector_read,0,&rootfs};
                return block_register(&dev);
            }
        }
        uint32_t step=(size+7U)&~7U;
        if(step>total-off)return -1;
        off+=step;
    }
    return -1;
}

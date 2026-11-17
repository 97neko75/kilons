#include <kilons/boot_module.h>
#include <kilons/block.h>
#include <kilons/limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define MAX_MODULE_BYTES (128U * 1024U * 1024U)
struct image { const uint8_t *data; uint64_t bytes; };
static struct image rootfs;
static bool sector_read(void *ctx,uint64_t lba,uint8_t out[BLOCK_SECTOR_SIZE]){
    const struct image *im=ctx;
    if(lba>=im->bytes/BLOCK_SECTOR_SIZE)return false;
    const uint8_t *src=im->data+(size_t)lba*BLOCK_SECTOR_SIZE;
    for(unsigned i=0;i<BLOCK_SECTOR_SIZE;i++)out[i]=src[i];
    return true;
}
static bool exact(const char *value,const char *expected) {
    if (!value) return false;
    while (*value && *expected && *value == *expected) {
        ++value;
        ++expected;
    }
    return *value == 0 && *expected == 0;
}

int boot_module_register_rootfs(void) {
    const struct limine_module_response *response=limine_modules();
    if(!response || response->module_count>1024 ||
       (response->module_count && !response->modules))return -1;
    for(uint64_t i=0;i<response->module_count;i++) {
        const struct limine_file *module=response->modules[i];
        if(!module || !exact(module->string,"rootfs"))continue;
        if(!module->address || !module->size || module->size>MAX_MODULE_BYTES ||
           module->size%BLOCK_SECTOR_SIZE)return -1;
        rootfs.data=(const uint8_t *)module->address;
        rootfs.bytes=module->size;
        struct block_device dev={rootfs.bytes/BLOCK_SECTOR_SIZE,sector_read,0,&rootfs};
        return block_register(&dev);
    }
    return -1;
}

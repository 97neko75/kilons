#ifndef KILONS_BOOT_MODULE_H
#define KILONS_BOOT_MODULE_H
#include <stdint.h>
/* Read-only block device backed by a reserved Multiboot2 module. */
int boot_module_register_rootfs(uint32_t mbi_phys);
#endif

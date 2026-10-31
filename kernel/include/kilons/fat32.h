#ifndef KILONS_FAT32_H
#define KILONS_FAT32_H
#include <stdint.h>
int fat32_mount(void);
void fat32_info(void);
void fat32_ls(void);
void fat32_cat(const char *name);
#endif

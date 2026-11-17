#ifndef KILONS_LIMINE_H
#define KILONS_LIMINE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Minimal Limine protocol surface used by kilons.  The definitions are kept
 * local so the kernel build does not depend on a host-installed bootloader
 * header.  The ISO still obtains the official Limine loader assets at build
 * time.
 */
#define LIMINE_REQUESTS_START_MARKER \
    { 0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL, \
      0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL }
#define LIMINE_REQUESTS_END_MARKER \
    { 0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL }
#define LIMINE_BASE_REVISION(N) \
    { 0xf9562b2d5c95a6c8ULL, 0x6a7b384944536bdcULL, (N) }
#define LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0)

#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL
#define LIMINE_HHDM_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852ULL, 0x63984e959a98244bULL }
#define LIMINE_MEMMAP_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL }
#define LIMINE_MODULE_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x3e7e279702be32afULL, 0xca1c4f3bd1280ceeULL }
#define LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0x71ba76863cc55f63ULL, 0xb2644a48c516a487ULL }
#define LIMINE_RSDP_REQUEST_ID \
    { LIMINE_COMMON_MAGIC, 0xc5e77b6b397e7b43ULL, 0x27637845accdcf3cULL }

#define LIMINE_MEMMAP_USABLE 0
#define LIMINE_MEMMAP_RESERVED 1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE 2
#define LIMINE_MEMMAP_ACPI_NVS 3
#define LIMINE_MEMMAP_BAD_MEMORY 4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER 7
#define LIMINE_MEMMAP_RESERVED_MAPPED 8

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};
struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};
struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};
struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

struct limine_file {
    uint64_t revision;
    void *address;
    uint64_t size;
    char *path;
    char *string;
    uint32_t media_type;
    uint32_t unused;
};
struct limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    struct limine_file **modules;
};
struct limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_module_response *response;
    uint64_t internal_module_count;
    void *internal_modules;
};

struct limine_executable_address_response {
    uint64_t revision;
    uint64_t physical_base;
    uint64_t virtual_base;
};
struct limine_executable_address_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_executable_address_response *response;
};

struct limine_rsdp_response {
    uint64_t revision;
    void *address;
};
struct limine_rsdp_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_rsdp_response *response;
};

struct limine_boot_info {
    const struct limine_memmap_response *memmap;
    uint64_t hhdm_offset;
    uint64_t kernel_phys_start;
    uint64_t kernel_phys_end;
};

bool limine_boot_info(struct limine_boot_info *info);
bool limine_base_revision_supported(void);
const struct limine_module_response *limine_modules(void);
const void *limine_rsdp_address(void);

#endif

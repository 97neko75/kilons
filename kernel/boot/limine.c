#include <kilons/limine.h>
#include <stdint.h>

__attribute__((used, aligned(8), section(".limine_requests_start")))
volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, aligned(8), section(".limine_requests")))
volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_hhdm_request limine_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_memmap_request limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_module_request limine_module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .internal_module_count = 0,
    .internal_modules = 0,
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_executable_address_request limine_executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_rsdp_request limine_rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, aligned(8), section(".limine_requests_end")))
volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

extern char __kernel_start[];
extern char __kernel_end[];

bool limine_base_revision_supported(void) {
    return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

bool limine_boot_info(struct limine_boot_info *info) {
    if (!info || !limine_base_revision_supported() ||
        !limine_hhdm_request.response || !limine_memmap_request.response ||
        !limine_memmap_request.response->entries ||
        !limine_executable_address_request.response) {
        return false;
    }
    uint64_t virtual_base = limine_executable_address_request.response->virtual_base;
    uint64_t physical_base = limine_executable_address_request.response->physical_base;
    uint64_t kernel_start = (uint64_t)(uintptr_t)__kernel_start;
    uint64_t kernel_end = (uint64_t)(uintptr_t)__kernel_end;
    if (kernel_end <= kernel_start || kernel_start < virtual_base ||
        kernel_end - virtual_base > UINT64_MAX - physical_base) {
        return false;
    }
    info->memmap = (const struct limine_memmap_response *)limine_memmap_request.response;
    info->hhdm_offset = limine_hhdm_request.response->offset;
    info->kernel_phys_start = physical_base + kernel_start - virtual_base;
    info->kernel_phys_end = physical_base + kernel_end - virtual_base;
    return info->kernel_phys_end > info->kernel_phys_start;
}

const struct limine_module_response *limine_modules(void) {
    return limine_module_request.response;
}

const void *limine_rsdp_address(void) {
    if (!limine_rsdp_request.response) return 0;
    return limine_rsdp_request.response->address;
}

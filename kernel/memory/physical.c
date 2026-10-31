#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kilons/memory.h>

/* The bootstrap identity maps only [0, 1 GiB). No frames above it are used. */
#define LIMIT (1ULL << 30)
#define FRAMES (LIMIT / PAGE_SIZE)
#define BITMAP_BYTES (FRAMES / 8)
#define MULTIBOOT_MMAP 6U
#define MULTIBOOT_MODULE 3U
#define MULTIBOOT_END 0U
#define MULTIBOOT_MAX_SIZE (16U * 1024U * 1024U)

extern char __kernel_end[];
static uint8_t usable[BITMAP_BYTES];
static uint8_t allocated[BITMAP_BYTES];
static uint32_t free_count;
static bool initialized;

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const uint8_t *p) { return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32); }
static bool bit(const uint8_t *b, uint32_t i) { return (b[i >> 3] & (1U << (i & 7))) != 0; }
static void set(uint8_t *b, uint32_t i) { b[i >> 3] |= (uint8_t)(1U << (i & 7)); }
static void clear(uint8_t *b, uint32_t i) { b[i >> 3] &= (uint8_t)~(1U << (i & 7)); }
static uint64_t align_up(uint64_t n) { return (n + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }
static uint64_t align_down(uint64_t n) { return n & ~(PAGE_SIZE - 1); }

/* Only fully contained frames can be used. */
static void mark_available(uint64_t start, uint64_t length) {
    if (start >= LIMIT || !length) return;
    uint64_t end = length > UINT64_MAX - start ? UINT64_MAX : start + length;
    if (end > LIMIT) end = LIMIT;
    start = align_up(start);
    end = align_down(end);
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE)
        set(usable, (uint32_t)(addr / PAGE_SIZE));
}
/* Exclude any frame overlapping a reserved range. */
static void mark_reserved(uint64_t start, uint64_t length) {
    if (start >= LIMIT || !length) return;
    uint64_t end = length > UINT64_MAX - start ? UINT64_MAX : start + length;
    if (end > LIMIT) end = LIMIT;
    start = align_down(start);
    end = align_up(end);
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE)
        clear(usable, (uint32_t)(addr / PAGE_SIZE));
}

bool memory_init(uint32_t mbi_phys) {
    initialized = false;
    free_count = 0;
    for (size_t i = 0; i < BITMAP_BYTES; ++i) { usable[i] = 0; allocated[i] = 0; }
    if ((mbi_phys & 7U) || mbi_phys < 4096 || mbi_phys >= LIMIT - 8) return false;
    const uint8_t *mbi = (const uint8_t *)(uintptr_t)mbi_phys;
    uint32_t total = u32(mbi);
    if (total < 16 || total > MULTIBOOT_MAX_SIZE || (uint64_t)mbi_phys + total > LIMIT) return false;
    bool found_map = false, found_end = false;
    uint32_t off = 8;
    while (off <= total - 8) {
        const uint8_t *tag = mbi + off;
        uint32_t type = u32(tag), size = u32(tag + 4);
        if (size < 8 || size > total - off) return false;
        if (type == MULTIBOOT_END) { if (size != 8) return false; found_end = true; break; }
        if (type == MULTIBOOT_MMAP) {
            if (size < 16) return false;
            uint32_t entry_size = u32(tag + 8);
            if (entry_size < 24 || (size - 16) % entry_size) return false;
            found_map = true;
            for (uint32_t pos = 16; pos < size; pos += entry_size) {
                const uint8_t *e = tag + pos;
                if (u32(e + 16) == 1) mark_available(u64(e), u64(e + 8));
            }
        }
        uint32_t advance = (size + 7U) & ~7U;
        if (advance > total - off) return false;
        off += advance;
    }
    if (!found_map || !found_end) return false;
    /* Multiboot memory map can contain overlapping reserved and usable ranges.
       A second pass makes reserved entries override available entries. */
    off = 8;
    while (off <= total - 8) {
        const uint8_t *tag = mbi + off;
        uint32_t type = u32(tag), size = u32(tag + 4);
        if (type == MULTIBOOT_END) break;
        if (type == MULTIBOOT_MODULE) {
            if (size < 16) return false;
            uint32_t start = u32(tag + 8), end = u32(tag + 12);
            if (end < start) return false;
            mark_reserved(start, (uint64_t)end - start);
        }
        if (type == MULTIBOOT_MMAP) {
            uint32_t entry_size = u32(tag + 8);
            for (uint32_t pos = 16; pos < size; pos += entry_size) {
                const uint8_t *e = tag + pos;
                if (u32(e + 16) != 1) mark_reserved(u64(e), u64(e + 8));
            }
        }
        off += (size + 7U) & ~7U;
    }
    /* The kernel is loaded at 1 MiB and includes boot page tables, stack,
       and allocator metadata in BSS. Keep all low memory unavailable. */
    mark_reserved(0, (uint64_t)(uintptr_t)__kernel_end);
    mark_reserved(mbi_phys, total);
    for (uint32_t i = 1; i < FRAMES; ++i) if (bit(usable, i)) ++free_count;
    initialized = true;
    return true;
}
uint64_t page_alloc(void) {
    if (!initialized || !free_count) return 0;
    for (uint32_t i = 1; i < FRAMES; ++i) {
        if (bit(usable, i) && !bit(allocated, i)) {
            set(allocated, i);
            --free_count;
            return (uint64_t)i * PAGE_SIZE;
        }
    }
    return 0;
}
bool page_free(uint64_t phys) {
    if (!initialized || (phys & (PAGE_SIZE - 1)) || phys >= LIMIT || !phys) return false;
    uint32_t i = (uint32_t)(phys / PAGE_SIZE);
    if (!bit(usable, i) || !bit(allocated, i)) return false;
    clear(allocated, i);
    ++free_count;
    return true;
}
uint32_t memory_free_pages(void) { return free_count; }

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kilons/memory.h>
#include <kilons/irq_guard.h>

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
static uint8_t mapped[BITMAP_BYTES];
static uint32_t free_count;
static uint32_t next_frame = 1;
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
    next_frame = 1;
    for (size_t i = 0; i < BITMAP_BYTES; ++i) {
        usable[i] = 0; allocated[i] = 0; mapped[i] = 0;
    }
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
#ifdef KILONS_HOST_TEST
    /* Hosted addresses are typically above 1 GiB; reserve synthetic low
     * boot memory rather than the host process's __kernel_end address. */
    mark_reserved(0, 2ULL * 1024 * 1024);
#else
    mark_reserved(0, (uint64_t)(uintptr_t)__kernel_end);
#endif
    mark_reserved(mbi_phys, total);
    for (uint32_t i = 1; i < FRAMES; ++i) if (bit(usable, i)) ++free_count;
    initialized = true;
    return true;
}
/* Interrupt-safe on one CPU. The scan cursor avoids repeatedly walking low
 * allocated memory; on wrap every usable frame is still considered. */
uint64_t page_alloc(void) {
    uint64_t flags = kilons_irq_save();
    if (!initialized || !free_count) {
        kilons_irq_restore(flags);
        return 0;
    }
    uint32_t start = next_frame;
    for (uint32_t scanned = 0; scanned < FRAMES - 1; ++scanned) {
        uint32_t i = start + scanned;
        if (i >= FRAMES) i -= FRAMES - 1;
        if (bit(usable, i) && !bit(allocated, i)) {
            set(allocated, i);
            --free_count;
            next_frame = i + 1;
            if (next_frame >= FRAMES) next_frame = 1;
            kilons_irq_restore(flags);
            return (uint64_t)i * PAGE_SIZE;
        }
    }
    /* Bitmap/count disagreement: fail closed instead of returning a frame
     * that might already be owned by somebody else. */
    kilons_irq_restore(flags);
    return 0;
}
bool page_free(uint64_t phys) {
    if ((phys & (PAGE_SIZE - 1)) || phys >= LIMIT || !phys) return false;
    uint64_t flags = kilons_irq_save();
    uint32_t i = (uint32_t)(phys / PAGE_SIZE);
    if (!initialized || !bit(usable, i) || !bit(allocated, i) || bit(mapped, i)) {
        kilons_irq_restore(flags);
        return false;
    }
    clear(allocated, i);
    ++free_count;
    kilons_irq_restore(flags);
    return true;
}
bool page_is_allocated(uint64_t phys) {
    if ((phys & (PAGE_SIZE - 1)) || phys >= LIMIT || !phys) return false;
    uint64_t flags = kilons_irq_save();
    bool owned = initialized && bit(allocated, (uint32_t)(phys / PAGE_SIZE));
    kilons_irq_restore(flags);
    return owned;
}
/* A physical frame may back at most one independently owned leaf mapping.
 * Page-table pages are allocated, but are not claimed as user leaf frames.
 * Holding the UP interrupt guard makes check-and-set indivisible on one CPU. */
bool page_claim_mapping(uint64_t phys) {
    if(!phys || (phys&(PAGE_SIZE-1)) || phys>=LIMIT)return false;
    uint64_t flags=kilons_irq_save();
    uint32_t i=(uint32_t)(phys/PAGE_SIZE);
    bool ok=initialized && bit(usable,i) && bit(allocated,i) && !bit(mapped,i);
    if(ok)set(mapped,i);
    kilons_irq_restore(flags);
    return ok;
}
bool page_release_mapping(uint64_t phys) {
    if(!phys || (phys&(PAGE_SIZE-1)) || phys>=LIMIT)return false;
    uint64_t flags=kilons_irq_save();
    uint32_t i=(uint32_t)(phys/PAGE_SIZE);
    bool ok=initialized && bit(allocated,i) && bit(mapped,i);
    if(ok)clear(mapped,i);
    kilons_irq_restore(flags);
    return ok;
}
bool page_is_mapped(uint64_t phys) {
    if(!phys || (phys&(PAGE_SIZE-1)) || phys>=LIMIT)return false;
    uint64_t flags=kilons_irq_save();
    uint32_t i=(uint32_t)(phys/PAGE_SIZE);
    bool result=initialized && bit(allocated,i) && bit(mapped,i);
    kilons_irq_restore(flags);
    return result;
}
/* Full bitmap invariant audit. Deliberately O(1 GiB / 4 KiB): call at
 * startup and in tests, not in the page allocation fast path. Checks that
 * no unowned/reserved frame was allocated or marked as mapped, and that the
 * authoritative free-frame counter agrees with the bitmap. */
bool memory_validate(void) {
    uint64_t flags=kilons_irq_save();
    if (!initialized || bit(usable,0) || bit(allocated,0) || bit(mapped,0)) {
        kilons_irq_restore(flags);
        return false;
    }
    uint32_t counted=0;
    bool ok=true;
    for (uint32_t i=1;i<FRAMES;i++) {
        bool allowed=bit(usable,i), owned=bit(allocated,i), claimed=bit(mapped,i);
        if ((owned && !allowed) || (claimed && !owned)) {ok=false;break;}
        if (allowed && !owned)counted++;
    }
    if (counted!=free_count)ok=false;
    kilons_irq_restore(flags);
    return ok;
}
uint32_t memory_free_pages(void) {
    uint64_t flags = kilons_irq_save();
    uint32_t count = free_count;
    kilons_irq_restore(flags);
    return count;
}

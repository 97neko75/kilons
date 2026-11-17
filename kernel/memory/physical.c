#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kilons/memory.h>
#include <kilons/limine.h>
#include <kilons/irq_guard.h>

/* The bootstrap identity maps only [0, 1 GiB). No frames above it are used. */
#define LIMIT (1ULL << 30)
#define FRAMES (LIMIT / PAGE_SIZE)
#define BITMAP_BYTES (FRAMES / 8)

static uint8_t usable[BITMAP_BYTES];
static uint8_t allocated[BITMAP_BYTES];
static uint8_t mapped[BITMAP_BYTES];
static uint32_t free_count;
static uint32_t next_frame = 1;
static bool initialized;
static uint64_t hhdm;

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

bool memory_init(const struct limine_memmap_response *memmap,
                 const struct limine_module_response *modules,
                 uint64_t kernel_phys_start, uint64_t kernel_phys_end,
                 uint64_t hhdm_offset) {
    initialized = false;
    hhdm = hhdm_offset;
    free_count = 0;
    next_frame = 1;
    for (size_t i = 0; i < BITMAP_BYTES; ++i) {
        usable[i] = 0; allocated[i] = 0; mapped[i] = 0;
    }
    if (!memmap || !memmap->entries || !memmap->entry_count ||
        memmap->entry_count > 4096 || kernel_phys_end < kernel_phys_start) {
        return false;
    }
    /* Limine supplies a flat array of entries.  First collect usable ranges,
     * then apply every non-usable range so overlaps fail closed. */
    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memmap->entries[i];
        if (!entry || !entry->length ||
            entry->base > UINT64_MAX - entry->length) return false;
        if (entry->type == LIMINE_MEMMAP_USABLE)
            mark_available(entry->base, entry->length);
    }
    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            mark_reserved(entry->base, entry->length);
    }
    if (kernel_phys_end > kernel_phys_start)
        mark_reserved(kernel_phys_start, kernel_phys_end - kernel_phys_start);
    if (modules) {
        if (modules->module_count > 1024 ||
            (modules->module_count && !modules->modules)) return false;
        for (uint64_t i = 0; i < modules->module_count; ++i) {
            const struct limine_file *module = modules->modules[i];
            if (!module || !module->address || !module->size) return false;
            uint64_t address = (uint64_t)(uintptr_t)module->address;
            if (address < hhdm) return false;
            mark_reserved(address - hhdm, module->size);
        }
    }
    /* Keep the zero page unavailable even if firmware reports it as usable. */
    mark_reserved(0, PAGE_SIZE);
    for (uint32_t i = 1; i < FRAMES; ++i) if (bit(usable, i)) ++free_count;
    initialized = true;
    return true;
}

uint64_t memory_hhdm_offset(void) { return hhdm; }

void *memory_phys_ptr(uint64_t phys) {
    if (phys > UINT64_MAX - hhdm) return NULL;
    return (void *)(uintptr_t)(phys + hhdm);
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

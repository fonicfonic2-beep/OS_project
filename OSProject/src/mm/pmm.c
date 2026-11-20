/*
 * Physical Memory Manager — bitmap-based page-frame allocator.
 *
 * Scans the multiboot2 memory map, builds one bit per 4 KiB frame in a
 * bitmap placed immediately after the kernel image, and hands out
 * physical frames.  All physical addresses managed here are clamped to
 * PMM_MAX_PHYS so that the direct map (KERNEL_VMA + phys) and the
 * kernel heap live in separate, non-overlapping address ranges.
 */
#include "pmm.h"
#include "common.h"
#include "string.h"

/* Linker symbols (virtual addresses) from linker.ld */
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

/*
 * Only manage the first PMM_MAX_PHYS bytes of RAM.  The direct map
 * starts at KERNEL_VMA (0xFFFFFFFF80000000) and the canonical high
 * half ends at 0xFFFFFFFFFFFFFFFF, so at most 2 GiB can be mapped
 * there without wrapping.  We cap at 2 GiB.
 */
#define PMM_MAX_PHYS   0x80000000ULL   /* 2 GiB */

static uint8_t*  bitmap;          /* virtual address of the bitmap      */
static uint32_t  nframes;         /* total frames tracked               */
static uint32_t  free_frames;     /* number of free frames              */
static uintptr_t bitmap_phys;     /* physical address of the bitmap     */

/* --- bit helpers ------------------------------------------------------- */
static inline void bit_set(uint32_t i)   { bitmap[i >> 3] |=  (uint8_t)(1u << (i & 7)); }
static inline void bit_clear(uint32_t i) { bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static inline int  bit_test(uint32_t i)  { return (bitmap[i >> 3] >> (i & 7)) & 1u; }

/* --- range helpers ----------------------------------------------------- */
/* Mark every frame that intersects [addr, addr+len) used. */
static void mark_range_used(uintptr_t addr, uint64_t len)
{
    if (len == 0) return;
    uint64_t end = addr + len;
    if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;
    uint32_t first = (uint32_t)(addr >> PAGE_SHIFT);
    uint32_t last  = (uint32_t)((end - 1) >> PAGE_SHIFT);
    if (first >= nframes) return;
    if (last >= nframes) last = nframes - 1;
    for (uint32_t i = first; i <= last; i++) {
        if (!bit_test(i)) free_frames--;
        bit_set(i);
    }
}

/* Mark every frame that intersects [addr, addr+len) free (available). */
static void mark_range_free(uintptr_t addr, uint64_t len)
{
    if (len == 0) return;
    uint64_t end = addr + len;
    if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;
    uint32_t first = (uint32_t)(addr >> PAGE_SHIFT);
    uint32_t last  = (uint32_t)((end - 1) >> PAGE_SHIFT);
    if (first >= nframes) return;
    if (last >= nframes) last = nframes - 1;
    for (uint32_t i = first; i <= last; i++) {
        if (bit_test(i)) free_frames++;
        bit_clear(i);
    }
}

/* --- multiboot2 tag iteration ------------------------------------------ */
static multiboot_tag_t* tag_first(multiboot_info_t* mbi)
{
    return (multiboot_tag_t*)((uint8_t*)mbi + sizeof(multiboot_info_t));
}

static multiboot_tag_t* tag_next(multiboot_tag_t* tag)
{
    uint32_t sz = (tag->size + 7u) & ~7u;   /* tags are 8-byte aligned */
    return (multiboot_tag_t*)((uint8_t*)tag + sz);
}

/*
 * pmm_init — scan multiboot2 tags, install the bitmap, reserve kernel /
 * boot structures / modules, and publish free RAM.
 */
void pmm_init(multiboot_info_t* mbi)
{
    multiboot_tag_mmap_t* mmap_tag = NULL;

    /* Pass 1: locate the memory-map tag and compute max physical address. */
    uint64_t max_phys = 0;
    for (multiboot_tag_t* t = tag_first(mbi); t->type != MULTIBOOT_TAG_TYPE_END; t = tag_next(t)) {
        if (t->type == MULTIBOOT_TAG_TYPE_MMAP) {
            mmap_tag = (multiboot_tag_mmap_t*)t;
            uint8_t* p = (uint8_t*)mmap_tag->entries;
            uint8_t* pend = (uint8_t*)mmap_tag + mmap_tag->size;
            while (p + sizeof(multiboot_mmap_entry_t) <= pend) {
                multiboot_mmap_entry_t* e = (multiboot_mmap_entry_t*)p;
                uint64_t end = e->addr + e->len;
                if (end > max_phys) max_phys = end;
                p += mmap_tag->entry_size;
            }
        }
    }
    if (max_phys == 0) max_phys = PMM_MAX_PHYS;
    if (max_phys > PMM_MAX_PHYS) max_phys = PMM_MAX_PHYS;

    nframes = (uint32_t)(max_phys >> PAGE_SHIFT);
    uint32_t bitmap_bytes = (nframes + 7u) / 8u;
    uint32_t bitmap_pages = (uint32_t)PAGE_ALIGN_UP(bitmap_bytes);

    /* Place the bitmap right after the kernel image (physical). */
    uintptr_t kstart_phys = (uintptr_t)_kernel_start - KERNEL_VBASE;
    uintptr_t kend_phys   = (uintptr_t)_kernel_end   - KERNEL_VBASE;
    bitmap_phys = (uintptr_t)PAGE_ALIGN_UP(kend_phys);
    bitmap = (uint8_t*)PHYS_TO_VIRT(bitmap_phys);

    /* Everything starts out used. */
    memset(bitmap, 0xFF, bitmap_bytes);
    free_frames = 0;

    /* Pass 2: mark available (type=1) regions free. */
    if (mmap_tag != NULL) {
        uint8_t* p = (uint8_t*)mmap_tag->entries;
        uint8_t* pend = (uint8_t*)mmap_tag + mmap_tag->size;
        while (p + sizeof(multiboot_mmap_entry_t) <= pend) {
            multiboot_mmap_entry_t* e = (multiboot_mmap_entry_t*)p;
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE)
                mark_range_free((uintptr_t)e->addr, e->len);
            p += mmap_tag->entry_size;
        }
    }

    /* Reserve low 1 MiB (BIOS data, VGA text buffer, real-mode ivt). */
    mark_range_used(0, 0x100000);

    /* Reserve the kernel image itself. */
    mark_range_used(kstart_phys, kend_phys - kstart_phys);

    /* Reserve the bitmap we just installed. */
    mark_range_used(bitmap_phys, (uint64_t)bitmap_pages << PAGE_SHIFT);

    /* Reserve the multiboot2 information structure. */
    uintptr_t mbi_phys = (uintptr_t)mbi - KERNEL_VBASE;
    mark_range_used(mbi_phys, mbi->total_size);

    /* Reserve every loaded module (initrd etc.). */
    for (multiboot_tag_t* t = tag_first(mbi); t->type != MULTIBOOT_TAG_TYPE_END; t = tag_next(t)) {
        if (t->type == MULTIBOOT_TAG_TYPE_MODULE) {
            multiboot_tag_module_t* mod = (multiboot_tag_module_t*)t;
            mark_range_used(mod->mod_start, (uint64_t)(mod->mod_end - mod->mod_start));
        }
    }
}

/* --- allocation -------------------------------------------------------- */
uintptr_t pmm_alloc_page(void)
{
    /* Scan whole bytes first for speed. */
    uint32_t full_bytes = nframes / 8u;
    for (uint32_t b = 0; b < full_bytes; b++) {
        if (bitmap[b] == 0xFF) continue;
        for (uint32_t bit = 0; bit < 8; bit++) {
            uint32_t frame = b * 8u + bit;
            if (!bit_test(frame)) {
                bit_set(frame);
                free_frames--;
                return (uintptr_t)frame << PAGE_SHIFT;
            }
        }
    }
    /* Remainder bits. */
    for (uint32_t frame = full_bytes * 8u; frame < nframes; frame++) {
        if (!bit_test(frame)) {
            bit_set(frame);
            free_frames--;
            return (uintptr_t)frame << PAGE_SHIFT;
        }
    }
    return 0;   /* out of memory (frame 0 is always reserved) */
}

void pmm_free_page(uintptr_t p)
{
    if (p == 0) return;
    uint32_t frame = (uint32_t)(p >> PAGE_SHIFT);
    if (frame >= nframes) return;
    if (bit_test(frame)) free_frames++;
    bit_clear(frame);
}

/* First-fit contiguous allocation of n frames.  Returns virtual pointer. */
void* pmm_alloc_pages(size_t n)
{
    if (n == 0) return NULL;
    if (n > nframes) return NULL;

    uint32_t run = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i < nframes; i++) {
        if (!bit_test(i)) {
            if (run == 0) start = i;
            run++;
            if (run == n) {
                for (uint32_t j = start; j < start + n; j++)
                    bit_set(j);
                free_frames -= (uint32_t)n;
                return (void*)PHYS_TO_VIRT((uintptr_t)start << PAGE_SHIFT);
            }
        } else {
            run = 0;
        }
    }
    return NULL;
}

void pmm_free_pages(uintptr_t p, size_t n)
{
    if (p == 0 || n == 0) return;
    /* Accept either a physical address or a direct-map virtual address. */
    if (p >= KERNEL_VBASE) p -= KERNEL_VBASE;
    uint32_t frame = (uint32_t)(p >> PAGE_SHIFT);
    for (size_t i = 0; i < n; i++) {
        uint32_t f = frame + (uint32_t)i;
        if (f >= nframes) break;
        if (bit_test(f)) free_frames++;
        bit_clear(f);
    }
}

uint32_t pmm_free_count(void)  { return free_frames; }
uint32_t pmm_total_count(void) { return nframes; }

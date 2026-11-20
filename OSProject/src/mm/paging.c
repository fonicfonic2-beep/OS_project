/*
 * x86_64 4-level paging manager.
 *
 * Builds a fresh kernel PML4 with:
 *   - identity map of low 4 MiB (boot code / BIOS data)
 *   - direct map of physical RAM (up to PHYSMAP_MAX) at KERNEL_VMA + phys
 *     using 2 MiB huge pages
 *   - VGA text buffer at 0xB8000
 *   - linear framebuffer (from multiboot2 tag), identity-mapped
 *   - local APIC page at 0xFEE00000, identity-mapped, cache-disabled
 * then switches CR3 to it.
 *
 * Framebuffer geometry is exported through the g_framebuffer_* globals
 * below so the VGA driver can pick them up via `extern` declarations.
 */
#include "paging.h"
#include "common.h"
#include "pmm.h"
#include "string.h"
#include "io.h"
#include "kernel.h"
#include "multiboot2.h"

/* Direct-map cap: KERNEL_VMA + 2 GiB reaches the top of canonical space. */
#define PHYSMAP_MAX   0x80000000ULL   /* 2 GiB */

uint64_t* kernel_pml4 = NULL;   /* virtual address of the kernel PML4 */

/* Framebuffer info exported for the VGA driver (vga.c). */
uint64_t g_framebuffer_phys   = 0;
uint64_t g_framebuffer_width  = 0;
uint64_t g_framebuffer_height = 0;
uint64_t g_framebuffer_pitch  = 0;
uint64_t g_framebuffer_bpp    = 0;

/* --- internal helpers -------------------------------------------------- */
/*
 * Walk one level: ensure the table referenced by parent[index] exists.
 * Returns the virtual address of that child table.
 */
static uint64_t* get_or_create(uint64_t* parent, int index, uint64_t flags)
{
    uint64_t entry = parent[index];
    if (!(entry & PAGE_PRESENT)) {
        uintptr_t phys = pmm_alloc_page();
        if (phys == 0) return NULL;
        memset((void*)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        parent[index] = phys | flags | PAGE_PRESENT;
        entry = parent[index];
    }
    return (uint64_t*)PHYS_TO_VIRT(entry & PAGING_ADDR_MASK);
}

/* Map a single 4 KiB page. */
void paging_map_page(uintptr_t virt, uintptr_t phys, uint64_t flags)
{
    if (kernel_pml4 == NULL) return;

    uint64_t* pdpt = get_or_create(kernel_pml4, PML4_INDEX(virt), PAGE_RW);
    if (pdpt == NULL) return;
    uint64_t* pd = get_or_create(pdpt, PDPT_INDEX(virt), PAGE_RW);
    if (pd == NULL) return;
    uint64_t* pt = get_or_create(pd, PD_INDEX(virt), PAGE_RW);
    if (pt == NULL) return;

    pt[PT_INDEX(virt)] = (phys & PAGING_ADDR_MASK) | (flags & PAGING_FLAGS_MASK) | PAGE_PRESENT;
    paging_invlpg(virt);
}

/* Map a 2 MiB huge page at PD level. */
static void map_huge_2mb(uintptr_t virt, uintptr_t phys, uint64_t flags)
{
    if (kernel_pml4 == NULL) return;
    uint64_t* pdpt = get_or_create(kernel_pml4, PML4_INDEX(virt), PAGE_RW);
    if (pdpt == NULL) return;
    uint64_t* pd = get_or_create(pdpt, PDPT_INDEX(virt), PAGE_RW);
    if (pd == NULL) return;
    pd[PD_INDEX(virt)] = (phys & PAGING_ADDR_MASK) | (flags & PAGING_FLAGS_MASK) | PAGE_PRESENT | PAGE_PS;
    paging_invlpg(virt);
}

void paging_unmap_page(uintptr_t virt)
{
    if (kernel_pml4 == NULL) return;
    uint64_t e = kernel_pml4[PML4_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    e = pdpt[PDPT_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return;
    if (e & PAGE_PS) {          /* 1 GiB huge page */
        pdpt[PDPT_INDEX(virt)] = 0;
        paging_invlpg(virt);
        return;
    }
    uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    e = pd[PD_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return;
    if (e & PAGE_PS) {          /* 2 MiB huge page */
        pd[PD_INDEX(virt)] = 0;
        paging_invlpg(virt);
        return;
    }
    uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    pt[PT_INDEX(virt)] = 0;
    paging_invlpg(virt);
}

uintptr_t paging_get_phys(uintptr_t virt)
{
    if (kernel_pml4 == NULL) return 0;
    uint64_t e = kernel_pml4[PML4_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    e = pdpt[PDPT_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    if (e & PAGE_PS)   /* 1 GiB page */
        return (e & ~0x3FFFFFFFULL) | (virt & 0x3FFFFFFFULL);
    uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    e = pd[PD_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    if (e & PAGE_PS)   /* 2 MiB page */
        return (e & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);
    uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    e = pt[PT_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    return (e & PAGING_ADDR_MASK) | (virt & 0xFFF);
}

bool paging_is_mapped(uintptr_t virt)
{
    if (kernel_pml4 == NULL) return false;
    uint64_t e = kernel_pml4[PML4_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    e = pdpt[PDPT_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return false;
    if (e & PAGE_PS) return true;
    uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    e = pd[PD_INDEX(virt)];
    if (!(e & PAGE_PRESENT)) return false;
    if (e & PAGE_PS) return true;
    uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
    return (pt[PT_INDEX(virt)] & PAGE_PRESENT) != 0;
}

void paging_switch_pml4(uintptr_t pml4_phys)
{
    write_cr3(pml4_phys & PAGING_ADDR_MASK);
}

void paging_invlpg(uintptr_t virt)
{
    invlpg(virt);
}

/* --- multiboot2 tag walk (local) --------------------------------------- */
static multiboot_tag_t* mb_tag_first(multiboot_info_t* mbi)
{
    return (multiboot_tag_t*)((uint8_t*)mbi + sizeof(multiboot_info_t));
}
static multiboot_tag_t* mb_tag_next(multiboot_tag_t* t)
{
    return (multiboot_tag_t*)((uint8_t*)t + ((t->size + 7u) & ~7u));
}

/* Identity-map a physical range [phys, phys+len) so virt == phys. */
static void identity_map_range(uintptr_t phys, uint64_t len, uint64_t flags)
{
    uintptr_t start = PAGE_ALIGN_DOWN(phys);
    uintptr_t end   = (uintptr_t)PAGE_ALIGN_UP(phys + len);
    for (uintptr_t p = start; p < end; p += PAGE_SIZE)
        paging_map_page(p, p, flags);
}

void paging_init(void)
{
    /* Allocate and zero the new kernel PML4. */
    uintptr_t pml4_phys = pmm_alloc_page();
    kernel_pml4 = (uint64_t*)PHYS_TO_VIRT(pml4_phys);
    memset(kernel_pml4, 0, PAGE_SIZE);

    /* 1. Identity-map low 4 MiB (boot code, BIOS data, real-mode ivt). */
    for (uintptr_t p = 0; p < 0x400000; p += PAGE_SIZE)
        paging_map_page(p, p, PAGE_PRESENT | PAGE_RW);

    /* 2. Direct-map physical memory at KERNEL_VMA + phys (2 MiB huge). */
    for (uintptr_t phys = 0; phys < PHYSMAP_MAX; phys += 0x200000)
        map_huge_2mb(KERNEL_VBASE + phys, phys, PAGE_PRESENT | PAGE_RW);

    /* 3. VGA text buffer (also covered by identity map, map explicitly). */
    identity_map_range(0xB8000, 0x8000, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

    /* 4. Framebuffer from multiboot2 tag + export geometry globals. */
    if (g_multiboot != NULL) {
        for (multiboot_tag_t* t = mb_tag_first(g_multiboot);
             t->type != MULTIBOOT_TAG_TYPE_END; t = mb_tag_next(t)) {
            if (t->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
                multiboot_tag_framebuffer_t* fb = (multiboot_tag_framebuffer_t*)t;
                g_framebuffer_phys   = (uint64_t)fb->framebuffer_addr;
                g_framebuffer_width  = fb->framebuffer_width;
                g_framebuffer_height = fb->framebuffer_height;
                g_framebuffer_pitch  = fb->framebuffer_pitch;
                g_framebuffer_bpp    = fb->framebuffer_bpp;
                uint64_t fb_size = (uint64_t)fb->framebuffer_pitch * fb->framebuffer_height;
                if (fb_size == 0)
                    fb_size = (uint64_t)fb->framebuffer_width * fb->framebuffer_height *
                              ((fb->framebuffer_bpp + 7u) / 8u);
                identity_map_range((uintptr_t)fb->framebuffer_addr, fb_size,
                                   PAGE_PRESENT | PAGE_RW | PAGE_PCD);
                break;
            }
        }
    }

    /* 5. Local APIC page (cache-disabled). */
    identity_map_range(0xFEE00000, PAGE_SIZE, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

    /* 6. Switch to the new address space. */
    paging_switch_pml4(pml4_phys);
}

/* --- user address spaces ----------------------------------------------- */
uintptr_t paging_create_user_pml4(void)
{
    uintptr_t phys = pmm_alloc_page();
    if (phys == 0) return 0;
    uint64_t* upml4 = (uint64_t*)PHYS_TO_VIRT(phys);
    memset(upml4, 0, PAGE_SIZE);

    /* Share the kernel half (PML4[511]) and the identity map (PML4[0]). */
    upml4[511] = kernel_pml4[511];
    upml4[0]   = kernel_pml4[0];
    return phys;
}

void paging_destroy_user_pml4(uintptr_t pml4_phys)
{
    if (pml4_phys == 0) return;
    uint64_t* upml4 = (uint64_t*)PHYS_TO_VIRT(pml4_phys);

    /* Skip index 0 (identity) and 511 (kernel half) — they are shared. */
    for (int i = 1; i < 511; i++) {
        uint64_t e = upml4[i];
        if (!(e & PAGE_PRESENT)) continue;
        uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(e & PAGING_ADDR_MASK);
        for (int j = 0; j < 512; j++) {
            uint64_t e2 = pdpt[j];
            if (!(e2 & PAGE_PRESENT)) continue;
            if (e2 & PAGE_PS) continue;   /* 1 GiB huge page — no table below */
            uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(e2 & PAGING_ADDR_MASK);
            for (int k = 0; k < 512; k++) {
                uint64_t e3 = pd[k];
                if (!(e3 & PAGE_PRESENT)) continue;
                if (e3 & PAGE_PS) continue;  /* 2 MiB huge page — no PT below */
                pmm_free_page(e3 & PAGING_ADDR_MASK);   /* free the PT page itself */
            }
            pmm_free_page(e2 & PAGING_ADDR_MASK);       /* free the PD page */
        }
        pmm_free_page(e & PAGING_ADDR_MASK);            /* free the PDPT page */
    }
    pmm_free_page(pml4_phys);
}

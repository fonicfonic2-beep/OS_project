#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "multiboot2.h"

/*
 * Physical Memory Manager — page-frame allocator.
 * Tracks free 4 KiB frames with a bitmap; pmm_init scans the
 * multiboot2 memory map and reserves kernel/modules/ACPI regions.
 */

void     pmm_init(multiboot_info_t* mbi);
uintptr_t pmm_alloc_page(void);
void     pmm_free_page(uintptr_t p);
uint32_t pmm_free_count(void);
uint32_t pmm_total_count(void);
void*    pmm_alloc_pages(size_t n);
void     pmm_free_pages(uintptr_t p, size_t n);

#endif /* PMM_H */

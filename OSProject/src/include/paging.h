#ifndef PAGING_H
#define PAGING_H

#include "types.h"

/* Page table entry flags (bits in PML4/PDPT/PD/PT entries) */
#define PAGE_PRESENT    1
#define PAGE_RW         2
#define PAGE_USER       4
#define PAGE_PWT        8
#define PAGE_PCD        16
#define PAGE_ACCESSED   32
#define PAGE_DIRTY      64
#define PAGE_PS         128   /* page size (2 MiB / 1 GiB) */
#define PAGE_GLOBAL     256
#define PAGE_NX         (1ULL << 63)

#define PAGING_FLAGS_MASK  0xFFFULL
#define PAGING_ADDR_MASK   (~0xFFFULL)

#define PML4_INDEX(v)  (((uintptr_t)(v) >> 39) & 0x1FF)
#define PDPT_INDEX(v)  (((uintptr_t)(v) >> 30) & 0x1FF)
#define PD_INDEX(v)    (((uintptr_t)(v) >> 21) & 0x1FF)
#define PT_INDEX(v)    (((uintptr_t)(v) >> 12) & 0x1FF)

void      paging_init(void);
void      paging_map_page(uintptr_t virt, uintptr_t phys, uint64_t flags);
void      paging_unmap_page(uintptr_t virt);
uintptr_t paging_get_phys(uintptr_t virt);
bool      paging_is_mapped(uintptr_t virt);
void      paging_switch_pml4(uintptr_t pml4_phys);
uintptr_t paging_create_user_pml4(void);
void      paging_destroy_user_pml4(uintptr_t pml4_phys);
void      paging_invlpg(uintptr_t virt);

extern uint64_t* kernel_pml4;   /* virtual address of kernel PML4 */

#endif /* PAGING_H */

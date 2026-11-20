#ifndef GDT_H
#define GDT_H

#include "types.h"

#define GDT_NULL_INDEX      0
#define GDT_KCODE_INDEX     1
#define GDT_KDATA_INDEX     2
#define GDT_UCODE_INDEX     3
#define GDT_UDATA_INDEX     4
#define GDT_TSS_INDEX       5
#define GDT_ENTRIES         7   /* null, kcode, kdata, ucode, udata, tss_lo, tss_hi */

#define GDT_SELECTOR(idx, rpl)  (((idx) << 3) | (rpl))

/* Access byte flags */
#define GDT_ACCESS_PRESENT  0x80
#define GDT_ACCESS_RING0    0x00
#define GDT_ACCESS_RING3    0x60
#define GDT_ACCESS_CODE     0x18  /* code, executable, readable */
#define GDT_ACCESS_DATA     0x12  /* data, writable */
#define GDT_ACCESS_TSS      0x89  /* 64-bit TSS available */

/* Granularity byte high nibble flags */
#define GDT_GRAN_4K         0x80
#define GDT_GRAN_32BIT      0x40
#define GDT_GRAN_64BIT      0x20

typedef struct PACKED gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;   /* limit_high (4 bits) | flags (4 bits) */
    uint8_t  base_high;
} gdt_entry_t;

typedef struct PACKED gdt_ptr {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

/* x86_64 Task State Segment (16-byte aligned in GDT via two gdt_entry_t) */
typedef struct PACKED tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss_entry_t;

void gdt_init(void);
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void tss_set_stack(uint64_t rsp0);

extern tss_entry_t tss;
extern gdt_entry_t gdt_entries[GDT_ENTRIES];
extern gdt_ptr_t   gdt_ptr;

#endif /* GDT_H */

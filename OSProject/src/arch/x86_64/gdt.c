/*
 * gdt.c — x86_64 Global Descriptor Table.
 *
 * Selector layout (frozen ABI, other modules depend on it):
 *   0x00  null
 *   0x08  kernel code 64-bit, DPL0
 *   0x10  kernel data 64-bit, DPL0
 *   0x18  user   code 64-bit, DPL3  (use with RPL3 -> 0x1B)
 *   0x20  user   data 64-bit, DPL3  (use with RPL3 -> 0x23)
 *   0x28  TSS64 (occupies two 8-byte slots: index 5 and 6)
 */
#include "gdt.h"
#include "io.h"
#include "string.h"

/* Defined in tss.c */
extern void tss_init(void);

gdt_entry_t gdt_entries[GDT_ENTRIES];
gdt_ptr_t   gdt_ptr;

/*
 * Set a normal (8-byte) GDT gate. base is 32-bit; for the 64-bit TSS
 * descriptor we write the two slots manually in gdt_init().
 */
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit,
                  uint8_t access, uint8_t gran)
{
    gdt_entries[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt_entries[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt_entries[num].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt_entries[num].access      = access;
    gdt_entries[num].granularity = (uint8_t)((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_entries[num].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

/* Reload segment registers after lgdt. CS is reloaded via lretq. */
static void gdt_flush(void)
{
    __asm__ volatile (
        "lgdt (%0)\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        :
        : "r" (&gdt_ptr)
        : "memory", "rax"
    );
}

/* Build the 16-byte TSS descriptor occupying GDT slots 5 and 6. */
static void gdt_set_tss(uint64_t base, uint32_t limit)
{
    /* Low 8 bytes (slot GDT_TSS_INDEX = 5) */
    gdt_entries[GDT_TSS_INDEX].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt_entries[GDT_TSS_INDEX].base_low    = (uint16_t)(base & 0xFFFF);
    gdt_entries[GDT_TSS_INDEX].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt_entries[GDT_TSS_INDEX].access      = GDT_ACCESS_TSS; /* 0x89, present, 64-bit TSS */
    gdt_entries[GDT_TSS_INDEX].granularity = (uint8_t)((limit >> 16) & 0x0F); /* G=0, byte granular */
    gdt_entries[GDT_TSS_INDEX].base_high   = (uint8_t)((base >> 24) & 0xFF);

    /* High 8 bytes (slot 6): base[63:32] + reserved zero */
    gdt_entries[GDT_TSS_INDEX + 1].limit_low   = (uint16_t)((base >> 32) & 0xFFFF);
    gdt_entries[GDT_TSS_INDEX + 1].base_low    = (uint16_t)((base >> 48) & 0xFFFF);
    gdt_entries[GDT_TSS_INDEX + 1].base_mid    = 0;
    gdt_entries[GDT_TSS_INDEX + 1].access      = 0;
    gdt_entries[GDT_TSS_INDEX + 1].granularity = 0;
    gdt_entries[GDT_TSS_INDEX + 1].base_high   = 0;
}

void gdt_init(void)
{
    memset(gdt_entries, 0, sizeof(gdt_entries));

    /* 0x00 null descriptor */
    gdt_set_gate(GDT_NULL_INDEX,  0, 0,       0,    0);

    /* 0x08 kernel code 64-bit, DPL0 */
    gdt_set_gate(GDT_KCODE_INDEX, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);

    /* 0x10 kernel data, DPL0 */
    gdt_set_gate(GDT_KDATA_INDEX, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_DATA,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* 0x18 user code 64-bit, DPL3 */
    gdt_set_gate(GDT_UCODE_INDEX, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);

    /* 0x20 user data, DPL3 */
    gdt_set_gate(GDT_UDATA_INDEX, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_DATA,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* 0x28 TSS64 — two slots, base = linear address of tss */
    gdt_set_tss((uint64_t)&tss, (uint32_t)(sizeof(tss_entry_t) - 1));

    gdt_ptr.limit = (uint16_t)(sizeof(gdt_entries) - 1);
    gdt_ptr.base  = (uint64_t)&gdt_entries;

    gdt_flush();

    /* Zero the TSS, set RSP0 to the current kernel stack, load TR. */
    tss_init();
}

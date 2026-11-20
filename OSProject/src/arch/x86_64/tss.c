/*
 * tss.c — x86_64 Task State Segment.
 *
 * Defines the `tss` object (declared extern in gdt.h), zeroes it,
 * sets RSP0, and loads the task register with selector 0x28.
 */
#include "gdt.h"
#include "io.h"
#include "string.h"

#define TSS_SELECTOR 0x28   /* GDT index 5, RPL0 */

/* 16-byte aligned as recommended by the SDM. */
tss_entry_t tss __attribute__((aligned(16)));

/* Read the current stack pointer (used as the default RSP0). */
static inline uint64_t read_rsp(void)
{
    uint64_t r;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(r));
    return r;
}

void tss_set_stack(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}

void tss_init(void)
{
    memset(&tss, 0, sizeof(tss));

    /* Default kernel stack for interrupts coming from ring 3. */
    tss.rsp0 = read_rsp();

    /* I/O permission bitmap base beyond the TSS limit = no IOPB. */
    tss.iomap_base = (uint16_t)sizeof(tss_entry_t);

    ltr(TSS_SELECTOR);
}

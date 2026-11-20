/*
 * apic.c — Local APIC and minimal IO APIC support.
 *
 * The local APIC's physical page (default 0xFEE00000, possibly
 * overridden by IA32_APIC_BASE) is mapped into the higher half via
 * paging_map_page with present+rw+cache-disable. If CPUID reports no
 * local APIC, we fall back to the legacy PIC (this file does nothing).
 *
 * IO APIC support is minimal: detect at the default physical address
 * 0xFEC00000 and expose redirection-table programming; routing is not
 * required by the spec.
 */
#include "apic.h"
#include "io.h"
#include "paging.h"
#include "common.h"
#include "console.h"

/* Higher-half virtual addresses chosen for MMIO mapping. PHYS_TO_VIRT
 * overflows for physical addresses >= 2 GiB, so we map explicitly. */
#define LAPIC_VIRT_BASE   0xFFFFFFFFFEE00000ULL
#define IOAPIC_VIRT_BASE  0xFFFFFFFFFEC00000ULL
#define IOAPIC_DEFAULT_PHYS 0xFEC00000ULL

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_ENABLE   (1ULL << 11)
#define APIC_BASE_BSP      (1ULL << 8)

static volatile uint32_t* lapic_base = NULL;
static bool               lapic_present = false;

static volatile uint32_t* ioapic_base = NULL;
static bool               ioapic_present = false;
static uint8_t            ioapic_max_entries = 0;

static inline void cpuid1(uint32_t* eax, uint32_t* ebx,
                          uint32_t* ecx, uint32_t* edx)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(1U));
}

static inline uint32_t lapic_read(uint32_t reg_off)
{
    return lapic_base[reg_off / 4];
}

static inline void lapic_write(uint32_t reg_off, uint32_t val)
{
    lapic_base[reg_off / 4] = val;
}

void apic_init(void)
{
    uint32_t eax, ebx, ecx, edx;
    uint64_t base_msr;
    uintptr_t phys;

    cpuid1(&eax, &ebx, &ecx, &edx);
    if (!(edx & (1U << 9))) {
        console_printf("APIC: not present, using legacy PIC.\n");
        lapic_present = false;
        return;
    }

    base_msr = rdmsr(IA32_APIC_BASE_MSR);
    phys = (uintptr_t)(base_msr & 0xFFFFF000ULL);
    if (!(base_msr & APIC_BASE_ENABLE)) {
        wrmsr(IA32_APIC_BASE_MSR, base_msr | APIC_BASE_ENABLE);
    }

    /* Map the 4 KiB local APIC page uncached into higher half. */
    paging_map_page(LAPIC_VIRT_BASE, phys,
                    PAGE_PRESENT | PAGE_RW | PAGE_PCD);
    lapic_base = (volatile uint32_t*)LAPIC_VIRT_BASE;
    lapic_present = true;

    /* Allow all interrupt priorities. */
    lapic_write(LAPIC_TPR, 0);

    /* Spurious vector register: enable APIC + spurious vector 0xFF. */
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | (LAPIC_SVR_VECTOR & 0xFF));

    /* Clear any stale interrupt. */
    lapic_write(LAPIC_EOI, 0);

    console_printf("APIC: enabled, id=%u, phys=%p\n",
                   (unsigned)apic_id(), (void*)phys);

    ioapic_init();
}

void apic_eoi(void)
{
    if (lapic_present) {
        lapic_write(LAPIC_EOI, 0);
    }
}

uint32_t apic_id(void)
{
    if (!lapic_present) return 0;
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

uint32_t apic_version(void)
{
    if (!lapic_present) return 0;
    return lapic_read(LAPIC_VERSION) & 0xFF;
}

void apic_send_ipi(uint32_t apic_id, uint32_t vector)
{
    if (!lapic_present) return;

    /* Wait for delivery status idle. */
    while (lapic_read(LAPIC_ICR_LO) & LAPIC_ICR_DELIVS) { }

    /* Destination field in high dword. */
    lapic_write(LAPIC_ICR_HI, (apic_id & 0xFF) << 24);

    /* Fixed delivery, edge-triggered, assert. */
    lapic_write(LAPIC_ICR_LO, (vector & 0xFF) | LAPIC_ICR_ASSERT);
}

/* ---------- minimal IO APIC ---------- */

static inline uint32_t ioapic_read(uint8_t reg)
{
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_WINDOW / 4];
}

static inline void ioapic_write(uint8_t reg, uint32_t val)
{
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_WINDOW / 4] = val;
}

void ioapic_init(void)
{
    uint32_t ver;

    paging_map_page(IOAPIC_VIRT_BASE, IOAPIC_DEFAULT_PHYS,
                    PAGE_PRESENT | PAGE_RW | PAGE_PCD);
    ioapic_base = (volatile uint32_t*)IOAPIC_VIRT_BASE;

    ver = ioapic_read(IOAPIC_VER);
    if (ver == 0xFFFFFFFFu || ver == 0) {
        ioapic_present = false;
        ioapic_base = NULL;
        console_printf("IOAPIC: not detected at default address.\n");
        return;
    }

    ioapic_max_entries = (uint8_t)(((ver >> 16) & 0xFF) + 1);
    ioapic_present = true;
    console_printf("IOAPIC: detected, version=%u, entries=%u\n",
                   (unsigned)(ver & 0xFF), (unsigned)ioapic_max_entries);
}

void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t apic_id, bool masked)
{
    uint32_t low, high;

    if (!ioapic_present) return;
    if (irq >= ioapic_max_entries) return;

    /* Fixed delivery, physical destination, edge-triggered. */
    low  = (uint32_t)(vector & 0xFF);
    if (masked) low |= 0x10000u;          /* mask bit */
    high = (uint32_t)((apic_id & 0xFF) << 24);

    ioapic_write(IOAPIC_REDTBL_LO(irq), low);
    ioapic_write(IOAPIC_REDTBL_HI(irq), high);
}

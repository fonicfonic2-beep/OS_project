#ifndef APIC_H
#define APIC_H

#include "types.h"

/* Default local APIC physical base (may be overridden by MSR IA32_APIC_BASE) */
#define LAPIC_DEFAULT_BASE   0xFEE00000

/* Local APIC register offsets (memory-mapped, 128-byte aligned stride) */
#define LAPIC_ID             0x020
#define LAPIC_VERSION        0x030
#define LAPIC_TPR            0x080
#define LAPIC_EOI            0x0B0
#define LAPIC_LDR            0x0D0
#define LAPIC_DFR            0x0E0
#define LAPIC_SVR            0x0F0
#define LAPIC_ISR_BASE       0x100
#define LAPIC_TMR_BASE       0x180
#define LAPIC_IRR_BASE       0x200
#define LAPIC_ESR            0x280
#define LAPIC_ICR_LO         0x300
#define LAPIC_ICR_HI         0x310
#define LAPIC_LVT_TIMER      0x320
#define LAPIC_LVT_THERMAL    0x330
#define LAPIC_LVT_PERF       0x340
#define LAPIC_LVT_LINT0      0x350
#define LAPIC_LVT_LINT1      0x360
#define LAPIC_LVT_ERROR      0x370
#define LAPIC_TIMER_INITCNT  0x380
#define LAPIC_TIMER_CURCNT   0x390
#define LAPIC_TIMER_DIV      0x3E0

/* Spurious interrupt vector register bits */
#define LAPIC_SVR_ENABLE     0x100
#define LAPIC_SVR_VECTOR     0xFF

/* ICR low bits */
#define LAPIC_ICR_INIT       0x500
#define LAPIC_ICR_STARTUP    0x600
#define LAPIC_ICR_ASSERT     0x4000
#define LAPIC_ICR_LEVELTRIG  0x8000
#define LAPIC_ICR_DELIVS     0x1000

/* IO APIC registers */
#define IOAPIC_REGSEL        0x00
#define IOAPIC_WINDOW        0x10
#define IOAPIC_ID            0x00
#define IOAPIC_VER           0x01
#define IOAPIC_ARB           0x02
#define IOAPIC_REDTBL_LO(n)  (0x10 + 2 * (n))
#define IOAPIC_REDTBL_HI(n)  (0x10 + 2 * (n) + 1)

void apic_init(void);
void apic_eoi(void);
uint32_t apic_id(void);
uint32_t apic_version(void);
void apic_send_ipi(uint32_t apic_id, uint32_t vector);

/* Minimal IO APIC support */
void ioapic_init(void);
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t apic_id, bool masked);

#endif /* APIC_H */

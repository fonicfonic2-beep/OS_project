/*
 * idt.c — x86_64 Interrupt Descriptor Table (256 x 16-byte gates).
 *
 * Vectors:
 *   0x00..0x1F  CPU exceptions (isr0..isr31)
 *   0x20..0x2F  PIC IRQs remapped (irq0..irq15)
 *   0x80        syscall software interrupt (DPL3, int 0x80)
 */
#include "idt.h"
#include "isr.h"
#include "irq.h"
#include "io.h"
#include "string.h"

/* Defined in isr.asm */
extern void syscall_isr(void);

#define KERNEL_CODE_SEL 0x08

idt_entry_t idt_entries[IDT_ENTRIES];
idt_ptr_t   idt_ptr;

void idt_set_gate(uint8_t vec, uint64_t handler, uint8_t type_attr, uint8_t dpl)
{
    idt_entries[vec].base_low  = (uint16_t)(handler & 0xFFFF);
    idt_entries[vec].selector  = KERNEL_CODE_SEL;
    idt_entries[vec].ist       = IDT_IST_NONE;
    /* Preserve P (bit7) and type (bits3:0), force bit4=0, set DPL from arg. */
    idt_entries[vec].type_attr = (uint8_t)((type_attr & 0x9F) | ((dpl & 3) << 5));
    idt_entries[vec].base_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt_entries[vec].base_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt_entries[vec].reserved  = 0;
}

void idt_init(void)
{
    memset(idt_entries, 0, sizeof(idt_entries));

    /* 32 CPU exception gates (ring 0, interrupt gates) */
    idt_set_gate(0,  (uint64_t)isr0,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(1,  (uint64_t)isr1,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(2,  (uint64_t)isr2,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(3,  (uint64_t)isr3,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(4,  (uint64_t)isr4,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(5,  (uint64_t)isr5,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(6,  (uint64_t)isr6,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(7,  (uint64_t)isr7,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(8,  (uint64_t)isr8,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(9,  (uint64_t)isr9,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(10, (uint64_t)isr10, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(11, (uint64_t)isr11, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(12, (uint64_t)isr12, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(13, (uint64_t)isr13, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(14, (uint64_t)isr14, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(15, (uint64_t)isr15, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(16, (uint64_t)isr16, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(17, (uint64_t)isr17, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(18, (uint64_t)isr18, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(19, (uint64_t)isr19, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(20, (uint64_t)isr20, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(21, (uint64_t)isr21, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(22, (uint64_t)isr22, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(23, (uint64_t)isr23, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(24, (uint64_t)isr24, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(25, (uint64_t)isr25, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(26, (uint64_t)isr26, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(27, (uint64_t)isr27, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(28, (uint64_t)isr28, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(29, (uint64_t)isr29, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(30, (uint64_t)isr30, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(31, (uint64_t)isr31, IDT_TYPE_INTERRUPT, 0);

    /* 16 remapped PIC IRQs -> vectors 0x20..0x2F */
    idt_set_gate(0x20, (uint64_t)irq0,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x21, (uint64_t)irq1,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x22, (uint64_t)irq2,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x23, (uint64_t)irq3,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x24, (uint64_t)irq4,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x25, (uint64_t)irq5,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x26, (uint64_t)irq6,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x27, (uint64_t)irq7,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x28, (uint64_t)irq8,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x29, (uint64_t)irq9,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x2A, (uint64_t)irq10, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x2B, (uint64_t)irq11, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x2C, (uint64_t)irq12, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x2D, (uint64_t)irq13, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x2E, (uint64_t)irq14, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(0x2F, (uint64_t)irq15, IDT_TYPE_INTERRUPT, 0);

    /* Syscall via int 0x80, reachable from ring 3 (DPL=3) */
    idt_set_gate(0x80, (uint64_t)syscall_isr, IDT_TYPE_INTERRUPT, 3);

    idt_ptr.limit = (uint16_t)(sizeof(idt_entries) - 1);
    idt_ptr.base  = (uint64_t)idt_entries;

    lidt(&idt_ptr);
}

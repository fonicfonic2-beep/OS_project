#ifndef IDT_H
#define IDT_H

#include "types.h"

#define IDT_ENTRIES         256

/* Type attributes for idt_set_gate */
#define IDT_TYPE_INTERRUPT  0x8E   /* present, ring0, 64-bit interrupt gate */
#define IDT_TYPE_TRAP       0x8F   /* present, ring0, 64-bit trap gate */
#define IDT_TYPE_USER_INT   0xEE   /* present, ring3, interrupt gate (syscall) */
#define IDT_IST_NONE        0

typedef struct PACKED idt_entry {
    uint16_t base_low;    /* handler offset bits 0..15  */
    uint16_t selector;    /* code segment selector      */
    uint8_t  ist;         /* interrupt stack table (0 = none) */
    uint8_t  type_attr;   /* type & attributes          */
    uint16_t base_mid;    /* handler offset bits 16..31 */
    uint32_t base_high;   /* handler offset bits 32..63 */
    uint32_t reserved;
} idt_entry_t;

typedef struct PACKED idt_ptr {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

void idt_init(void);
void idt_set_gate(uint8_t vec, uint64_t handler, uint8_t type_attr, uint8_t dpl);

extern idt_entry_t idt_entries[IDT_ENTRIES];
extern idt_ptr_t   idt_ptr;

#endif /* IDT_H */

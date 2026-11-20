/*
 * irq.c — PIC interrupt request dispatch (C side).
 *
 * IRQ n arrives on vector 32+n (after PIC remap). The registered
 * per-IRQ handler is invoked, then EOI is sent to the PIC(s).
 * Default handlers: IRQ0 -> scheduler tick, IRQ1 -> keyboard,
 * IRQ12 -> PS/2 mouse.
 */
#include "irq.h"
#include "pic.h"
#include "io.h"
#include "string.h"
#include "scheduler.h"

/* Implemented by the driver modules (not in frozen headers). */
extern void keyboard_handler(registers_t* regs);
extern void mouse_handler(registers_t* regs);

static irq_handler_t irq_handlers[16];

void irq_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);   /* acknowledge slave first */
    }
    outb(PIC1_CMD, PIC_EOI);       /* always acknowledge master */
}

void irq_install_handler(uint8_t irq, irq_handler_t h)
{
    if (irq >= 16) return;
    irq_handlers[irq] = h;
    pic_clear_mask(irq);
}

void irq_uninstall_handler(uint8_t irq)
{
    if (irq >= 16) return;
    irq_handlers[irq] = NULL;
    pic_set_mask(irq);
}

/*
 * Common C handler. Name matches the assembly stub call target.
 * The header-declared irq_handler() is a thin wrapper below.
 */
void irq_common_handler(registers_t* r)
{
    uint8_t irq = (uint8_t)(r->int_no - IRQ0_VECTOR);

    if (irq < 16 && irq_handlers[irq] != NULL) {
        irq_handlers[irq](r);
    }

    irq_eoi(irq);
}

/* Header-declared entry point (frozen API). */
void irq_handler(registers_t* regs)
{
    irq_common_handler(regs);
}

void irq_init(void)
{
    /* Remap master PIC to 0x20..0x27, slave to 0x28..0x2F. */
    pic_init(0x20, 0x28);

    memset(irq_handlers, 0, sizeof(irq_handlers));

    /* Default device handlers. */
    irq_install_handler(IRQ_PIT,      scheduler_tick);   /* IRQ0 */
    irq_install_handler(IRQ_KEYBOARD, keyboard_handler); /* IRQ1 */
    irq_install_handler(IRQ_MOUSE,    mouse_handler);    /* IRQ12 */

    /* Unmask cascade line (IRQ2) so slave PIC IRQs (8..15) can fire. */
    pic_clear_mask(IRQ_CASCADE);
}

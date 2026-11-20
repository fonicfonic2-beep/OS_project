/*
 * pic.c — 8259A Programmable Interrupt Controller driver.
 *
 * Handles initialization (ICW1..ICW4), end-of-interrupt, and
 * per-line masking (OCW1), plus IRR/ISR reads and full disable.
 */
#include "pic.h"
#include "io.h"

#define PIC_OCW3_READ_IRR 0x0A
#define PIC_OCW3_READ_ISR 0x0B

void pic_init(uint8_t offset1, uint8_t offset2)
{
    uint8_t mask1, mask2;

    /* Save current masks. */
    mask1 = inb(PIC1_DATA);
    mask2 = inb(PIC2_DATA);

    /* ICW1: start initialization cascade, expect ICW4. */
    outb(PIC1_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    /* ICW2: vector offsets. */
    outb(PIC1_DATA, offset1);
    io_wait();
    outb(PIC2_DATA, offset2);
    io_wait();

    /* ICW3: master has slave on IRQ2; slave's cascade identity is 2. */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: 8086 mode (normal EOI, non-buffered, fully nested). */
    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    /* Mask all lines by default; drivers unmask as handlers are installed. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    (void)mask1; (void)mask2;
}

void pic_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void pic_set_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t  value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = (uint8_t)(inb(port) | (1 << irq));
    outb(port, value);
}

void pic_clear_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t  value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = (uint8_t)(inb(port) & ~(1 << irq));
    outb(port, value);
}

uint16_t pic_get_irr(void)
{
    outb(PIC1_CMD, PIC_OCW3_READ_IRR);
    outb(PIC2_CMD, PIC_OCW3_READ_IRR);
    return (uint16_t)(inb(PIC2_CMD) << 8 | inb(PIC1_CMD));
}

uint16_t pic_get_isr(void)
{
    outb(PIC1_CMD, PIC_OCW3_READ_ISR);
    outb(PIC2_CMD, PIC_OCW3_READ_ISR);
    return (uint16_t)(inb(PIC2_CMD) << 8 | inb(PIC1_CMD));
}

void pic_disable(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

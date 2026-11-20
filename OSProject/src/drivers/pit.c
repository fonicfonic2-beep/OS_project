/*
 * pit.c — Programmable Interrupt Timer (8253/8254), channel 0.
 *
 * Channel 0 is driven by a 1.193182 MHz reference clock and is wired to
 * IRQ0.  We program it in mode 3 (square-wave generator) at the requested
 * frequency and count ticks in the IRQ0 handler.
 */
#include "pit.h"
#include "io.h"
#include "irq.h"

/* Command byte: channel 0, low/high byte access, mode 3, binary. */
#define PIT_CMD_CH0_MODE3  0x36

static volatile uint64_t g_pit_ticks = 0;
static uint32_t          g_pit_hz    = 1000;

static void pit_handler(registers_t* regs)
{
    (void)regs;
    g_pit_ticks++;
}

void pit_init(uint32_t hz)
{
    uint32_t divisor;

    if (hz == 0) {
        hz = 1000;            /* sane default */
    }
    if (hz > PIT_BASE_FREQ) {
        hz = PIT_BASE_FREQ;   /* clamp to reference clock */
    }
    g_pit_hz = hz;

    divisor = PIT_BASE_FREQ / hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFF) {
        divisor = 0xFFFF;     /* minimum frequency ~18.2 Hz */
    }

    /* Send the command, then the divisor low byte then high byte. */
    outb(PIT_CMD, PIT_CMD_CH0_MODE3);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    g_pit_ticks = 0;
    irq_install_handler(IRQ_PIT, pit_handler);
}

uint64_t pit_ticks(void)
{
    return g_pit_ticks;
}

uint64_t pit_millis(void)
{
    /* ticks * 1000 / hz, computed in 64-bit to avoid overflow. */
    return (g_pit_ticks * 1000ULL) / (uint64_t)g_pit_hz;
}

void pit_sleep(uint64_t ms)
{
    uint64_t start = pit_millis();
    while ((pit_millis() - start) < ms) {
        /* busy-wait; interrupts must be enabled for the tick to advance */
        __asm__ volatile ("pause");
    }
}

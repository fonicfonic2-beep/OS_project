/*
 * serial.c — 16550 UART driver (COM1 / COM2).
 *
 * COM1 is configured at 115200 baud, 8 data bits, no parity, 1 stop bit
 * (8N1).  All port access goes through inb/outb from <io.h>.
 */
#include "serial.h"
#include "io.h"

/* 16550 register offsets (relative to the base port) */
#define UART_RX       0   /* DLAB=0: receiver buffer (read)             */
#define UART_TX       0   /* DLAB=0: transmitter holding (write)        */
#define UART_IER      1   /* DLAB=0: interrupt enable                   */
#define UART_IIR      2   /* interrupt identification (read)            */
#define UART_FCR      2   /* FIFO control (write)                       */
#define UART_LCR      3   /* line control                               */
#define UART_MCR      4   /* modem control                              */
#define UART_LSR      5   /* line status                                */
#define UART_MSR      6   /* modem status                               */
#define UART_DLL      0   /* DLAB=1: divisor latch low                  */
#define UART_DLH      1   /* DLAB=1: divisor latch high                 */

#define UART_BAUD_MAX 115200  /* reference clock / 16                     */

/* Line status bits */
#define LSR_DATA_READY  0x01
#define LSR_TX_EMPTY    0x20  /* transmitter holding register empty        */
#define LSR_TX_IDLE     0x40  /* transmitter empty (line idle)             */

static uint16_t g_port = COM1_PORT;

/*
 * Wait until the transmitter holding register is empty so we can safely
 * write another byte.
 */
static void serial_wait_tx_empty(uint16_t port)
{
    while ((inb(port + UART_LSR) & LSR_TX_EMPTY) == 0) {
        /* spin */
    }
}

void serial_init(void)
{
    uint16_t port = COM1_PORT;
    uint16_t divisor;

    g_port = port;

    /* Disable all interrupts on the UART. */
    outb(port + UART_IER, 0x00);

    /* Enable DLAB (divisor latch access) to program the baud rate. */
    outb(port + UART_LCR, 0x80);

    /* 115200 baud -> divisor = 115200 / 115200 = 1. */
    divisor = (uint16_t)(UART_BAUD_MAX / 115200u);
    outb(port + UART_DLL, (uint8_t)(divisor & 0xFF));
    outb(port + UART_DLH, (uint8_t)((divisor >> 8) & 0xFF));

    /* Clear DLAB, set 8 data bits, no parity, 1 stop bit (8N1). */
    outb(port + UART_LCR, 0x03);

    /* Enable FIFO, clear both FIFOs, 14-byte interrupt threshold. */
    outb(port + UART_FCR, 0xC7);

    /* Assert RTS/DTR (modem control: OUT2=IRQ enable, RTS, DTR). */
    outb(port + UART_MCR, 0x0B);

    /* Loop-back test: write a byte and read it back. */
    outb(port + UART_MCR, 0x1E);          /* loop-back mode */
    outb(port + UART_TX, 0xAE);
    if (inb(port + UART_RX) != 0xAE) {
        /* No serial port / loop-back failed: fall back gracefully. */
        g_port = 0;
        return;
    }

    /* Disable loop-back, restore normal operation with IRQ disabled. */
    outb(port + UART_MCR, 0x0F);
}

void serial_write(char c)
{
    if (g_port == 0) {
        return;
    }
    serial_wait_tx_empty(g_port);
    outb(g_port + UART_TX, (uint8_t)c);
}

void serial_write_str(const char* s)
{
    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        serial_write(*s++);
    }
}

void serial_write_hex(uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;

    serial_write_str("0x");
    if (v == 0) {
        serial_write('0');
        return;
    }
    /* Print from most significant nibble down, skip leading zeros. */
    for (i = 15; i >= 0; i--) {
        uint8_t nib = (uint8_t)((v >> (i * 4)) & 0xF);
        if (nib != 0 || i == 0) {
            serial_write(hex[nib]);
            /* once we have started, print all remaining nibbles */
            while (--i >= 0) {
                serial_write(hex[(uint8_t)((v >> (i * 4)) & 0xF)]);
            }
            break;
        }
    }
}

int serial_received(void)
{
    if (g_port == 0) {
        return 0;
    }
    return (inb(g_port + UART_LSR) & LSR_DATA_READY) ? 1 : 0;
}

char serial_read(void)
{
    if (g_port == 0) {
        return 0;
    }
    while (!serial_received()) {
        /* spin until a byte arrives */
    }
    return (char)inb(g_port + UART_RX);
}

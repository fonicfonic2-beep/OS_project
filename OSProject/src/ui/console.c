/*
 * ui/console.c — unified text console.
 *
 * Every byte written goes to both the VGA text buffer and the serial
 * port. Control characters \n, \r and \b are handled here; printable
 * bytes (including UTF-8 continuation bytes) are forwarded to vga_putc.
 * Input blocks on the keyboard buffer one byte at a time.
 */
#include "console.h"
#include "types.h"
#include "vga.h"
#include "serial.h"
#include "keyboard.h"
#include "stdio.h"
#include "stdarg.h"

#define CONSOLE_PRINTF_BUF 1024

void console_init(void)
{
    /* Light-grey text on black background. */
    vga_color = VGA_ENTRY_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_row = 0;
    vga_col = 0;
    vga_enable_cursor(14, 15);
    vga_set_cursor(0, 0);
}

void console_putc(char c)
{
    /* Mirror every byte (including control codes) to the serial port. */
    serial_write(c);

    switch (c) {
    case '\n':
        vga_row++;
        vga_col = 0;
        if (vga_row >= VGA_TEXT_HEIGHT) {
            vga_scroll();
            vga_row = VGA_TEXT_HEIGHT - 1;
        }
        break;

    case '\r':
        vga_col = 0;
        break;

    case '\b':
        if (vga_col > 0) {
            vga_col--;
            /* vga_putc advances the column, so erase and step back again. */
            vga_putc(' ', vga_color);
            vga_col--;
        }
        break;

    default:
        vga_putc(c, vga_color);
        /* Defensive wrap/scroll in case vga_putc does not handle it. */
        if (vga_col >= VGA_TEXT_WIDTH) {
            vga_col = 0;
            vga_row++;
            if (vga_row >= VGA_TEXT_HEIGHT) {
                vga_scroll();
                vga_row = VGA_TEXT_HEIGHT - 1;
            }
        }
        break;
    }

    if (vga_col < 0) {
        vga_col = 0;
    }
    if (vga_row < 0) {
        vga_row = 0;
    }
    vga_set_cursor((uint8_t)vga_col, (uint8_t)vga_row);
}

void console_puts(const char* s)
{
    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        console_putc(*s);
        s++;
    }
}

void console_printf(const char* fmt, ...)
{
    char buf[CONSOLE_PRINTF_BUF];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    console_puts(buf);
}

void console_clear(void)
{
    vga_clear(vga_color);
    vga_row = 0;
    vga_col = 0;
    vga_set_cursor(0, 0);
}

void console_set_color(uint8_t fg, uint8_t bg)
{
    vga_color = VGA_ENTRY_COLOR(fg & 0x0F, bg & 0x0F);
}

int console_getchar(void)
{
    /* Block until a byte is available, then return it (0..255). */
    while (keyboard_haschar() == 0) {
        /* busy-wait; interrupts refill the keyboard buffer */
    }
    return (int)(unsigned char)keyboard_getchar();
}

int console_haschar(void)
{
    return (keyboard_haschar() != 0) ? 1 : 0;
}

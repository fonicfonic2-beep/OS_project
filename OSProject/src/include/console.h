#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"

/*
 * Unified console: writes go to VGA text (or framebuffer) + serial.
 * console_printf supports %c %s %d %u %x %X %p and %% (minimal).
 * Input is UTF-8; console_getchar blocks until a byte is available
 * from the keyboard buffer.
 */

void console_init(void);
void console_putc(char c);
void console_puts(const char* s);
void console_printf(const char* fmt, ...);
void console_clear(void);
void console_set_color(uint8_t fg, uint8_t bg);
int  console_getchar(void);      /* blocking */
int  console_haschar(void);

#endif /* CONSOLE_H */

#ifndef STDIO_H
#define STDIO_H

#include "types.h"
#include "stdarg.h"

/*
 * Minimal freestanding stdio: only string-formatting functions.
 * Kernel console output is provided by console_printf (console.h).
 * Supported specifiers: %c %s %d %i %u %x %X %p %% with optional width.
 */
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsprintf(char* buf, const char* fmt, va_list args);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);

#endif /* STDIO_H */

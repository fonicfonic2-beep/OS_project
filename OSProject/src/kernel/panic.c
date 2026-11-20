/*
 * kernel/panic.c — fatal-error helpers.
 *
 * kpanic() formats a message into a stack buffer, emits it on both the
 * console and the serial port, then disables interrupts and halts forever.
 * kassert() is a thin wrapper that panics when its condition is false.
 * khalt() is the plain "disable interrupts and halt" primitive.
 */
#include "kernel.h"
#include "types.h"
#include "stdio.h"
#include "stdarg.h"
#include "console.h"
#include "serial.h"
#include "io.h"

#define PANIC_BUF_SIZE 1024

void kpanic(const char* fmt, ...)
{
    char buf[PANIC_BUF_SIZE];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Console output (best-effort; may be uninitialised very early). */
    console_puts("\n");
    console_puts("**************** KERNEL PANIC ****************\n");
    console_puts(buf);
    console_puts("\n");
    console_puts("System halted.\n");

    /* Serial output always works once serial_init() has run. */
    serial_write_str("\n**************** KERNEL PANIC ****************\n");
    serial_write_str(buf);
    serial_write_str("\nSystem halted.\n");

    cli();
    for (;;) {
        hlt();
    }
}

void kassert(bool cond, const char* msg)
{
    if (!cond) {
        kpanic("Assertion failed: %s\n", (msg != NULL) ? msg : "(no message)");
    }
}

void khalt(void)
{
    cli();
    for (;;) {
        hlt();
    }
}

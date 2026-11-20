/*
 * Minimal freestanding stdio — string formatting only.
 *
 * Supported: %c %s %d %i %u %x %X %p %%
 * Flags/modifiers: '-' (left align), '0' (zero pad), numeric width,
 *                  'l' length modifier (long, 64-bit on x86_64).
 * Kernel console output lives in console.c (console_printf).
 */
#include "stdio.h"
#include "string.h"

/* Output sink with a hard size cap; pos counts chars that would be
 * written even beyond the buffer (so the return value is correct). */
typedef struct {
    char*  buf;
    size_t size;   /* total buffer size incl. NUL */
    size_t pos;
} out_t;

static void outc(out_t* o, char c)
{
    if (o->size != 0 && o->pos < o->size - 1)
        o->buf[o->pos] = c;
    o->pos++;
}

static void outs(out_t* o, const char* s, size_t len)
{
    for (size_t i = 0; i < len; i++) outc(o, s[i]);
}

/* Reverse of a small buffer. */
static void rev(char* s, int n)
{
    for (int i = 0, j = n - 1; i < j; i++, j--) {
        char t = s[i]; s[i] = s[j]; s[j] = t;
    }
}

/* Format an unsigned integer into buf (caller-provided, >= 32 bytes),
 * return length.  base 10 or 16. */
static int fmt_ull(unsigned long long v, char* buf, int base, int upper)
{
    static const char lhex[] = "0123456789abcdef";
    static const char uhex[] = "0123456789ABCDEF";
    const char* digits = upper ? uhex : lhex;
    int n = 0;
    if (v == 0) { buf[n++] = '0'; return n; }
    while (v > 0) {
        buf[n++] = digits[v % (unsigned)base];
        v /= (unsigned)base;
    }
    rev(buf, n);
    return n;
}

/* Emit a field with padding. */
static void emit_padded(out_t* o, const char* body, int body_len,
                        int width, int left_align, char pad_char,
                        const char* prefix, int prefix_len)
{
    int total = body_len + prefix_len;
    int pad = (width > total) ? (width - total) : 0;

    if (!left_align && pad_char == ' ')
        for (int i = 0; i < pad; i++) outc(o, ' ');
    outs(o, prefix, (size_t)prefix_len);
    if (!left_align && pad_char == '0')
        for (int i = 0; i < pad; i++) outc(o, '0');
    outs(o, body, (size_t)body_len);
    if (left_align)
        for (int i = 0; i < pad; i++) outc(o, ' ');
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args)
{
    out_t o;
    o.buf = buf;
    o.size = size;
    o.pos = 0;

    for (; *fmt != '\0'; fmt++) {
        if (*fmt != '%') { outc(&o, *fmt); continue; }
        fmt++;

        /* flags */
        int left_align = 0, zero_pad = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left_align = 1;
            if (*fmt == '0') zero_pad = 1;
            fmt++;
        }
        if (left_align) zero_pad = 0;   /* '-' overrides '0' */

        /* width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* length modifier */
        int lng = 0;
        while (*fmt == 'l') { lng = 1; fmt++; }

        char pad_char = zero_pad ? '0' : ' ';
        char numbuf[32];
        int  nlen;
        const char* prefix = "";
        int prefix_len = 0;
        unsigned long long uval;

        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(args, int);
            emit_padded(&o, &c, 1, width, left_align, ' ', "", 0);
            break;
        }
        case 's': {
            const char* s = va_arg(args, const char*);
            if (s == NULL) s = "(null)";
            int slen = (int)strlen(s);
            emit_padded(&o, s, slen, width, left_align, ' ', "", 0);
            break;
        }
        case 'd':
        case 'i': {
            long long val = lng ? (long long)va_arg(args, long)
                                : (long long)va_arg(args, int);
            if (val < 0) {
                prefix = "-"; prefix_len = 1;
                uval = (unsigned long long)(-val);
            } else {
                uval = (unsigned long long)val;
            }
            nlen = fmt_ull(uval, numbuf, 10, 0);
            emit_padded(&o, numbuf, nlen, width, left_align, pad_char, prefix, prefix_len);
            break;
        }
        case 'u':
            uval = lng ? (unsigned long long)va_arg(args, unsigned long)
                       : (unsigned long long)va_arg(args, unsigned int);
            nlen = fmt_ull(uval, numbuf, 10, 0);
            emit_padded(&o, numbuf, nlen, width, left_align, pad_char, "", 0);
            break;
        case 'x':
        case 'X': {
            int upper = (*fmt == 'X');
            uval = lng ? (unsigned long long)va_arg(args, unsigned long)
                       : (unsigned long long)va_arg(args, unsigned int);
            nlen = fmt_ull(uval, numbuf, 16, upper);
            emit_padded(&o, numbuf, nlen, width, left_align, pad_char, "", 0);
            break;
        }
        case 'p': {
            uval = (unsigned long long)(uintptr_t)va_arg(args, void*);
            prefix = "0x"; prefix_len = 2;
            nlen = fmt_ull(uval, numbuf, 16, 0);
            emit_padded(&o, numbuf, nlen, width, left_align, pad_char, prefix, prefix_len);
            break;
        }
        case '%':
            outc(&o, '%');
            break;
        case '\0':
            fmt--;   /* trailing '%' — stop */
            break;
        default:
            /* Unknown specifier: emit verbatim. */
            outc(&o, '%');
            outc(&o, *fmt);
            break;
        }
    }

    if (size != 0) {
        size_t end = (o.pos < size - 1) ? o.pos : size - 1;
        buf[end] = '\0';
    }
    return (int)o.pos;
}

int vsprintf(char* buf, const char* fmt, va_list args)
{
    /* No size bound: pass a huge cap. */
    return vsnprintf(buf, (size_t)-1, fmt, args);
}

int snprintf(char* buf, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}

int sprintf(char* buf, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsprintf(buf, fmt, args);
    va_end(args);
    return n;
}

#ifndef STDARG_H
#define STDARG_H

/*
 * Minimal freestanding stdarg implemented with compiler builtins.
 * This avoids pulling in the host toolchain's <stdarg.h>.
 */

typedef __builtin_va_list va_list;

#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_end(ap)          __builtin_va_end(ap)
#define va_copy(dst, src)   __builtin_va_copy(dst, src)

#endif /* STDARG_H */

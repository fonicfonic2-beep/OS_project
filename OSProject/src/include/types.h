#ifndef TYPES_H
#define TYPES_H

/*
 * Freestanding fixed-width types and compiler macros.
 * No standard library headers are used anywhere in this kernel.
 */

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long       uint64_t;

typedef signed char         int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef long                int64_t;

typedef unsigned long       size_t;
typedef long                ssize_t;
typedef unsigned long       uintptr_t;
typedef long                intptr_t;

typedef _Bool               bool;
#define true                1
#define false               0

#define NULL                ((void*)0)

/* Compiler attribute macros */
#define PACKED              __attribute__((packed))
#define ALIGN(x)            __attribute__((aligned(x)))
#define UNUSED              __attribute__((unused))
#define INLINE              static inline
#define NORETURN            __attribute__((noreturn))
#define WEAK                __attribute__((weak))
#define SECTION(s)          __attribute__((section(s)))
#define ALIGNED(x)          __attribute__((aligned(x)))

#endif /* TYPES_H */

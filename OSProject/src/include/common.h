#ifndef COMMON_H
#define COMMON_H

#include "types.h"

/* --- Paging / memory layout constants --- */
#define PAGE_SIZE               4096
#define PAGE_SHIFT              12
#define PAGE_MASK               (~((uintptr_t)PAGE_SIZE - 1))

#define HIGHER_HALF_OFFSET      0xFFFFFFFF80000000ULL
#define KERNEL_VBASE            0xFFFFFFFF80000000ULL
#define KERNEL_VIRT_BASE        0xFFFFFFFF80000000ULL

#define USER_BASE               0x0000000000400000ULL
#define USER_STACK_TOP          0x00007FFFFFFFE000ULL

#define PHYS_TO_VIRT(x)         ((uintptr_t)(x) + HIGHER_HALF_OFFSET)
#define VIRT_TO_PHYS(x)         ((uintptr_t)(x) - HIGHER_HALF_OFFSET)

/* --- Generic utility macros --- */
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))

#define BIT(n)                  (1ULL << (n))
#define SET_BIT(v, n)           ((v) |= BIT(n))
#define CLEAR_BIT(v, n)         ((v) &= ~BIT(n))
#define TEST_BIT(v, n)          (((v) & BIT(n)) != 0)

#define ALIGN_UP(x, a)          (((uintptr_t)(x) + ((uintptr_t)(a) - 1)) & ~((uintptr_t)(a) - 1))
#define ALIGN_DOWN(x, a)        ((uintptr_t)(x) & ~((uintptr_t)(a) - 1))
#define PAGE_ALIGN_UP(x)        ALIGN_UP((x), PAGE_SIZE)
#define PAGE_ALIGN_DOWN(x)      ALIGN_DOWN((x), PAGE_SIZE)

#define ARRAY_SIZE(a)           (sizeof(a) / sizeof((a)[0]))

#define CONTAINER_OF(ptr, type, member) \
    ((type*)((uintptr_t)(ptr) - (uintptr_t)&((type*)0)->member))

#endif /* COMMON_H */

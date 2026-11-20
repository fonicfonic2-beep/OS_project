#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include "multiboot2.h"

/*
 * Kernel-wide globals and fatal-error helpers.
 * kpanic prints a message, disables interrupts, and halts.
 * kassert triggers kpanic if cond is false.
 */

void kpanic(const char* fmt, ...) __attribute__((noreturn));
void kassert(bool cond, const char* msg);
void khalt(void) __attribute__((noreturn));

extern multiboot_info_t* g_multiboot;   /* saved boot info pointer */
extern uintptr_t         g_kernel_phys_start;
extern uintptr_t         g_kernel_phys_end;

#endif /* KERNEL_H */

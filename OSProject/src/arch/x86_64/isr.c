/*
 * isr.c — CPU exception dispatch (C side).
 *
 * isr_common_handler() is entered from isr.asm with a full registers_t
 * frame on the stack. It prints diagnostics, dispatches to a registered
 * handler if present, and otherwise panics. Page faults in user mode
 * kill the faulting process instead of panicking.
 */
#include "isr.h"
#include "console.h"
#include "io.h"
#include "kernel.h"
#include "string.h"
#include "scheduler.h"

isr_handler_t isr_handlers[256];

/* Names of the first 32 exceptions for diagnostics. */
static const char* const exception_names[32] = {
    "Divide Error",          "Debug",                "NMI Interrupt",
    "Breakpoint",            "Overflow",             "Bound Range Exceeded",
    "Invalid Opcode",        "Device Not Available", "Double Fault",
    "Coprocessor Overrun",   "Invalid TSS",          "Segment Not Present",
    "Stack-Segment Fault",   "General Protection",   "Page Fault",
    "Reserved",              "x87 FP Exception",     "Alignment Check",
    "Machine Check",         "SIMD FP Exception",    "Virtualization",
    "Control Protection",    "Reserved",             "Reserved",
    "Reserved",              "Reserved",             "Reserved",
    "Reserved",              "Reserved",             "Reserved",
    "Security Exception",    "Reserved"
};

void isr_init(void)
{
    memset(isr_handlers, 0, sizeof(isr_handlers));
}

void isr_install_handler(uint8_t n, isr_handler_t h)
{
    isr_handlers[n] = h;
}

/*
 * Common C handler. Name matches the assembly stub call target.
 * The header-declared isr_handler() is a thin wrapper below.
 */
void isr_common_handler(registers_t* r)
{
    uint8_t vec = (uint8_t)r->int_no;

    /* Page fault: always report cr2; user-mode faults kill the process. */
    if (vec == ISR_PF) {
        uintptr_t cr2 = read_cr2();
        bool user_mode = ((r->cs & 3) == 3);
        console_printf("PAGE FAULT: err=0x%x rip=%p cr2=%p %s-mode\n",
                       (uint32_t)r->err_code, (void*)r->rip, (void*)cr2,
                       user_mode ? "user" : "kernel");
        if (user_mode) {
            console_printf("Killing user process due to page fault.\n");
            /* Never returns; marks current process exiting and schedules. */
            scheduler_exit_current(139);
            for (;;) { cli(); hlt(); }
        }
        /* Kernel-mode page fault is fatal. Fall through to panic below
         * unless a handler was registered (e.g. demand paging). */
    }

    /* Registered handler takes precedence (covers syscall 0x80 too). */
    if (isr_handlers[vec] != NULL) {
        isr_handlers[vec](r);
        return;
    }

    /* Only exceptions (0..31) get verbose diagnostics. */
    if (vec < 32) {
        const char* name = exception_names[vec];
        console_printf("EXCEPTION %u (%s): err=0x%x rip=%p cs=0x%x\n",
                       (unsigned)vec, name, (uint32_t)r->err_code,
                       (void*)r->rip, (uint32_t)r->cs);
    }

    kpanic("Unhandled interrupt/exception vector %u (err=0x%x) at rip=%p",
           (unsigned)vec, (uint32_t)r->err_code, (void*)r->rip);
}

/* Header-declared entry point (frozen API). */
void isr_handler(registers_t* regs)
{
    isr_common_handler(regs);
}

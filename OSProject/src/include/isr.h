#ifndef ISR_H
#define ISR_H

#include "types.h"

/*
 * registers_t mirrors the stack layout built by the assembly stub.
 *
 * Field order below = low address -> high address at the point the C
 * handler is entered (rsp points at &regs->rax).
 *
 * Assembly stub convention (implementation MUST follow):
 *   1. Hardware already pushed (high->low): ss, rsp, rflags, cs, rip.
 *   2. Per-vector stub pushes: err_code (or dummy 0), then int_no.
 *   3. Common stub pushes (in this order): r15, r14, r13, r12, r11,
 *      r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax.
 *   4. mov rdi, rsp  (first arg = registers_t*).
 * On return: pop GPRs in reverse, add rsp,16 (drop int_no/err_code), iretq.
 */
typedef struct registers {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t int_no;
    uint64_t err_code;
    /* Hardware-pushed iret frame */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} registers_t;

typedef void (*isr_handler_t)(registers_t* regs);

/* Exception vectors (0..31) */
#define ISR_DE   0   /* Divide Error              */
#define ISR_DB   1   /* Debug                     */
#define ISR_NMI  2   /* Non-maskable Interrupt    */
#define ISR_BP   3   /* Breakpoint                */
#define ISR_OF   4   /* Overflow                  */
#define ISR_BR   5   /* Bound Range Exceeded      */
#define ISR_UD   6   /* Invalid Opcode            */
#define ISR_NM   7   /* Device Not Available      */
#define ISR_DF   8   /* Double Fault              */
#define ISR_TS   10  /* Invalid TSS               */
#define ISR_NP   11  /* Segment Not Present       */
#define ISR_SS   12  /* Stack-Segment Fault       */
#define ISR_GP   13  /* General Protection        */
#define ISR_PF   14  /* Page Fault                */
#define ISR_MF   16  /* x87 Floating-Point        */
#define ISR_AC   17  /* Alignment Check           */
#define ISR_MC   18  /* Machine Check             */
#define ISR_XM   19  /* SIMD Floating-Point       */
#define ISR_VE   20  /* Virtualization            */
#define ISR_SX   30  /* Security Exception        */

void isr_init(void);
void isr_install_handler(uint8_t n, isr_handler_t h);
void isr_handler(registers_t* regs);   /* common C handler called from stub */

extern isr_handler_t isr_handlers[256];

/* Assembly stub entries (0..31). Each has prototype void stub(void). */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

#endif /* ISR_H */

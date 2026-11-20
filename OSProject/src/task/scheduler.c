/*
 * scheduler.c — Simple preemptive round-robin scheduler (x86_64).
 *
 * The ready queue is a circular singly-linked list threaded through
 * process_t::next.  idle is always present and is the fallback when no
 * other process is READY.
 *
 *   scheduler_tick()  — called from the PIT IRQ (IRQ0, vector 0x20) with
 *                       the full register frame built by the asm stub.
 *   yield()           — voluntary reschedule, implemented as a naked
 *                       function that builds an identical registers_t
 *                       frame on the kernel stack and then hands off to
 *                       switch_context() (arch agent, context_switch.asm).
 *
 * switch_context(registers_t** old, registers_t* new) restores the full
 * frame (GPRs + iret frame) and returns via iretq, so every context we
 * hand it must be a complete registers_t.
 */
#include "types.h"
#include "common.h"
#include "isr.h"
#include "process.h"
#include "scheduler.h"
#include "paging.h"
#include "gdt.h"
#include "kernel.h"

/* Provided by arch agent (context_switch.asm). */
extern void switch_context(registers_t** old, registers_t* new_ctx);

/* Defined in process.c. */
extern void idle_task(void);

/* Circular ready-queue head (exported so process.c can walk it). */
process_t* ready_head = NULL;

/* Currently running process. */
static process_t* current    = NULL;

/* Always-runnable fallback. */
static process_t* idle_proc  = NULL;

/* ------------------------------------------------------------------ */
/* Interrupt-state helpers (UP; used to protect queue manipulations).  */
/* ------------------------------------------------------------------ */
static inline uint64_t irq_save_cli(void)
{
    uint64_t f;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}

static inline void irq_restore(uint64_t f)
{
    __asm__ volatile ("pushq %0; popfq" :: "r"(f) : "memory", "cc");
}

/* ------------------------------------------------------------------ */
/* scheduler_add — append to the circular ready queue.                 */
/* ------------------------------------------------------------------ */
void scheduler_add(process_t* p)
{
    uint64_t flags = irq_save_cli();
    if (!p) {
        irq_restore(flags);
        return;
    }
    if (!ready_head) {
        ready_head = p;
        p->next    = p;            /* single-node circle */
    } else {
        process_t* tail = ready_head;
        while (tail->next != ready_head) {
            tail = tail->next;
        }
        tail->next = p;
        p->next    = ready_head;
    }
    irq_restore(flags);
}

/* ------------------------------------------------------------------ */
/* scheduler_remove — unlink from the circular ready queue.            */
/* ------------------------------------------------------------------ */
void scheduler_remove(process_t* p)
{
    uint64_t flags;
    if (!p || !ready_head) {
        return;
    }
    flags = irq_save_cli();

    if (p->next == p && p == ready_head) {
        ready_head = NULL;          /* sole node */
    } else {
        process_t* prev = ready_head;
        while (prev->next != p && prev->next != ready_head) {
            prev = prev->next;
        }
        if (prev->next == p) {
            prev->next = p->next;
            if (p == ready_head) {
                ready_head = p->next;
            }
        }
    }
    p->next = NULL;
    irq_restore(flags);
}

/* ------------------------------------------------------------------ */
/* scheduler_current                                                   */
/* ------------------------------------------------------------------ */
process_t* scheduler_current(void)
{
    return current;
}

/* ------------------------------------------------------------------ */
/* pick_next — round-robin selection starting after `from`.            */
/* Returns idle_proc if nothing else is READY.                         */
/* ------------------------------------------------------------------ */
static process_t* pick_next(process_t* from)
{
    process_t* p;
    if (!ready_head) {
        return idle_proc ? idle_proc : from;
    }
    p = from ? from->next : ready_head;
    while (p != from && p->state != PROC_READY) {
        p = p->next;
    }
    if (p->state == PROC_READY) {
        return p;
    }
    return idle_proc ? idle_proc : from;
}

/* Common context-switch tail shared by scheduler_tick and yield. */
static void switch_to(process_t* old, process_t* next)
{
    next->state = PROC_RUNNING;
    current     = next;
    paging_switch_pml4(next->pml4_phys);
    /* On user->kernel entry the CPU loads rsp0 from the TSS; point it at
     * the incoming process's kernel stack top. */
    tss_set_stack(next->kstack + KSTACK_SIZE);
    switch_context(&old->ctx, next->ctx);
    __builtin_unreachable();
}

/* ------------------------------------------------------------------ */
/* scheduler_tick — invoked from the PIT IRQ handler (IRQ0).           */
/* `regs` points at the full frame on the kernel stack.                */
/* ------------------------------------------------------------------ */
void scheduler_tick(registers_t* regs)
{
    process_t* old = current;
    process_t* next;

    if (!old) {
        return;
    }

    old->ctx      = regs;          /* save full frame */
    old->cpu_time++;

    if (old->state == PROC_RUNNING) {
        old->state = PROC_READY;
    }

    next = pick_next(old);
    if (next == old) {
        old->state = PROC_RUNNING;
        return;                    /* IRQ stub iretq resumes old */
    }

    switch_to(old, next);
}

/* ------------------------------------------------------------------ */
/* yield_finish — C half of yield().  Receives the registers_t frame   */
/* built on the kernel stack by the naked asm prologue.  May return    */
/* (with interrupts re-enabled) if the only runnable task is self.     */
/* ------------------------------------------------------------------ */
static void __attribute__((used)) yield_finish(registers_t* frame)
{
    process_t* old = current;
    process_t* next;

    if (!old) {
        __asm__ volatile ("sti");
        return;
    }

    old->ctx = frame;
    if (old->state == PROC_RUNNING) {
        old->state = PROC_READY;
    }

    next = pick_next(old);
    if (next == old) {
        old->state = PROC_RUNNING;
        __asm__ volatile ("sti");
        return;
    }

    switch_to(old, next);
}

/* ------------------------------------------------------------------ */
/* yield — voluntary reschedule.  Builds a complete registers_t frame  */
/* (matching isr.h layout) on the kernel stack, then calls             */
/* yield_finish() which picks the next task and switch_context()'s.    */
/*                                                                     */
/* Frame layout (low -> high = %rsp after sub):                        */
/*   rax rbx rcx rdx rsi rdi rbp r8 r9 r10 r11 r12 r13 r14 r15         */
/*   int_no err_code rip cs rflags rsp ss   (22 qwords = 176 bytes)    */
/* ------------------------------------------------------------------ */
__attribute__((naked)) void yield(void)
{
    __asm__ (
        "sub $176, %rsp\n\t"
        "mov %rax,    0(%rsp)\n\t"
        "mov %rbx,    8(%rsp)\n\t"
        "mov %rcx,   16(%rsp)\n\t"
        "mov %rdx,   24(%rsp)\n\t"
        "mov %rsi,   32(%rsp)\n\t"
        "mov %rdi,   40(%rsp)\n\t"
        "mov %rbp,   48(%rsp)\n\t"
        "mov %r8,    56(%rsp)\n\t"
        "mov %r9,    64(%rsp)\n\t"
        "mov %r10,   72(%rsp)\n\t"
        "mov %r11,   80(%rsp)\n\t"
        "mov %r12,   88(%rsp)\n\t"
        "mov %r13,   96(%rsp)\n\t"
        "mov %r14,  104(%rsp)\n\t"
        "mov %r15,  112(%rsp)\n\t"
        "movq $0,    120(%rsp)\n\t"          /* int_no   */
        "movq $0,    128(%rsp)\n\t"          /* err_code */
        "leaq 2f(%rip), %rax\n\t"
        "mov %rax,  136(%rsp)\n\t"          /* rip = resume label */
        "movq $0x08, 144(%rsp)\n\t"          /* cs  = kernel code */
        "pushfq\n\t"
        "popq %rax\n\t"
        "mov %rax,  152(%rsp)\n\t"          /* rflags (IF kept) */
        "cli\n\t"
        "leaq 176(%rsp), %rax\n\t"
        "mov %rax,  160(%rsp)\n\t"          /* rsp = original at yield entry */
        "movq $0x20, 168(%rsp)\n\t"          /* ss  = kernel data */
        "mov %rsp, %rdi\n\t"                /* arg1 = frame */
        "call yield_finish\n\t"               /* returns only if next==old */
        "add $176, %rsp\n\t"                 /* discard frame, then return */
        "ret\n\t"
        "2:\n\t"                               /* resumed via iretq: rsp already original */
        "ret\n\t"
    );
    __builtin_unreachable();
}

/* ------------------------------------------------------------------ */
/* scheduler_init — create the idle task and mark it running.          */
/* ------------------------------------------------------------------ */
void scheduler_init(void)
{
    pid_t idle_pid;

    ready_head = NULL;
    current    = NULL;
    idle_proc  = NULL;

    /* First process_create() consumes PID 0 = PID_KERNEL. */
    idle_pid = process_create("idle", idle_task, /*user=*/false);
    if (idle_pid != PID_KERNEL) {
        kpanic("scheduler_init: idle task did not get PID 0\n");
    }

    idle_proc = process_by_pid(PID_KERNEL);
    if (!idle_proc) {
        kpanic("scheduler_init: cannot find idle task\n");
    }

    current         = idle_proc;
    current->state  = PROC_RUNNING;
    tss_set_stack(current->kstack + KSTACK_SIZE);
}

/* ------------------------------------------------------------------ */
/* scheduler_exit_current — terminate the current process.             */
/* ------------------------------------------------------------------ */
void scheduler_exit_current(int code)
{
    process_exit(code);
}

/* ------------------------------------------------------------------ */
/* process_list_head — head of the circular ready queue (for ps /      */
/* task manager).  Walk via ->next until returning to the head.        */
/* ------------------------------------------------------------------ */
process_t* process_list_head(void)
{
    return ready_head;
}

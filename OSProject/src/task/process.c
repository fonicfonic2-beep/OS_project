/*
 * process.c — Process (task) management for the x86_64 kernel.
 *
 * Implements process creation / destruction, PID allocation, kernel-stack
 * and address-space setup, and the idle task.  The scheduler queue itself
 * lives in scheduler.c (ready_head is exported so we can walk it here).
 *
 * Hardware contract (from arch agent, MUST match):
 *   GDT: 0x08 kcode, 0x10 kdata, 0x18 ucode (DPL3), 0x20 udata (DPL3).
 *   registers_t (isr.h) is a full iret frame: GPRs, int_no, err_code,
 *   rip, cs, rflags, rsp, ss.  switch_context() restores it via iretq.
 */
#include "types.h"
#include "common.h"
#include "isr.h"
#include "process.h"
#include "scheduler.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "string.h"
#include "kernel.h"

/* Defined in scheduler.c (circular ready queue head). */
extern process_t* ready_head;

/* Atomic PID counter.  First allocation is PID 0 (the idle/kernel task). */
static volatile pid_t next_pid = 0;

/* Number of physical pages backing each kernel stack.
 * Header freezes KSTACK_SIZE = 16 KiB (4 pages); we honour the header
 * constant so ctx placement and stack-top arithmetic stay consistent. */
#define KSTACK_PAGES  (KSTACK_SIZE / PAGE_SIZE)

/* User stack: 4 pages mapped immediately below USER_STACK_TOP. */
#define USTACK_PAGES  4

/* ------------------------------------------------------------------ */
/* Idle task — the very first process.  Runs only when nothing else is */
/* runnable.  Re-enables interrupts and halts until the next tick.      */
/* ------------------------------------------------------------------ */
void idle_task(void)
{
    for (;;) {
        __asm__ volatile ("sti; hlt" ::: "memory");
    }
}

/* ------------------------------------------------------------------ */
/* process_create                                                      */
/* ------------------------------------------------------------------ */
pid_t process_create(const char* name, void (*entry)(void), bool user)
{
    process_t* p;
    uintptr_t  kstack_phys, kstack_virt, kstack_top;
    size_t     i;

    p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) {
        kpanic("process_create: out of memory (process_t)\n");
    }
    memset(p, 0, sizeof(process_t));

    /* 1. Atomic PID allocation. */
    p->pid = __sync_fetch_and_add(&next_pid, 1);

    /* 2. Name. */
    if (name) {
        strncpy(p->name, name, PROC_NAME_MAX - 1);
    }
    p->name[PROC_NAME_MAX - 1] = '\0';

    p->state    = PROC_READY;
    p->entry    = (uintptr_t)entry;
    p->cpu_time = 0;
    p->exit_code = 0;
    p->parent   = scheduler_current();   /* NULL for the idle task */

    /* 3. Kernel stack: physical frames via PMM, mapped into the kernel
     *    address space (higher-half direct map) with RW permissions. */
    kstack_phys = (uintptr_t)pmm_alloc_pages(KSTACK_PAGES);
    if (!kstack_phys) {
        kpanic("process_create: out of physical memory (kstack)\n");
    }
    kstack_virt = PHYS_TO_VIRT(kstack_phys);
    for (i = 0; i < KSTACK_PAGES; i++) {
        paging_map_page(kstack_virt + i * PAGE_SIZE,
                        kstack_phys + i * PAGE_SIZE,
                        PAGE_PRESENT | PAGE_RW);
    }
    p->kstack   = kstack_virt;
    kstack_top  = kstack_virt + KSTACK_SIZE;

    /* 4. Address space. */
    if (user) {
        uintptr_t saved_cr3 = VIRT_TO_PHYS((uintptr_t)kernel_pml4);

        /* Kernel stack is mapped now, so creating the user PML4 next
         * copies the kernel entries (including this kstack) into it. */
        p->pml4_phys = paging_create_user_pml4();
        if (!p->pml4_phys) {
            kpanic("process_create: cannot create user PML4\n");
        }

        /* Map the user stack (4 pages, USER|RW) into the new address
         * space.  paging_map_page operates on the current PML4, so we
         * temporarily switch to the user PML4 (kernel VA stays mapped). */
        paging_switch_pml4(p->pml4_phys);
        p->ustack = USER_STACK_TOP - USTACK_PAGES * PAGE_SIZE;
        for (i = 0; i < USTACK_PAGES; i++) {
            uintptr_t phys = pmm_alloc_page();
            if (!phys) {
                kpanic("process_create: out of memory (ustack)\n");
            }
            paging_map_page(p->ustack + i * PAGE_SIZE, phys,
                            PAGE_PRESENT | PAGE_RW | PAGE_USER);
        }
        paging_switch_pml4(saved_cr3);

        p->brk = USER_BASE;
    } else {
        /* Kernel thread shares the kernel address space. */
        p->pml4_phys = VIRT_TO_PHYS((uintptr_t)kernel_pml4);
        p->ustack    = 0;
        p->brk       = 0;
    }

    /* 5. Initial register context, placed at the top of the kernel stack.
     *    switch_context() loads rsp = ctx and restores the full frame
     *    (GPRs + iret frame), so the first "return" lands at entry. */
    p->ctx = (registers_t*)(kstack_top - sizeof(registers_t));
    memset(p->ctx, 0, sizeof(registers_t));

    p->ctx->rip    = (uintptr_t)entry;
    p->ctx->rflags = 0x202;   /* IF set + reserved bit */

    if (user) {
        p->ctx->rsp = USER_STACK_TOP;
        p->ctx->cs  = 0x18 | 3;   /* user code, RPL3 */
        p->ctx->ss  = 0x20 | 3;   /* user data, RPL3 */
    } else {
        p->ctx->rsp = kstack_top;
        p->ctx->cs  = 0x08;       /* kernel code */
        p->ctx->ss  = 0x10;       /* kernel data */
    }

    /* 6. Insert into the round-robin ready queue. */
    scheduler_add(p);

    return p->pid;
}

/* ------------------------------------------------------------------ */
/* process_exit — mark current as zombie and yield away (never returns).*/
/* ------------------------------------------------------------------ */
void process_exit(int code)
{
    process_t* cur = scheduler_current();
    if (!cur) {
        return;
    }
    cur->state     = PROC_ZOMBIE;
    cur->exit_code = code;
    yield();
    /* Unreachable; guard against a buggy yield that returns. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* ------------------------------------------------------------------ */
/* process_by_pid — walk the circular ready queue.                     */
/* ------------------------------------------------------------------ */
process_t* process_by_pid(pid_t pid)
{
    process_t* p;
    if (!ready_head) {
        return NULL;
    }
    p = ready_head;
    do {
        if (p->pid == pid) {
            return p;
        }
        p = p->next;
    } while (p != ready_head);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* process_block / process_unblock                                     */
/* ------------------------------------------------------------------ */
void process_block(process_t* p)
{
    if (!p) {
        return;
    }
    if (p->state == PROC_RUNNING || p->state == PROC_READY) {
        p->state = PROC_BLOCKED;
    }
    if (p == scheduler_current()) {
        yield();
    }
}

void process_unblock(process_t* p)
{
    if (p && p->state == PROC_BLOCKED) {
        p->state = PROC_READY;
    }
}

/* ------------------------------------------------------------------ */
/* process_kill — asynchronously mark a process as a zombie.           */
/* PID 0 (idle) is immune.                                             */
/* ------------------------------------------------------------------ */
void process_kill(pid_t pid)
{
    process_t* p;
    if (pid == PID_KERNEL) {
        return;   /* never kill idle */
    }
    p = process_by_pid(pid);
    if (!p) {
        return;
    }
    p->state     = PROC_ZOMBIE;
    p->exit_code = -1;
    if (p == scheduler_current()) {
        yield();
    }
}

/* ------------------------------------------------------------------ */
/* process_current_pid                                                 */
/* ------------------------------------------------------------------ */
pid_t process_current_pid(void)
{
    process_t* cur = scheduler_current();
    return cur ? cur->pid : PID_KERNEL;
}

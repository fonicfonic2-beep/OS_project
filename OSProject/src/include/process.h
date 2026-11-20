#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "isr.h"

typedef int pid_t;

#define PID_KERNEL  0
#define PID_INIT    1

#define PROC_NAME_MAX  32
#define KSTACK_SIZE    (1024 * 16)   /* 16 KiB kernel stack per process */
#define USTACK_SIZE    (1024 * 1024) /* 1 MiB user stack                */

typedef enum proc_state {
    PROC_READY   = 0,
    PROC_RUNNING = 1,
    PROC_BLOCKED = 2,
    PROC_ZOMBIE  = 3
} proc_state_t;

typedef struct process {
    pid_t        pid;
    char         name[PROC_NAME_MAX];
    proc_state_t state;
    uintptr_t    pml4_phys;   /* physical address of process PML4 */
    uintptr_t    kstack;      /* kernel stack base (virtual)      */
    uintptr_t    ustack;      /* user stack base (virtual)        */
    uintptr_t    entry;       /* entry point address              */
    uintptr_t    brk;         /* program break (sbrk)             */
    registers_t* ctx;         /* saved register context (points into kstack) */
    uint64_t     cpu_time;    /* ticks consumed                   */
    int          exit_code;
    struct process* next;     /* ready-queue link                 */
    struct process* parent;
} process_t;

pid_t      process_create(const char* name, void (*entry)(void), bool user);
void       process_exit(int code);
process_t* process_by_pid(pid_t pid);
void       process_block(process_t* p);
void       process_unblock(process_t* p);
void       process_kill(pid_t pid);
pid_t      process_current_pid(void);

#endif /* PROCESS_H */

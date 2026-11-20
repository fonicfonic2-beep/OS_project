#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

/* System call numbers (passed in rax) */
#define SYS_exit    1
#define SYS_fork    2
#define SYS_read    3
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_yield   7
#define SYS_sbrk    8
#define SYS_exec    9
#define SYS_getpid  10
#define SYS_MAX     11

/* Syscall argument convention: rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5.
 * Return value in rax. Negative values are -errno. */
typedef uint64_t (*syscall_fn_t)(uint64_t a1, uint64_t a2, uint64_t a3,
                                 uint64_t a4, uint64_t a5);

void     syscall_init(void);
uint64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5);

extern syscall_fn_t syscall_table[SYS_MAX];

#endif /* SYSCALL_H */

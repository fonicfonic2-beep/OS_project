/*
 * syscall.c — System call dispatch via int 0x80 (vector 0x80, DPL3).
 *
 * Convention (syscall.h):  nr in rax, args in rdi, rsi, rdx, r10, r8, r9.
 * Return value (or -errno) written back to rax.  The C handler receives
 * a full registers_t* built by the asm stub.
 */
#include "types.h"
#include "common.h"
#include "isr.h"
#include "syscall.h"
#include "process.h"
#include "scheduler.h"
#include "console.h"
#include "vfs.h"
#include "string.h"

/* --- errno values (subset; returned negated) ------------------------ */
#define EPERM    1
#define ENOENT   2
#define EBADF    9
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define EINVAL  22
#define ENOSYS  38

/* --- Per-process (here: global, process_t is frozen) fd table ------- */
static file_t* fd_table[VFS_FD_MAX];   /* slots 0,1,2 reserved */

/* ------------------------------------------------------------------ */
/* user_ptr_ok — validate that [p, p+len) lies entirely below the      */
/* kernel half (user-accessible) and does not wrap around.             */
/* ------------------------------------------------------------------ */
bool user_ptr_ok(const void* p, size_t len)
{
    uintptr_t a = (uintptr_t)p;
    if (a >= KERNEL_VBASE) {
        return false;
    }
    if (len > KERNEL_VBASE - a) {   /* overflow / wraps into kernel */
        return false;
    }
    return true;
}

/* --- individual syscall handlers ------------------------------------ */

static uint64_t sys_exit(uint64_t code, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    process_exit((int)code);
    return 0;   /* unreachable */
}

static uint64_t sys_fork(uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return (uint64_t)(-1);   /* not yet supported */
}

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                         uint64_t a4, uint64_t a5)
{
    char* dst = (char*)buf;
    (void)a4; (void)a5;

    if (count == 0) {
        return 0;
    }
    if (!user_ptr_ok(dst, count)) {
        return (uint64_t)(-EFAULT);
    }

    if (fd == 0) {
        /* stdin = keyboard: blocking read of one byte. */
        char c = (char)console_getchar();
        dst[0] = c;
        return 1;
    }

    if (fd >= VFS_FD_MAX || !fd_table[fd]) {
        return (uint64_t)(-EBADF);
    }
    return (uint64_t)vfs_read(fd_table[fd], dst, count);
}

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                          uint64_t a4, uint64_t a5)
{
    const char* src = (const char*)buf;
    size_t i;
    (void)a4; (void)a5;

    if (!user_ptr_ok(src, count)) {
        return (uint64_t)(-EFAULT);
    }

    if (fd == 1 || fd == 2) {
        /* stdout/stderr -> console. */
        for (i = 0; i < count; i++) {
            console_putc(src[i]);
        }
        return count;
    }

    if (fd >= VFS_FD_MAX || !fd_table[fd]) {
        return (uint64_t)(-EBADF);
    }
    return (uint64_t)vfs_write(fd_table[fd], src, count);
}

static uint64_t sys_open(uint64_t path, uint64_t flags, uint64_t a3,
                         uint64_t a4, uint64_t a5)
{
    const char* p = (const char*)path;
    file_t* f = NULL;
    size_t len;
    int fd, rc;
    (void)a3; (void)a4; (void)a5;

    if ((uintptr_t)p >= KERNEL_VBASE) {
        return (uint64_t)(-EFAULT);
    }
    len = strnlen(p, VFS_PATH_MAX);
    if (!user_ptr_ok(p, len + 1)) {
        return (uint64_t)(-EFAULT);
    }

    rc = vfs_open(p, (int)flags, &f);
    if (rc < 0 || !f) {
        return (uint64_t)(rc < 0 ? rc : -ENOENT);
    }

    for (fd = 3; fd < (int)VFS_FD_MAX; fd++) {
        if (!fd_table[fd]) {
            fd_table[fd] = f;
            return (uint64_t)fd;
        }
    }
    vfs_close(f);
    return (uint64_t)(-ENOMEM);   /* table full */
}

static uint64_t sys_close(uint64_t fd, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (fd < 3 || fd >= VFS_FD_MAX || !fd_table[fd]) {
        return (uint64_t)(-EBADF);
    }
    vfs_close(fd_table[fd]);
    fd_table[fd] = NULL;
    return 0;
}

static uint64_t sys_yield(uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    yield();
    return 0;
}

static uint64_t sys_sbrk(uint64_t increment, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5)
{
    process_t* cur = scheduler_current();
    uintptr_t old;
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (!cur) {
        return (uint64_t)(-EINVAL);
    }
    old      = cur->brk;
    cur->brk = old + increment;
    return (uint64_t)old;
}

static uint64_t sys_exec(uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return (uint64_t)(-1);   /* not yet supported */
}

static uint64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return (uint64_t)process_current_pid();
}

/* --- syscall table (index 0 unused) --------------------------------- */
syscall_fn_t syscall_table[SYS_MAX] = {
    [0]            = NULL,
    [SYS_exit]     = sys_exit,
    [SYS_fork]     = sys_fork,
    [SYS_read]     = sys_read,
    [SYS_write]    = sys_write,
    [SYS_open]     = sys_open,
    [SYS_close]    = sys_close,
    [SYS_yield]    = sys_yield,
    [SYS_sbrk]     = sys_sbrk,
    [SYS_exec]     = sys_exec,
    [SYS_getpid]   = sys_getpid,
};

/* ------------------------------------------------------------------ */
/* syscall_dispatch — nr in [1, SYS_MAX-1], five integer args.         */
/* ------------------------------------------------------------------ */
uint64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5)
{
    syscall_fn_t fn;
    if (nr == 0 || nr >= SYS_MAX) {
        return (uint64_t)(-ENOSYS);
    }
    fn = syscall_table[nr];
    if (!fn) {
        return (uint64_t)(-ENOSYS);
    }
    return fn(a1, a2, a3, a4, a5);
}

/* ------------------------------------------------------------------ */
/* syscall_handler — registered as the ISR for vector 0x80.            */
/* Reads nr/args from the saved frame and writes the return value      */
/* back into rax (the asm stub restores it on iretq).                  */
/* ------------------------------------------------------------------ */
void syscall_handler(registers_t* r)
{
    uint64_t nr  = r->rax;
    uint64_t a1  = r->rdi;
    uint64_t a2  = r->rsi;
    uint64_t a3  = r->rdx;
    uint64_t a4  = r->r10;
    uint64_t a5  = r->r8;
    /* r9 is the 6th argument slot; current syscalls use at most 5. */
    r->rax = syscall_dispatch(nr, a1, a2, a3, a4, a5);
}

/* ------------------------------------------------------------------ */
/* syscall_init — install syscall_handler on vector 0x80.              */
/* (The IDT gate for 0x80 must be DPL3; set up by the arch layer.)     */
/* ------------------------------------------------------------------ */
void syscall_init(void)
{
    isr_install_handler(0x80, syscall_handler);
}

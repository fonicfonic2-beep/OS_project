/*
 * kernel/main.c — kernel entry point.
 *
 * Called from boot.asm as kernel_main(uint32_t magic, void* mbi).
 * Performs the full initialisation sequence, mounts the initrd filesystem
 * when present, selects the boot language, and finally drops into the
 * interactive shell. The shell is expected never to return; if it does,
 * that is a fatal error.
 */
#include "kernel.h"
#include "types.h"
#include "multiboot2.h"
#include "serial.h"
#include "vga.h"
#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "irq.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "i18n.h"
#include "keyboard.h"
#include "mouse.h"
#include "pit.h"
#include "rtc.h"
#include "apic.h"
#include "initrd.h"
#include "fat32.h"
#include "vfs.h"
#include "scheduler.h"
#include "syscall.h"
#include "shell.h"
#include "io.h"
#include "stdio.h"

/* Saved multiboot2 information pointer (declared extern in kernel.h). */
multiboot_info_t* g_multiboot = NULL;

/*
 * g_kernel_phys_start / g_kernel_phys_end are provided by the linker
 * script / boot module and are intentionally not defined here.
 */

/* Walk the multiboot2 tag list and return the framebuffer tag, or NULL. */
static multiboot_tag_framebuffer_t* find_framebuffer_tag(multiboot_info_t* mbi)
{
    multiboot_tag_t* tag;

    if (mbi == NULL) {
        return NULL;
    }
    tag = MULTIBOOT_TAG_FIRST(mbi);
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            return (multiboot_tag_framebuffer_t*)tag;
        }
        tag = MULTIBOOT_TAG_NEXT(tag);
    }
    return NULL;
}

void kernel_main(uint32_t magic, void* mbi_ptr)
{
    multiboot_info_t* mbi = (multiboot_info_t*)mbi_ptr;
    multiboot_tag_framebuffer_t* fb_tag;
    size_t initrd_size = 0;
    void* initrd_base;
    inode_t* fs_root;
    uint64_t current_rsp;

    /* Bring up serial first so early failures are at least visible there. */
    serial_init();

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        serial_write_str("kernel_main: invalid multiboot2 magic number\n");
        khalt();
    }

    g_multiboot = mbi;

    /* --- Display / console --- */
    vga_init();
    console_init();
    console_puts("DolaOS x86_64 kernel starting...\n");

    /* --- CPU mode / descriptor tables --- */
    gdt_init();
    __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));
    tss_set_stack(current_rsp);
    idt_init();
    isr_init();
    irq_init();

    /* --- Memory management --- */
    pmm_init(mbi);
    paging_init();
    kheap_init();

    /* --- Graphics mode (optional, only if a framebuffer was provided) --- */
    fb_tag = find_framebuffer_tag(mbi);
    if (fb_tag != NULL) {
        vga_gfx_init(fb_tag);
    }

    /* --- Language selection (interactive; defaults to Vietnamese) --- */
    console_puts("\n");
    (void)i18n_boot_menu();

    /* --- Input / timers / interrupt controllers --- */
    keyboard_init();
    mouse_init();
    pit_init(1000);
    rtc_init();
    apic_init();

    /* --- Filesystem: initrd -> FAT32 -> VFS mount at "/" --- */
    initrd_base = initrd_init(mbi, &initrd_size);
    if (initrd_base != NULL) {
        fs_root = fat32_mount(initrd_base, initrd_size);
        if (fs_root != NULL) {
            if (vfs_mount("/", fs_root) == 0) {
                console_printf("%s\n", tr("mount_ok"));
            } else {
                console_printf("%s\n", tr("mount_fail"));
            }
        } else {
            console_printf("%s\n", tr("mount_fail"));
        }
    } else {
        console_printf("%s\n", tr("no_initrd"));
    }

    /* --- Multitasking / system calls --- */
    scheduler_init();
    syscall_init();

    /* Enable interrupts and hand control to the shell. */
    sti();

    console_printf("\n%s\n\n", tr(L_KEY_WELCOME));

    shell_run();

    /* shell_run() is an infinite loop; reaching here is impossible. */
    kpanic("shell_run() returned; control flow corrupted.\n");
}

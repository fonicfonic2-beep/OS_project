/*
 * apps/taskmanager.c — live process / system monitor.
 *
 * Refreshes once per second for up to five seconds. Prints a table of
 * PID / NAME / STATE / CPU_TICKS plus free memory (pages) and the
 * current PIT tick count. Any keypress exits early.
 */
#include "apps.h"
#include "types.h"
#include "console.h"
#include "i18n.h"
#include "process.h"
#include "scheduler.h"
#include "pmm.h"
#include "pit.h"
#include "keyboard.h"
#include "stdio.h"

/* Provided by the task agent module. */
extern process_t* process_list_head(void);

#define TASKMGR_SECONDS 5

static const char* state_str(proc_state_t st)
{
    switch (st) {
    case PROC_READY:   return tr("state_ready");
    case PROC_RUNNING: return tr("state_running");
    case PROC_BLOCKED: return tr("state_blocked");
    case PROC_ZOMBIE:  return tr("state_zombie");
    default:           return "?";
    }
}

void app_taskmanager(void)
{
    int sec;

    for (sec = 0; sec < TASKMGR_SECONDS; sec++) {
        process_t* p;
        process_t* cur;
        uint32_t free_pages;
        unsigned tick_lo;

        if (keyboard_haschar()) {
            (void)keyboard_getchar();
            break;
        }

        console_clear();
        console_printf("=== %s (%d/%d s) ===\n",
                       tr(L_KEY_TASKMANAGER), sec + 1, TASKMGR_SECONDS);
        console_printf("%-5s %-20s %-12s %s\n",
                       tr("pid"), tr("proc_name"), tr("state"), tr("cpu_ticks"));
        console_printf("--------------------------------------------------\n");

        cur = scheduler_current();
        p = process_list_head();
        if (p == NULL) {
            console_printf("  (no processes)\n");
        }
        while (p != NULL) {
            console_printf("%c%-4u %-20s %-12s %u\n",
                           (p == cur) ? '*' : ' ',
                           (unsigned)p->pid,
                           p->name,
                           state_str(p->state),
                           (unsigned)(uint32_t)p->cpu_time);
            p = p->next;
        }

        free_pages = pmm_free_count();
        tick_lo = (unsigned)(uint32_t)pit_ticks();
        console_printf("--------------------------------------------------\n");
        console_printf("%s: %u pages (%u KiB)\n",
                       tr("free_mem"), free_pages, free_pages * 4u);
        console_printf("%s: %u\n", tr("uptime_ticks"), tick_lo);

        pit_sleep(1000);
    }
}

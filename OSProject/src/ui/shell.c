/*
 * ui/shell.c — minimal interactive shell.
 *
 * Prints a prompt with the current working directory, reads a line
 * (with backspace editing), splits it into up to 16 arguments and
 * dispatches builtin commands. All user-visible messages go through
 * tr() so they follow the selected language.
 *
 * Also provides app_help() (declared in apps.h) which prints the
 * command reference.
 */
#include "shell.h"
#include "apps.h"
#include "types.h"
#include "console.h"
#include "i18n.h"
#include "vfs.h"
#include "keyboard.h"
#include "process.h"
#include "io.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "ctype.h"

#define SHELL_MAX_ARGS   16
#define SHELL_LINE_MAX   256
#define SHELL_IO_BUF     1024

/* process_list_head() is provided by the task agent module. */
extern process_t* process_list_head(void);

static char s_cwd[VFS_PATH_MAX];

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void resolve_path(const char* arg, char* out, size_t outsz)
{
    if (arg == NULL || arg[0] == '\0') {
        strncpy(out, s_cwd, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }
    if (arg[0] == '/') {
        strncpy(out, arg, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }
    if (strcmp(s_cwd, "/") == 0) {
        snprintf(out, outsz, "/%s", arg);
    } else {
        snprintf(out, outsz, "%s/%s", s_cwd, arg);
    }
}

static int read_line(char* buf, size_t max)
{
    size_t pos = 0;
    int c;

    for (;;) {
        c = console_getchar();
        if (c == 0) {
            continue; /* non-printable key, ignore */
        }
        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            console_putc('\n');
            return (int)pos;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                console_putc('\b');
            }
            continue;
        }
        if (c == '\t') {
            c = ' ';
        }
        if (pos < max - 1) {
            buf[pos++] = (char)c;
            console_putc((char)c);
        }
    }
}

static int parse_args(char* line, char** argv)
{
    int argc = 0;
    char* p = line;

    while (*p != '\0' && argc < SHELL_MAX_ARGS) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* ------------------------------------------------------------------ */
/* app_help (declared in apps.h)                                       */
/* ------------------------------------------------------------------ */

void app_help(void)
{
    console_printf("=== %s ===\n", tr("help_header"));
    console_printf("  help              - %s\n", tr(L_KEY_HELP));
    console_printf("  ls [dir]          - %s\n", tr(L_KEY_LS));
    console_printf("  cd <dir>          - %s\n", tr(L_KEY_CD));
    console_printf("  pwd               - %s\n", tr(L_KEY_PWD));
    console_printf("  cat <file>        - %s\n", tr(L_KEY_CAT));
    console_printf("  echo <text>       - %s\n", tr(L_KEY_ECHO));
    console_printf("  clear             - %s\n", tr(L_KEY_CLEAR));
    console_printf("  mkdir <name>      - %s\n", tr(L_KEY_MKDIR));
    console_printf("  rm <name>         - %s\n", tr(L_KEY_RM));
    console_printf("  mv <old> <new>    - %s\n", tr("mv"));
    console_printf("  edit <file>       - %s\n", tr("edit"));
    console_printf("  calc              - %s\n", tr("calc"));
    console_printf("  fm                - %s\n", tr(L_KEY_FILEMANAGER));
    console_printf("  taskmgr           - %s\n", tr("taskmgr"));
    console_printf("  ps                - %s\n", tr("ps"));
    console_printf("  clock             - %s\n", tr(L_KEY_CLOCK));
    console_printf("  lang              - %s\n", tr("lang"));
    console_printf("  reboot            - %s\n", tr("reboot"));
    console_printf("  shutdown          - %s\n", tr("shutdown"));
    console_printf("  apps              - %s\n", tr(L_KEY_APPS));
    console_printf("  exit              - %s\n", tr(L_KEY_EXIT));
}

/* ------------------------------------------------------------------ */
/* Builtin commands                                                    */
/* ------------------------------------------------------------------ */

static void cmd_ls(int argc, char** argv)
{
    char path[VFS_PATH_MAX];
    file_t* dir;
    struct dirent de;

    resolve_path((argc >= 2) ? argv[1] : NULL, path, sizeof(path));

    if (vfs_opendir(path, &dir) != 0) {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr(L_KEY_NOT_A_DIR));
        return;
    }
    while (vfs_readdir(dir, &de) == 0) {
        if (S_ISDIR(de.type)) {
            console_printf("  %s/\n", de.name);
        } else {
            console_printf("  %s\n", de.name);
        }
    }
    vfs_close(dir);
}

static void cmd_cd(int argc, char** argv)
{
    char path[VFS_PATH_MAX];

    if (argc < 2) {
        strcpy(path, "/");
    } else {
        resolve_path(argv[1], path, sizeof(path));
    }
    if (vfs_chdir(path) == 0) {
        if (vfs_getcwd(s_cwd, sizeof(s_cwd)) == NULL) {
            strncpy(s_cwd, path, sizeof(s_cwd) - 1);
            s_cwd[sizeof(s_cwd) - 1] = '\0';
        }
    } else {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr("dir_not_found"));
    }
}

static void cmd_cat(int argc, char** argv)
{
    char path[VFS_PATH_MAX];
    file_t* f;
    char buf[SHELL_IO_BUF];
    ssize_t n;
    ssize_t i;

    if (argc < 2) {
        console_printf("%s: %s cat <file>\n", tr("usage"), tr(L_KEY_CAT));
        return;
    }
    resolve_path(argv[1], path, sizeof(path));
    if (vfs_open(path, O_RDONLY, &f) != 0) {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr(L_KEY_FILE_NOT_FOUND));
        return;
    }
    while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            console_putc(buf[i]);
        }
    }
    vfs_close(f);
    console_putc('\n');
}

static void cmd_echo(int argc, char** argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (i > 1) {
            console_putc(' ');
        }
        console_puts(argv[i]);
    }
    console_putc('\n');
}

static void cmd_mkdir(int argc, char** argv)
{
    char path[VFS_PATH_MAX];
    if (argc < 2) {
        console_printf("%s: %s mkdir <name>\n", tr("usage"), tr(L_KEY_MKDIR));
        return;
    }
    resolve_path(argv[1], path, sizeof(path));
    if (vfs_mkdir(path) == 0) {
        console_printf("%s: %s\n", tr("created_ok"), argv[1]);
    } else {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr(L_KEY_PERM_DENIED));
    }
}

static void cmd_rm(int argc, char** argv)
{
    char path[VFS_PATH_MAX];
    if (argc < 2) {
        console_printf("%s: %s rm <name>\n", tr("usage"), tr(L_KEY_RM));
        return;
    }
    resolve_path(argv[1], path, sizeof(path));
    if (vfs_unlink(path) == 0) {
        console_printf("%s: %s\n", tr("deleted_ok"), argv[1]);
    } else {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr(L_KEY_FILE_NOT_FOUND));
    }
}

static void cmd_mv(int argc, char** argv)
{
    char oldp[VFS_PATH_MAX];
    char newp[VFS_PATH_MAX];
    if (argc < 3) {
        console_printf("%s: %s mv <old> <new>\n", tr("usage"), tr("mv"));
        return;
    }
    resolve_path(argv[1], oldp, sizeof(oldp));
    resolve_path(argv[2], newp, sizeof(newp));
    if (vfs_rename(oldp, newp) == 0) {
        console_printf("%s\n", tr("renamed_ok"));
    } else {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr(L_KEY_FILE_NOT_FOUND));
    }
}

static void cmd_ps(void)
{
    process_t* p = process_list_head();
    if (p == NULL) {
        console_printf("  (no processes)\n");
        return;
    }
    console_printf("  %-5s %-20s %-10s %s\n",
                   tr("pid"), tr("proc_name"), tr("state"), tr("cpu_ticks"));
    while (p != NULL) {
        const char* st;
        switch (p->state) {
        case PROC_READY:   st = tr("state_ready");   break;
        case PROC_RUNNING: st = tr("state_running"); break;
        case PROC_BLOCKED: st = tr("state_blocked"); break;
        case PROC_ZOMBIE:  st = tr("state_zombie");  break;
        default:           st = "?";                 break;
        }
        console_printf("  %-5u %-20s %-10s %u\n",
                       (unsigned)p->pid, p->name, st,
                       (unsigned)(uint32_t)p->cpu_time);
        p = p->next;
    }
}

static void cmd_apps(void)
{
    console_printf("=== %s ===\n", tr("apps_list"));
    console_printf("  edit <file>  - %s\n", tr(L_KEY_EDITOR));
    console_printf("  calc         - %s\n", tr(L_KEY_CALCULATOR));
    console_printf("  fm           - %s\n", tr(L_KEY_FILEMANAGER));
    console_printf("  taskmgr      - %s\n", tr(L_KEY_TASKMANAGER));
    console_printf("  clock        - %s\n", tr(L_KEY_CLOCK));
}

static void cmd_reboot(void)
{
    console_printf("%s\n", tr("rebooting"));
    /* Pulse the CPU reset line via the keyboard controller. */
    outb(0x64, 0xFE);
    /* Fallback if reset did not happen: halt. */
    cli();
    for (;;) {
        hlt();
    }
}

static void cmd_shutdown(void)
{
    console_printf("%s\n", tr("shutting_down"));
    cli();
    for (;;) {
        hlt();
    }
}

/* ------------------------------------------------------------------ */
/* Public shell API                                                    */
/* ------------------------------------------------------------------ */

void shell_init(void)
{
    strcpy(s_cwd, "/");
}

int shell_exec(const char* line)
{
    char buf[SHELL_LINE_MAX];
    char* argv[SHELL_MAX_ARGS + 1];
    int argc;
    char path[VFS_PATH_MAX];

    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    argc = parse_args(buf, argv);
    if (argc == 0) {
        return 0;
    }

    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        app_help();
    } else if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "dir") == 0) {
        cmd_ls(argc, argv);
    } else if (strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "chdir") == 0) {
        cmd_cd(argc, argv);
    } else if (strcmp(argv[0], "pwd") == 0) {
        console_printf("%s\n", s_cwd);
    } else if (strcmp(argv[0], "cat") == 0 || strcmp(argv[0], "type") == 0) {
        cmd_cat(argc, argv);
    } else if (strcmp(argv[0], "echo") == 0) {
        cmd_echo(argc, argv);
    } else if (strcmp(argv[0], "clear") == 0 || strcmp(argv[0], "cls") == 0) {
        console_clear();
    } else if (strcmp(argv[0], "mkdir") == 0 || strcmp(argv[0], "md") == 0) {
        cmd_mkdir(argc, argv);
    } else if (strcmp(argv[0], "rm") == 0 || strcmp(argv[0], "del") == 0) {
        cmd_rm(argc, argv);
    } else if (strcmp(argv[0], "mv") == 0 || strcmp(argv[0], "rename") == 0) {
        cmd_mv(argc, argv);
    } else if (strcmp(argv[0], "edit") == 0 || strcmp(argv[0], "editor") == 0) {
        if (argc < 2) {
            console_printf("%s: %s edit <file>\n", tr("usage"), tr("edit"));
        } else {
            resolve_path(argv[1], path, sizeof(path));
            app_editor(path);
        }
    } else if (strcmp(argv[0], "calc") == 0 || strcmp(argv[0], "calculator") == 0) {
        app_calculator();
    } else if (strcmp(argv[0], "fm") == 0 || strcmp(argv[0], "filemanager") == 0 ||
               strcmp(argv[0], "files") == 0) {
        app_filemanager();
    } else if (strcmp(argv[0], "taskmgr") == 0 || strcmp(argv[0], "tasks") == 0 ||
               strcmp(argv[0], "top") == 0) {
        app_taskmanager();
    } else if (strcmp(argv[0], "ps") == 0) {
        cmd_ps();
    } else if (strcmp(argv[0], "clock") == 0 || strcmp(argv[0], "time") == 0) {
        app_clock();
    } else if (strcmp(argv[0], "lang") == 0 || strcmp(argv[0], "language") == 0) {
        (void)i18n_boot_menu();
        console_printf("%s\n", tr("lang_changed"));
    } else if (strcmp(argv[0], "reboot") == 0 || strcmp(argv[0], "restart") == 0) {
        cmd_reboot();
    } else if (strcmp(argv[0], "shutdown") == 0 || strcmp(argv[0], "halt") == 0 ||
               strcmp(argv[0], "poweroff") == 0) {
        cmd_shutdown();
    } else if (strcmp(argv[0], "apps") == 0) {
        cmd_apps();
    } else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        console_printf("%s\n", tr("exit_warn"));
    } else {
        console_printf("%s: '%s'\n", tr(L_KEY_UNKNOWN_CMD), argv[0]);
    }
    return 0;
}

void shell_run(void)
{
    char line[SHELL_LINE_MAX];

    shell_init();

    for (;;) {
        console_printf("%s%s> ", tr(L_KEY_PROMPT), s_cwd);
        read_line(line, sizeof(line));
        if (line[0] == '\0') {
            continue;
        }
        shell_exec(line);
    }
}

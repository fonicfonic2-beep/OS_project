/*
 * apps/filemanager.c — console file manager.
 *
 * Lists the current directory as [DIR] / [FILE] entries (file sizes are
 * looked up by opening each regular file). Arrow keys move the selection,
 * Enter opens a file (cats it) or enters a directory, 'd' deletes the
 * selected entry, 'r' renames it, 'q' quits.
 */
#include "apps.h"
#include "types.h"
#include "console.h"
#include "i18n.h"
#include "vfs.h"
#include "keyboard.h"
#include "pit.h"
#include "stdio.h"
#include "string.h"

#define FM_MAX_ENTRIES 256
#define FM_IO_BUF      1024

typedef struct {
    char     name[VFS_NAME_MAX];
    uint32_t type;
    uint32_t size;
} fm_entry_t;

static fm_entry_t fm_entries[FM_MAX_ENTRIES];
static int        fm_count;
static int        fm_selected;
static char       fm_cwd[VFS_PATH_MAX];

static void fm_full_path(const char* name, char* out, size_t outsz)
{
    if (strcmp(fm_cwd, "/") == 0) {
        snprintf(out, outsz, "/%s", name);
    } else {
        snprintf(out, outsz, "%s/%s", fm_cwd, name);
    }
}

static int fm_load(void)
{
    file_t* dir;
    struct dirent de;

    fm_count = 0;
    fm_selected = 0;

    if (vfs_opendir(fm_cwd, &dir) != 0) {
        return -1;
    }
    while (vfs_readdir(dir, &de) == 0 && fm_count < FM_MAX_ENTRIES) {
        char full[VFS_PATH_MAX];
        file_t* f;

        strncpy(fm_entries[fm_count].name, de.name, VFS_NAME_MAX - 1);
        fm_entries[fm_count].name[VFS_NAME_MAX - 1] = '\0';
        fm_entries[fm_count].type = de.type;
        fm_entries[fm_count].size = 0;

        if (S_ISREG(de.type)) {
            fm_full_path(de.name, full, sizeof(full));
            if (vfs_open(full, O_RDONLY, &f) == 0) {
                if (f->inode != NULL) {
                    fm_entries[fm_count].size = f->inode->size;
                }
                vfs_close(f);
            }
        }
        fm_count++;
    }
    vfs_close(dir);
    return 0;
}

static void fm_redraw(void)
{
    int i;

    console_clear();
    console_printf("=== %s: %s ===\n", tr(L_KEY_FILEMANAGER), fm_cwd);
    console_printf("%s\n", tr("fm_help"));
    console_printf("--------------------------------------------------\n");

    if (fm_count == 0) {
        console_printf("  %s\n", tr("fm_empty"));
    }
    for (i = 0; i < fm_count; i++) {
        char marker = (i == fm_selected) ? '>' : ' ';
        if (S_ISDIR(fm_entries[i].type)) {
            console_printf("%c [DIR]  %s/\n", marker, fm_entries[i].name);
        } else {
            console_printf("%c [FILE] %-20s %u B\n",
                           marker, fm_entries[i].name, fm_entries[i].size);
        }
    }
}

static void fm_view_file(const char* name)
{
    char full[VFS_PATH_MAX];
    file_t* f;
    char buf[FM_IO_BUF];
    ssize_t n;
    ssize_t i;

    fm_full_path(name, full, sizeof(full));
    console_clear();
    console_printf("===== %s =====\n", name);
    if (vfs_open(full, O_RDONLY, &f) != 0) {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr(L_KEY_FILE_NOT_FOUND));
    } else {
        while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
            for (i = 0; i < n; i++) {
                console_putc(buf[i]);
            }
        }
        vfs_close(f);
    }
    console_printf("\n\n%s", tr("press_any_key"));
    (void)console_getchar();
}

static void fm_read_line(char* buf, size_t max)
{
    size_t pos = 0;
    int c;
    for (;;) {
        c = console_getchar();
        if (c == 0) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            console_putc('\n');
            return;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                console_putc('\b');
            }
            continue;
        }
        if (pos < max - 1) {
            buf[pos++] = (char)c;
            console_putc((char)c);
        }
    }
}

void app_filemanager(void)
{
    if (vfs_getcwd(fm_cwd, sizeof(fm_cwd)) == NULL) {
        strcpy(fm_cwd, "/");
    }
    if (fm_load() != 0) {
        console_printf("%s: %s\n", tr(L_KEY_ERROR), tr("dir_not_found"));
        return;
    }

    for (;;) {
        int k;

        fm_redraw();

        /* Blocking poll for a full key code. */
        do {
            k = keyboard_getkey();
            if (k == 0) {
                pit_sleep(20);
            }
        } while (k == 0);

        if (k == KEY_UP) {
            if (fm_selected > 0) {
                fm_selected--;
            } else if (fm_count > 0) {
                fm_selected = fm_count - 1;
            }
        } else if (k == KEY_DOWN) {
            if (fm_selected < fm_count - 1) {
                fm_selected++;
            } else {
                fm_selected = 0;
            }
        } else if (k == KEY_ENTER || k == '\r' || k == '\n') {
            char full[VFS_PATH_MAX];
            if (fm_selected < 0 || fm_selected >= fm_count) {
                continue;
            }
            if (S_ISDIR(fm_entries[fm_selected].type)) {
                fm_full_path(fm_entries[fm_selected].name, full, sizeof(full));
                if (vfs_chdir(full) == 0) {
                    if (vfs_getcwd(fm_cwd, sizeof(fm_cwd)) == NULL) {
                        strncpy(fm_cwd, full, sizeof(fm_cwd) - 1);
                        fm_cwd[sizeof(fm_cwd) - 1] = '\0';
                    }
                    fm_load();
                }
            } else {
                fm_view_file(fm_entries[fm_selected].name);
            }
        } else if (k == 'd' || k == 'D') {
            char full[VFS_PATH_MAX];
            if (fm_selected >= 0 && fm_selected < fm_count) {
                fm_full_path(fm_entries[fm_selected].name, full, sizeof(full));
                vfs_unlink(full);
                fm_load();
            }
        } else if (k == 'r' || k == 'R') {
            char oldp[VFS_PATH_MAX];
            char newp[VFS_PATH_MAX];
            char newname[VFS_NAME_MAX];
            if (fm_selected >= 0 && fm_selected < fm_count) {
                fm_full_path(fm_entries[fm_selected].name, oldp, sizeof(oldp));
                console_printf("%s", tr("fm_rename_prompt"));
                fm_read_line(newname, sizeof(newname));
                if (newname[0] != '\0') {
                    fm_full_path(newname, newp, sizeof(newp));
                    vfs_rename(oldp, newp);
                }
                fm_load();
            }
        } else if (k == 'q' || k == 'Q' || k == KEY_ESCAPE) {
            break;
        }
        /* Any other key is ignored. */
    }
}

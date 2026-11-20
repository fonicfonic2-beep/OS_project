/*
 * apps/editor.c — minimal full-screen text editor.
 *
 * Loads a file (creating it if missing) into a 64 KiB buffer. Arrow keys
 * move the cursor, printable bytes are inserted, Backspace/Delete erase,
 * Ctrl+S writes the buffer back (truncate + rewrite) and Ctrl+Q quits.
 */
#include "apps.h"
#include "types.h"
#include "console.h"
#include "i18n.h"
#include "vfs.h"
#include "keyboard.h"
#include "heap.h"
#include "pit.h"
#include "vga.h"
#include "stdio.h"
#include "string.h"
#include "ctype.h"

#define EDITOR_MAX (64 * 1024)

static char* e_buf;          /* editable text buffer        */
static int   e_len;          /* current text length in bytes */
static int   e_cur;          /* cursor byte index            */
static int   e_max;          /* capacity (EDITOR_MAX)        */

/* Find [start, end) of the line containing byte index idx. */
static void line_bounds(int idx, int* start, int* end)
{
    int s = idx;
    int e = idx;

    while (s > 0 && e_buf[s - 1] != '\n') {
        s--;
    }
    while (e < e_len && e_buf[e] != '\n') {
        e++;
    }
    *start = s;
    *end = e;
}

static void cursor_up(void)
{
    int s, e, col, ps, pe, plen;

    line_bounds(e_cur, &s, &e);
    col = e_cur - s;
    if (s == 0) {
        e_cur = 0;
        return;
    }
    line_bounds(s - 1, &ps, &pe);
    plen = pe - ps;
    e_cur = ps + ((col < plen) ? col : plen);
}

static void cursor_down(void)
{
    int s, e, col, ns, ne, nlen, dummy;

    line_bounds(e_cur, &s, &e);
    col = e_cur - s;
    if (e >= e_len) {
        return; /* already on last line */
    }
    ns = e + 1; /* skip the newline */
    line_bounds(ns, &dummy, &ne);
    nlen = ne - ns;
    e_cur = ns + ((col < nlen) ? col : nlen);
}

static void insert_byte(int c)
{
    if (e_len >= e_max) {
        return; /* buffer full */
    }
    memmove(e_buf + e_cur + 1, e_buf + e_cur, (size_t)(e_len - e_cur + 1));
    e_buf[e_cur] = (char)c;
    e_len++;
    e_cur++;
}

static void editor_save(const char* file)
{
    file_t* f;

    if (vfs_open(file, O_WRONLY | O_TRUNC | O_CREAT, &f) != 0) {
        return;
    }
    (void)vfs_write(f, e_buf, (size_t)e_len);
    vfs_close(f);
}

static void editor_redraw(const char* file, int just_saved)
{
    int i;
    int x = 0;
    int y = 1; /* line 0 is the status bar */

    console_clear();
    console_printf("=== %s: %s | %s%s ===\n",
                   tr(L_KEY_EDITOR), file, tr("ed_help"),
                   just_saved ? " | " : "");
    if (just_saved) {
        console_printf("%s\n", tr("ed_saved"));
        y = 2;
    }

    for (i = 0; i < e_len; i++) {
        console_putc(e_buf[i]);
    }

    /* Compute cursor column/row from byte index (byte-granular). */
    x = 0;
    y = just_saved ? 2 : 1;
    for (i = 0; i < e_cur; i++) {
        if (e_buf[i] == '\n') {
            y++;
            x = 0;
        } else {
            x++;
            if (x >= VGA_TEXT_WIDTH) {
                x = 0;
                y++;
            }
        }
    }
    if (x >= VGA_TEXT_WIDTH) {
        x = VGA_TEXT_WIDTH - 1;
    }
    if (y >= VGA_TEXT_HEIGHT) {
        y = VGA_TEXT_HEIGHT - 1;
    }
    vga_set_cursor((uint8_t)x, (uint8_t)y);
}

void app_editor(const char* file)
{
    file_t* f;
    size_t to_read;
    ssize_t got;
    int saved_flash = 0;

    if (file == NULL || file[0] == '\0') {
        console_printf("%s: %s edit <file>\n", tr("usage"), tr("edit"));
        return;
    }

    e_buf = (char*)kmalloc(EDITOR_MAX + 1);
    if (e_buf == NULL) {
        console_printf("%s: kmalloc failed\n", tr(L_KEY_ERROR));
        return;
    }
    e_max = EDITOR_MAX;
    e_len = 0;
    e_cur = 0;
    e_buf[0] = '\0';

    /* Open for read/write, creating the file if it does not exist. */
    if (vfs_open(file, O_RDWR | O_CREAT, &f) == 0) {
        if (f->inode != NULL) {
            to_read = f->inode->size;
            if (to_read > (size_t)e_max) {
                to_read = (size_t)e_max;
            }
            got = vfs_read(f, e_buf, to_read);
            if (got > 0) {
                e_len = (int)got;
            }
        }
        e_buf[e_len] = '\0';
        vfs_close(f);
    }
    /* If open failed entirely we still allow editing an empty buffer. */

    for (;;) {
        int k;
        uint8_t mod;

        editor_redraw(file, saved_flash);
        saved_flash = 0;

        do {
            k = keyboard_getkey();
            if (k == 0) {
                pit_sleep(10);
            }
        } while (k == 0);

        mod = keyboard_modifiers();

        if (k == KEY_LEFT) {
            if (e_cur > 0) {
                e_cur--;
            }
        } else if (k == KEY_RIGHT) {
            if (e_cur < e_len) {
                e_cur++;
            }
        } else if (k == KEY_UP) {
            cursor_up();
        } else if (k == KEY_DOWN) {
            cursor_down();
        } else if (k == KEY_BACKSPACE || k == '\b') {
            if (e_cur > 0) {
                memmove(e_buf + e_cur - 1, e_buf + e_cur,
                        (size_t)(e_len - e_cur + 1));
                e_cur--;
                e_len--;
            }
        } else if (k == KEY_DELETE) {
            if (e_cur < e_len) {
                memmove(e_buf + e_cur, e_buf + e_cur + 1,
                        (size_t)(e_len - e_cur));
                e_len--;
            }
        } else if (k == KEY_ENTER || k == '\r' || k == '\n') {
            insert_byte('\n');
        } else if (k == 0x13 || ((mod & MOD_CTRL) && (k == 's' || k == 'S'))) {
            editor_save(file);
            saved_flash = 1;
        } else if (k == 0x11 || ((mod & MOD_CTRL) && (k == 'q' || k == 'Q'))) {
            break;
        } else if (k == '\t') {
            insert_byte(' ');
            insert_byte(' ');
            insert_byte(' ');
            insert_byte(' ');
        } else if (k > 0 && k < 256 && isprint(k)) {
            insert_byte(k);
        }
        /* Other keys (F-keys, etc.) are ignored. */
    }

    kfree(e_buf);
    console_clear();
}

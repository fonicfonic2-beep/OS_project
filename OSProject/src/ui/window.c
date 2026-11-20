/*
 * ui/window.c — minimal framebuffer window primitives.
 *
 * Windows are plain rectangles with a title bar; there is no compositing
 * or window manager. All drawing is done with the vga_gfx_* helpers and
 * the built-in 8x16 font. window_draw_string advances 8 pixels per char
 * and 16 pixels per line.
 */
#include "window.h"
#include "types.h"
#include "vga.h"

#define WINDOW_TITLE_BAR_H 16
#define WINDOW_FONT_W      8
#define WINDOW_FONT_H      16
#define WINDOW_TEXT_BG     0x000000u

void window_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    vga_gfx_fillrect(x, y, w, h, color);
}

void window_draw_border(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    vga_gfx_fillrect(x, y, w, 1, color);
    vga_gfx_fillrect(x, y + h - 1, w, 1, color);
    vga_gfx_fillrect(x, y, 1, h, color);
    vga_gfx_fillrect(x + w - 1, y, 1, h, color);
}

void window_draw_string(int x, int y, const char* s, uint32_t fg)
{
    int cx = x;

    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        if (*s == '\n') {
            cx = x;
            y += WINDOW_FONT_H;
            s++;
            continue;
        }
        vga_gfx_drawchar(cx, y, *s, fg, WINDOW_TEXT_BG);
        cx += WINDOW_FONT_W;
        s++;
    }
}

void window_draw(window_t* w)
{
    if (w == NULL) {
        return;
    }
    if (w->w <= 0 || w->h <= 0) {
        return;
    }

    /* Background fill. */
    vga_gfx_fillrect(w->x, w->y, w->w, w->h, w->color);

    /* Title bar (only if the window is tall enough). */
    if (w->h >= WINDOW_TITLE_BAR_H) {
        vga_gfx_fillrect(w->x, w->y, w->w, WINDOW_TITLE_BAR_H, w->title_color);
        if (w->title != NULL) {
            window_draw_string(w->x + 4, w->y, w->title, 0xFFFFFFFFu);
        }
    }

    /* Outer border. */
    window_draw_border(w->x, w->y, w->w, w->h, 0x000000u);
}

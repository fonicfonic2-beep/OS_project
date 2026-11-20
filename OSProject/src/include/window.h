#ifndef WINDOW_H
#define WINDOW_H

#include "types.h"

/*
 * Minimal graphics UI (framebuffer-based). Windows are simple
 * rectangles with a title bar; no compositing/window manager yet.
 */
typedef struct window {
    int         x;
    int         y;
    int         w;
    int         h;
    const char* title;
    uint32_t    color;       /* background color */
    uint32_t    title_color; /* title bar color  */
} window_t;

void window_draw(window_t* w);
void window_draw_string(int x, int y, const char* s, uint32_t fg);
void window_draw_rect(int x, int y, int w, int h, uint32_t color);
void window_draw_border(int x, int y, int w, int h, uint32_t color);

#endif /* WINDOW_H */

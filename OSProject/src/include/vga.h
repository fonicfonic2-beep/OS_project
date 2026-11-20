#ifndef VGA_H
#define VGA_H

#include "types.h"
#include "multiboot2.h"

/* --- Text mode (80x25, color at 0xB8000) --- */
#define VGA_TEXT_WIDTH   80
#define VGA_TEXT_HEIGHT  25
#define VGA_TEXT_MEM     0xB8000

/* 4-bit VGA text color indices */
#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_DARK_GREY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_LIGHT_BROWN   14
#define VGA_COLOR_WHITE         15

#define VGA_ENTRY_COLOR(fg, bg)  ((uint8_t)((fg) | ((bg) << 4)))

void vga_init(void);
void vga_putc(char c, uint8_t color);
void vga_puts(const char* s);
void vga_clear(uint8_t color);
void vga_set_cursor(uint8_t x, uint8_t y);
void vga_scroll(void);
void vga_enable_cursor(uint8_t start, uint8_t end);
void vga_disable_cursor(void);

extern int vga_row;
extern int vga_col;
extern uint8_t vga_color;

/* --- Graphics mode (linear framebuffer from multiboot2) --- */
typedef struct framebuffer_info {
    uintptr_t addr;        /* virtual address of framebuffer */
    uintptr_t phys_addr;   /* physical address               */
    uint32_t  pitch;
    uint32_t  width;
    uint32_t  height;
    uint8_t   bpp;
    uint8_t   type;        /* MULTIBOOT_FRAMEBUFFER_TYPE_* */
    uint8_t   red_pos, red_mask;
    uint8_t   green_pos, green_mask;
    uint8_t   blue_pos, blue_mask;
} framebuffer_info_t;

extern framebuffer_info_t g_framebuffer;
extern const uint8_t font8x16[256][16];

void vga_gfx_init(multiboot_tag_framebuffer_t* fb);
void vga_gfx_putpixel(int x, int y, uint32_t color);
uint32_t vga_gfx_getpixel(int x, int y);
void vga_gfx_fillrect(int x, int y, int w, int h, uint32_t color);
void vga_gfx_drawchar(int x, int y, char c, uint32_t fg, uint32_t bg);
void vga_gfx_drawstring(int x, int y, const char* s, uint32_t fg, uint32_t bg);
void vga_gfx_clearscreen(uint32_t color);
void vga_gfx_line(int x0, int y0, int x1, int y1, uint32_t color);

#endif /* VGA_H */

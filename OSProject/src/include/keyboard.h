#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KEY_BUFFER_SIZE  256

/* Special key codes returned by keyboard_getkey (>= 256, never a UTF-8 byte).
 * keyboard_getchar returns only printable/control bytes (0..255); for
 * non-printable keys it returns 0 and the code is available via keyboard_getkey. */
#define KEY_NONE        0
#define KEY_ENTER       0x0D   /* carriage return, fits in char */
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ESCAPE      0x1B
#define KEY_UP          0x100
#define KEY_DOWN        0x101
#define KEY_LEFT        0x102
#define KEY_RIGHT       0x103
#define KEY_F1          0x110
#define KEY_F2          0x111
#define KEY_F3          0x112
#define KEY_F4          0x113
#define KEY_F5          0x114
#define KEY_F6          0x115
#define KEY_F7          0x116
#define KEY_F8          0x117
#define KEY_F9          0x118
#define KEY_F10         0x119
#define KEY_F11         0x11A
#define KEY_F12         0x11B
#define KEY_INSERT      0x120
#define KEY_DELETE      0x121
#define KEY_HOME        0x122
#define KEY_END         0x123
#define KEY_PAGEUP      0x124
#define KEY_PAGEDOWN    0x125

/* Modifier key state bits */
#define MOD_SHIFT       0x01
#define MOD_CTRL        0x02
#define MOD_ALT         0x04
#define MOD_CAPSLOCK    0x08
#define MOD_NUMLOCK     0x10
#define MOD_SCROLLLOCK  0x20

/*
 * Scancode set 1 (XT/AT) driver with shift + capslock.
 * Vietnamese telex input (basic) composes accented characters and
 * emits them as UTF-8 into the keyboard buffer.
 */
void keyboard_init(void);
char keyboard_getchar(void);       /* blocking, returns one UTF-8 byte */
int  keyboard_haschar(void);       /* non-zero if a byte is available  */
int  keyboard_getkey(void);        /* non-blocking, returns full key code or 0 */
uint8_t keyboard_modifiers(void);
void keyboard_set_telex(bool enable);

#endif /* KEYBOARD_H */

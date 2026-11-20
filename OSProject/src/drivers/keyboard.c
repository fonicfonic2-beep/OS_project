/*
 * keyboard.c — PS/2 keyboard driver (scancode set 1 / XT).
 *
 * Reads bytes from port 0x60 on IRQ1, translates set-1 scancodes (with
 * 0xE0 extended prefix) into characters / special key codes, and buffers
 * them in a 256-entry ring buffer.  Shift, CapsLock and Ctrl are handled.
 *
 * A simple Vietnamese telex input method composes accented vowels and
 * emits them as UTF-8 bytes into the same buffer.  Telex can be toggled
 * at runtime with keyboard_set_telex() (or the F2 key).
 *
 * Special key codes (>= 256) are defined in <keyboard.h> and are never
 * returned as UTF-8 bytes by keyboard_getchar().
 */
#include "keyboard.h"
#include "io.h"
#include "irq.h"
#include "pic.h"

#define KBD_DATA_PORT   0x60
#define KBD_CMD_PORT    0x64
#define KBD_ENABLE_KBD  0xAE   /* controller command: enable first port */

#define EXTENDED_PREFIX 0xE0
#define BREAK_BIT       0x80

/* IRQ1 handler — called by irq.c (extern) and installed via the table. */
void keyboard_handler(registers_t* regs);

/* --- scancode set 1 translation tables (index = make code) ------------- */
static const uint8_t kbd_us_unshift[0x60] = {
    /* 00 */ 0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',
    /* 08 */ '7',  '8',  '9',  '0',  '-',  '=',  0x08, 0x09,
    /* 10 */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    /* 18 */ 'o',  'p',  '[',  ']',  0x0D, 0,    'a',  's',
    /* 20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    /* 28 */ '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    /* 30 */ 'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    /* 38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
    /* 40 */ 0,    0,    0,    0,    0,    0,    '7',  '8',
    /* 48 */ '9',  '-',  '4',  '5',  '6',  '+',  '1',  '2',
    /* 50 */ '3',  '0',  '.',  0,    0,    0,    0,    0,
    /* 58 */ 0
};

static const uint8_t kbd_us_shift[0x60] = {
    /* 00 */ 0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',
    /* 08 */ '&',  '*',  '(',  ')',  '_',  '+',  0x08, 0x09,
    /* 10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    /* 18 */ 'O',  'P',  '{',  '}',  0x0D, 0,    'A',  'S',
    /* 20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    /* 28 */ '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    /* 30 */ 'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    /* 38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
    /* 40 */ 0,    0,    0,    0,    0,    0,    '7',  '8',
    /* 48 */ '9',  '-',  '4',  '5',  '6',  '+',  '1',  '2',
    /* 50 */ '3',  '0',  '.',  0,    0,    0,    0,    0,
    /* 58 */ 0
};

/* Map extended (0xE0) make codes to special key codes. */
static int kbd_extended_special(uint8_t code)
{
    switch (code) {
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x49: return KEY_PAGEUP;
    case 0x51: return KEY_PAGEDOWN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    default:   return 0;
    }
}

/* --- ring buffer of full key codes (0..255 = UTF-8 byte, >255 = special) */
static int      g_kbuf[KEY_BUFFER_SIZE];
static uint8_t  g_kbuf_head = 0;   /* next write slot */
static uint8_t  g_kbuf_tail = 0;   /* next read slot  */

static void kbuf_put(int code)
{
    uint8_t next = (uint8_t)(g_kbuf_head + 1);
    if (next == g_kbuf_tail) {
        return;           /* full: drop */
    }
    g_kbuf[g_kbuf_head] = code;
    g_kbuf_head = next;
}

static int kbuf_get(void)
{
    int code;
    if (g_kbuf_head == g_kbuf_tail) {
        return -1;
    }
    code = g_kbuf[g_kbuf_tail];
    g_kbuf_tail = (uint8_t)(g_kbuf_tail + 1);
    return code;
}

static int kbuf_peek(void)
{
    if (g_kbuf_head == g_kbuf_tail) {
        return -1;
    }
    return g_kbuf[g_kbuf_tail];
}

/* Stashed special key for keyboard_getkey() after getchar() returned 0. */
static int g_pending_key = 0;

/* --- modifier state ---------------------------------------------------- */
static uint8_t g_modifiers = 0;
static bool    g_extended  = false;

/* --- Vietnamese telex compose state ------------------------------------
 * base: 0=a 1=e 2=o 3=u 4=i 5=y 6=d
 * form: 0=plain 1=circumflex(^) 2=breve(breve) 3=horn
 * tone: 0=none 1=acute(s) 2=grave(f) 3=hook(x) 4=tilde(r) 5=dot(j)
 */
static bool     g_telex_enabled = true;
static bool     g_compose_active = false;
static uint8_t  g_compose_base;     /* 0..6 */
static uint8_t  g_compose_form;     /* 0..3 */
static uint8_t  g_compose_tone;     /* 0..5 */
static bool     g_compose_upper;

/* vi_table[base][form][tone] -> lowercase Unicode codepoint (0 = invalid) */
static const uint16_t vi_table[6][4][6] = {
    /* a */ {
        /* plain */     {0x0061, 0x00E1, 0x00E0, 0x1EA3, 0x00E3, 0x1EA1},
        /* circumflex â*/{0x00E2, 0x1EA5, 0x1EA7, 0x1EA9, 0x1EAB, 0x1EAD},
        /* breve ă */   {0x0103, 0x1EAF, 0x1EB1, 0x1EB3, 0x1EB5, 0x1EB7},
        /* horn (inv) */{0,      0,      0,      0,      0,      0}
    },
    /* e */ {
        /* plain */     {0x0065, 0x00E9, 0x00E8, 0x1EBB, 0x1EBD, 0x1EB9},
        /* circumflex ê*/{0x00EA, 0x1EBF, 0x1EC1, 0x1EC3, 0x1EC5, 0x1EC7},
        {0,0,0,0,0,0}, {0,0,0,0,0,0}
    },
    /* o */ {
        /* plain */     {0x006F, 0x00F3, 0x00F2, 0x1ECF, 0x00F5, 0x1ECD},
        /* circumflex ô*/{0x00F4, 0x1ED1, 0x1ED3, 0x1ED5, 0x1ED7, 0x1ED9},
        {0,0,0,0,0,0},
        /* horn ơ */    {0x01A1, 0x1EDB, 0x1EDD, 0x1EDF, 0x1EE1, 0x1EE3}
    },
    /* u */ {
        /* plain */     {0x0075, 0x00FA, 0x00F9, 0x1EE7, 0x0169, 0x1EE5},
        {0,0,0,0,0,0}, {0,0,0,0,0,0},
        /* horn ư */    {0x01B0, 0x1EE9, 0x1EEB, 0x1EED, 0x1EEF, 0x1EF1}
    },
    /* i */ {
        /* plain */     {0x0069, 0x00ED, 0x00EC, 0x1EC9, 0x0129, 0x1ECB},
        {0,0,0,0,0,0}, {0,0,0,0,0,0}, {0,0,0,0,0,0}
    },
    /* y */ {
        /* plain */     {0x0079, 0x00FD, 0x1EF3, 0x1EF7, 0x1EF9, 0x1EF5},
        {0,0,0,0,0,0}, {0,0,0,0,0,0}, {0,0,0,0,0,0}
    }
};

static bool is_vowel_letter(uint8_t c, uint8_t* base_out, bool* upper_out)
{
    bool upper = false;
    uint8_t lc = c;
    if (c >= 'A' && c <= 'Z') { upper = true; lc = (uint8_t)(c + 32); }
    switch (lc) {
    case 'a': *base_out = 0; break;
    case 'e': *base_out = 1; break;
    case 'o': *base_out = 2; break;
    case 'u': *base_out = 3; break;
    case 'i': *base_out = 4; break;
    case 'y': *base_out = 5; break;
    default: return false;
    }
    *upper_out = upper;
    return true;
}

static bool is_d_letter(uint8_t c, bool* upper_out)
{
    if (c == 'd') { *upper_out = false; return true; }
    if (c == 'D') { *upper_out = true;  return true; }
    return false;
}

/* Emit a Unicode codepoint as UTF-8 bytes into the ring buffer. */
static void emit_utf8(uint32_t cp)
{
    if (cp < 0x80) {
        kbuf_put((int)cp);
    } else if (cp < 0x800) {
        kbuf_put((int)(0xC0 | (cp >> 6)));
        kbuf_put((int)(0x80 | (cp & 0x3F)));
    } else {
        kbuf_put((int)(0xE0 | (cp >> 12)));
        kbuf_put((int)(0x80 | ((cp >> 6) & 0x3F)));
        kbuf_put((int)(0x80 | (cp & 0x3F)));
    }
}

static uint32_t compose_codepoint(void)
{
    uint32_t cp;
    if (g_compose_base == 6) {
        /* đ */
        cp = g_compose_upper ? 0x0110 : 0x0111;
        return cp;
    }
    cp = vi_table[g_compose_base][g_compose_form][g_compose_tone];
    if (cp == 0) {
        return 0;
    }
    if (g_compose_upper) {
        /* Latin-1: upper = lower - 0x20; Latin Extended: even=upper. */
        if (cp <= 0xFF) {
            cp -= 0x20;
        } else {
            cp -= 1;
        }
    }
    return cp;
}

/* Flush any held compose character to the buffer, then deactivate. */
static void telex_flush(void)
{
    uint32_t cp;
    if (!g_compose_active) {
        return;
    }
    cp = compose_codepoint();
    if (cp == 0) {
        /* Fallback: emit the plain ASCII base letter. */
        static const char base_letter[] = "aeouiyd";
        char c = base_letter[g_compose_base];
        if (g_compose_upper && c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        kbuf_put((uint8_t)c);
    } else {
        emit_utf8(cp);
    }
    g_compose_active = false;
}

static void telex_cancel(void)
{
    g_compose_active = false;
}

/*
 * Feed one printable ASCII character to the telex engine.  The character
 * may be consumed (composed), flushed, or emitted directly.
 */
static void telex_feed(uint8_t c)
{
    uint8_t base;
    bool    upper;

    if (!g_telex_enabled) {
        kbuf_put(c);
        return;
    }

    if (!g_compose_active) {
        if (is_vowel_letter(c, &base, &upper)) {
            g_compose_active = true;
            g_compose_base   = base;
            g_compose_form   = 0;
            g_compose_tone   = 0;
            g_compose_upper  = upper;
            return;                     /* hold the vowel */
        }
        if (is_d_letter(c, &upper)) {
            g_compose_active = true;
            g_compose_base   = 6;       /* d -> possibly đ */
            g_compose_form   = 0;
            g_compose_tone   = 0;
            g_compose_upper  = upper;
            return;
        }
        kbuf_put(c);
        return;
    }

    /* --- compose active: try to combine with the held base ----------- */

    /* Tone keys (only lowercase, only on vowels). */
    if (g_compose_base <= 5) {
        uint8_t tone = 0;
        switch (c) {
        case 's': tone = 1; break;  /* acute  (sắc)  */
        case 'f': tone = 2; break;  /* grave  (huyền) */
        case 'x': tone = 3; break;  /* hook   (hỏi)   */
        case 'r': tone = 4; break;  /* tilde  (ngã)   */
        case 'j': tone = 5; break;  /* dot    (nặng)  */
        default:  tone = 0; break;
        }
        if (tone != 0) {
            g_compose_tone = tone;   /* overwrite tone */
            return;                  /* still held */
        }
    }

    /* Vowel-shape keys (only lowercase). */
    if (c >= 'a' && c <= 'z') {
        uint8_t new_form = 0;
        bool    valid = false;

        if (g_compose_base == 0 && c == 'a') { new_form = 1; valid = true; } /* aa -> â */
        if (g_compose_base == 0 && c == 'w') { new_form = 2; valid = true; } /* aw -> ă */
        if (g_compose_base == 1 && c == 'e') { new_form = 1; valid = true; } /* ee -> ê */
        if (g_compose_base == 2 && c == 'o') { new_form = 1; valid = true; } /* oo -> ô */
        if (g_compose_base == 2 && c == 'w') { new_form = 3; valid = true; } /* ow -> ơ */
        if (g_compose_base == 3 && c == 'w') { new_form = 3; valid = true; } /* uw -> ư */
        if (g_compose_base == 6 && c == 'd') {                               /* dd -> đ */
            /* đ is final: emit it immediately. */
            uint32_t cp = g_compose_upper ? 0x0110 : 0x0111;
            emit_utf8(cp);
            g_compose_active = false;
            return;
        }

        if (valid) {
            /* Applying a shape resets tone to none (standard telex). */
            g_compose_form = new_form;
            g_compose_tone = 0;
            return;                     /* still held */
        }
    }

    /* Not a valid continuation: flush the held character, then handle c. */
    telex_flush();
    if (is_vowel_letter(c, &base, &upper)) {
        g_compose_active = true;
        g_compose_base   = base;
        g_compose_form   = 0;
        g_compose_tone   = 0;
        g_compose_upper  = upper;
    } else if (is_d_letter(c, &upper)) {
        g_compose_active = true;
        g_compose_base   = 6;
        g_compose_form   = 0;
        g_compose_tone   = 0;
        g_compose_upper  = upper;
    } else {
        kbuf_put(c);
    }
}

/* --- public API -------------------------------------------------------- */

void keyboard_init(void)
{
    /* Enable the first PS/2 port (keyboard) and flush any stale byte. */
    outb(KBD_CMD_PORT, KBD_ENABLE_KBD);
    (void)inb(KBD_DATA_PORT);

    g_modifiers    = 0;
    g_extended     = false;
    g_kbuf_head    = 0;
    g_kbuf_tail    = 0;
    g_pending_key  = 0;
    g_compose_active = false;

    irq_install_handler(IRQ_KEYBOARD, keyboard_handler);
    pic_clear_mask(IRQ_KEYBOARD);
}

/* IRQ1 handler — called by irq.c (extern) and installed via the table. */
void keyboard_handler(registers_t* regs)
{
    uint8_t scancode;
    bool    released;
    int     special;
    uint8_t c;
    bool    shift, ctrl, alt, caps;

    (void)regs;

    scancode = inb(KBD_DATA_PORT);

    if (scancode == EXTENDED_PREFIX) {
        g_extended = true;
        return;                 /* wait for the following byte */
    }

    released = (scancode & BREAK_BIT) ? true : false;
    scancode &= (uint8_t)~BREAK_BIT;

    /* --- modifier keys (make = press, break = release) --------------- */
    if (!g_extended) {
        if (scancode == 0x2A || scancode == 0x36) {   /* LShift / RShift */
            if (released) { g_modifiers &= (uint8_t)~MOD_SHIFT; }
            else          { g_modifiers |= MOD_SHIFT; }
            return;
        }
        if (scancode == 0x1D) {                       /* Left Ctrl */
            if (released) { g_modifiers &= (uint8_t)~MOD_CTRL; }
            else          { g_modifiers |= MOD_CTRL; }
            return;
        }
        if (scancode == 0x38) {                       /* Left Alt */
            if (released) { g_modifiers &= (uint8_t)~MOD_ALT; }
            else          { g_modifiers |= MOD_ALT; }
            return;
        }
        if (scancode == 0x3A && !released) {          /* CapsLock toggle */
            g_modifiers ^= MOD_CAPSLOCK;
            return;
        }
        if (scancode == 0x45 && !released) {          /* NumLock toggle */
            g_modifiers ^= MOD_NUMLOCK;
            return;
        }
        if (scancode == 0x46 && !released) {          /* ScrollLock toggle */
            g_modifiers ^= MOD_SCROLLLOCK;
            return;
        }
    } else {
        /* Extended modifiers: Right Ctrl (0x1D), Right Alt (0x38). */
        if (scancode == 0x1D) {
            if (released) { g_modifiers &= (uint8_t)~MOD_CTRL; }
            else          { g_modifiers |= MOD_CTRL; }
            g_extended = false;
            return;
        }
        if (scancode == 0x38) {
            if (released) { g_modifiers &= (uint8_t)~MOD_ALT; }
            else          { g_modifiers |= MOD_ALT; }
            g_extended = false;
            return;
        }
    }

    if (released) {
        g_extended = false;
        return;                 /* ignore release of non-modifier keys */
    }

    /* --- extended navigation / editing keys --------------------------- */
    if (g_extended) {
        special = kbd_extended_special(scancode);
        g_extended = false;
        if (special != 0) {
            telex_flush();
            kbuf_put(special);
            return;
        }
        /* Extended numpad Enter (0x1C) and '/' (0x35) fall through. */
        if (scancode == 0x1C) { telex_flush(); kbuf_put(KEY_ENTER); return; }
        if (scancode == 0x35) { telex_flush(); telex_feed('/');      return; }
        return;
    }

    /* --- function keys ------------------------------------------------- */
    if (scancode >= 0x3B && scancode <= 0x44) {
        telex_flush();
        kbuf_put(KEY_F1 + (scancode - 0x3B));
        return;
    }
    if (scancode == 0x57) { telex_flush(); kbuf_put(KEY_F11); return; }
    if (scancode == 0x58) { telex_flush(); kbuf_put(KEY_F12); return; }

    /* F2 toggles telex mode (in addition to being reported). */
    if (scancode == 0x3C) {
        g_telex_enabled = !g_telex_enabled;
        if (!g_telex_enabled) {
            telex_flush();
        }
    }

    /* --- printable / control character -------------------------------- */
    shift = (g_modifiers & MOD_SHIFT) ? true : false;
    ctrl  = (g_modifiers & MOD_CTRL)  ? true : false;
    alt   = (g_modifiers & MOD_ALT)   ? true : false;
    caps  = (g_modifiers & MOD_CAPSLOCK) ? true : false;
    (void)alt;

    if (scancode >= sizeof(kbd_us_unshift)) {
        return;
    }
    c = shift ? kbd_us_shift[scancode] : kbd_us_unshift[scancode];
    if (c == 0) {
        return;                 /* unmapped key */
    }

    /* CapsLock + shift invert case for letters. */
    if (caps && c >= 'a' && c <= 'z') {
        if (!shift) c = (uint8_t)(c - 32);
    } else if (caps && c >= 'A' && c <= 'Z') {
        if (shift)  c = (uint8_t)(c + 32);
    }

    /* Ctrl + letter -> control character (0x01..0x1A). */
    if (ctrl && c >= 'A' && c <= 'Z') {
        telex_flush();
        kbuf_put((int)(c - 'A' + 1));
        return;
    }
    if (ctrl && c >= 'a' && c <= 'z') {
        telex_flush();
        kbuf_put((int)(c - 'a' + 1));
        return;
    }

    /* Control characters (Enter, Backspace, Tab, Esc) flush compose. */
    if (c == KEY_ENTER || c == KEY_TAB || c == KEY_ESCAPE) {
        telex_flush();
        kbuf_put(c);
        return;
    }
    if (c == KEY_BACKSPACE) {
        if (g_compose_active) {
            telex_cancel();      /* backspace cancels the held compose */
        } else {
            kbuf_put(c);
        }
        return;
    }

    /* Anything else goes through the telex composer (or direct emit). */
    telex_feed(c);
}

char keyboard_getchar(void)
{
    int code;
    for (;;) {
        code = kbuf_get();
        if (code < 0) {
            /* Block: enable interrupts and halt until the next IRQ. */
            __asm__ volatile ("sti; hlt; cli");
            continue;
        }
        if (code <= 255) {
            return (char)code;
        }
        /* Non-printable special key: stash it for keyboard_getkey(). */
        g_pending_key = code;
        return 0;
    }
}

int keyboard_haschar(void)
{
    uint8_t i;
    /* True iff at least one queued event is a byte (0..255). */
    for (i = g_kbuf_tail; i != g_kbuf_head; i = (uint8_t)(i + 1)) {
        if (g_kbuf[i] <= 255) {
            return 1;
        }
    }
    return 0;
}

int keyboard_getkey(void)
{
    int code;
    if (g_pending_key != 0) {
        code = g_pending_key;
        g_pending_key = 0;
        return code;
    }
    code = kbuf_get();
    if (code < 0) {
        return 0;               /* no key available */
    }
    return code;
}

uint8_t keyboard_modifiers(void)
{
    return g_modifiers;
}

void keyboard_set_telex(bool enable)
{
    if (!enable) {
        telex_flush();
    }
    g_telex_enabled = enable;
}

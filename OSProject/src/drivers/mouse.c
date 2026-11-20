/*
 * mouse.c — PS/2 (auxiliary) mouse driver.
 *
 * The mouse shares the keyboard controller (ports 0x60 data / 0x64
 * command).  Bytes destined for the mouse are prefixed with command 0xD4
 * on port 0x64.  Movement reports are 3-byte packets delivered on IRQ12.
 */
#include "mouse.h"
#include "io.h"
#include "irq.h"
#include "common.h"

#define PS2_DATA_PORT   0x60
#define PS2_CMD_PORT    0x64

/* Controller commands */
#define PS2_CMD_ENABLE_AUX    0xA8
#define PS2_CMD_DISABLE_AUX   0xA7
#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60
#define PS2_CMD_WRITE_AUX     0xD4   /* next byte goes to the mouse */

/* Mouse (device) commands */
#define MOUSE_CMD_SET_DEFAULTS 0xF6
#define MOUSE_CMD_ENABLE       0xF4
#define MOUSE_CMD_DISABLE      0xF5
#define MOUSE_ACK              0xFA

/* Status register bits (read from 0x64) */
#define PS2_STATUS_OUTPUT_FULL 0x01   /* output buffer full (can read)  */
#define PS2_STATUS_INPUT_FULL  0x02   /* input buffer full (wait)       */

/* Controller configuration byte bits */
#define CFG_IRQ12_ENABLE       0x02   /* bit 1: aux port IRQ (IRQ12)    */
#define CFG_AUX_CLOCK_DISABLE  0x20   /* bit 5: aux clock disable (1=off) */

/* Packet byte 0 flags */
#define PKT_LEFT_BTN    0x01
#define PKT_RIGHT_BTN   0x02
#define PKT_MIDDLE_BTN  0x04
#define PKT_ALWAYS_ONE  0x08
#define PKT_X_SIGN      0x10
#define PKT_Y_SIGN      0x20
#define PKT_X_OVERFLOW  0x40
#define PKT_Y_OVERFLOW  0x80

/* Framebuffer dimensions (defined by paging.c / boot code). */
extern uint64_t g_framebuffer_width;
extern uint64_t g_framebuffer_height;

/* Global mouse state as requested by the specification. */
mouse_state_t mouse_state;

static volatile bool    g_mouse_event = false;
static uint8_t          g_packet[3];
static uint8_t          g_packet_idx = 0;

/* IRQ12 handler, also called directly by irq.c (extern). */
void mouse_handler(registers_t* regs);

/* --- low-level controller helpers ------------------------------------- */

static void mouse_wait_input_empty(void)
{
    uint32_t timeout = 100000;
    while ((inb(PS2_CMD_PORT) & PS2_STATUS_INPUT_FULL) && timeout--) {
        /* spin */
    }
}

static void mouse_wait_output_full(void)
{
    uint32_t timeout = 100000;
    while (!(inb(PS2_CMD_PORT) & PS2_STATUS_OUTPUT_FULL) && timeout--) {
        /* spin */
    }
}

/* Send a command byte directly to the mouse (with 0xD4 prefix). */
static void mouse_write(uint8_t val)
{
    mouse_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
    mouse_wait_input_empty();
    outb(PS2_DATA_PORT, val);
}

static uint8_t mouse_read(void)
{
    mouse_wait_output_full();
    return inb(PS2_DATA_PORT);
}

/* --- initialization ---------------------------------------------------- */

void mouse_init(void)
{
    uint8_t config;

    /* 1. Enable the auxiliary (mouse) device. */
    mouse_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_AUX);

    /* 2. Read the controller configuration byte. */
    mouse_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    config = mouse_read();

    /*
     * 3. Read-modify-write the controller configuration byte.
     *    Per the specification we set bit 5 (0x20).  We also set bit 1
     *    (CFG_IRQ12_ENABLE) so the aux device raises IRQ12.  Bit 4
     *    (keyboard clock disable) is left untouched.
     */
    config |= (uint8_t)(CFG_IRQ12_ENABLE | 0x20);

    /* Write the configuration byte back. */
    mouse_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    mouse_wait_input_empty();
    outb(PS2_DATA_PORT, config);

    /* 4. Load defaults, then enable streaming (data reporting). */
    mouse_write(MOUSE_CMD_SET_DEFAULTS);
    (void)mouse_read();                 /* ACK */
    mouse_write(MOUSE_CMD_ENABLE);
    (void)mouse_read();                 /* ACK */

    /* Initial cursor position (clamped on first movement). */
    mouse_state.x = 0;
    mouse_state.y = 0;
    mouse_state.dx = 0;
    mouse_state.dy = 0;
    mouse_state.buttons = 0;
    g_packet_idx = 0;
    g_mouse_event = false;

    irq_install_handler(IRQ_MOUSE, mouse_handler);
}

/* --- IRQ12 handler (called by irq.c; also installed via table) --------- */

void mouse_handler(registers_t* regs)
{
    uint8_t b;
    int16_t dx, dy;
    uint16_t max_x, max_y;

    (void)regs;

    b = inb(PS2_DATA_PORT);

    /* First byte of a packet must have bit 3 set (always-one).  Use it
     * to re-synchronise if we lost a byte. */
    if (g_packet_idx == 0 && !(b & PKT_ALWAYS_ONE)) {
        return;
    }

    g_packet[g_packet_idx++] = b;
    if (g_packet_idx < 3) {
        return;
    }
    g_packet_idx = 0;

    /* Ignore packets with overflow (unreliable deltas). */
    if (g_packet[0] & (PKT_X_OVERFLOW | PKT_Y_OVERFLOW)) {
        return;
    }

    /* Sign-extend the 9-bit movement values. */
    dx = (int16_t)g_packet[1];
    if (g_packet[0] & PKT_X_SIGN) {
        dx |= (int16_t)0xFF00;
    }
    dy = (int16_t)g_packet[2];
    if (g_packet[0] & PKT_Y_SIGN) {
        dy |= (int16_t)0xFF00;
    }

    /* PS/2 Y is positive upwards; screen Y grows downwards. */
    dy = (int16_t)(-dy);

    mouse_state.dx = dx;
    mouse_state.dy = dy;
    mouse_state.buttons = (uint8_t)(g_packet[0] &
                                    (PKT_LEFT_BTN | PKT_RIGHT_BTN | PKT_MIDDLE_BTN));

    /* Clamp cursor to screen bounds. */
    max_x = (uint16_t)((g_framebuffer_width  > 0) ? g_framebuffer_width  - 1 : 799);
    max_y = (uint16_t)((g_framebuffer_height > 0) ? g_framebuffer_height - 1 : 599);

    mouse_state.x = (int16_t)(mouse_state.x + dx);
    mouse_state.y = (int16_t)(mouse_state.y + dy);
    if (mouse_state.x < 0)       mouse_state.x = 0;
    if (mouse_state.x > max_x)   mouse_state.x = (int16_t)max_x;
    if (mouse_state.y < 0)       mouse_state.y = 0;
    if (mouse_state.y > max_y)   mouse_state.y = (int16_t)max_y;

    g_mouse_event = true;
}

/* --- public API -------------------------------------------------------- */

void mouse_get(mouse_state_t* out)
{
    if (out == NULL) {
        return;
    }
    *out = mouse_state;
    g_mouse_event = false;
}

bool mouse_has_event(void)
{
    return g_mouse_event;
}

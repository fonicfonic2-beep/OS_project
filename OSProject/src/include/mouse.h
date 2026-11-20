#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04

typedef struct mouse_state {
    int16_t  x;          /* accumulated absolute X (clamped to screen) */
    int16_t  y;          /* accumulated absolute Y                     */
    int16_t  dx;         /* last movement delta X                      */
    int16_t  dy;         /* last movement delta Y                      */
    uint8_t  buttons;    /* bitmask of MOUSE_BUTTON_*                   */
} mouse_state_t;

void mouse_init(void);
void mouse_get(mouse_state_t* out);
bool mouse_has_event(void);

#endif /* MOUSE_H */

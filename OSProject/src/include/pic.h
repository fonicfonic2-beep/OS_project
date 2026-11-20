#ifndef PIC_H
#define PIC_H

#include "types.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define PIC_EOI    0x20

/* ICW1 bits */
#define PIC_ICW1_INIT     0x10
#define PIC_ICW1_ICW4     0x01
/* ICW4 bits */
#define PIC_ICW4_8086     0x01
#define PIC_ICW4_AUTO_EOI 0x02

void pic_init(uint8_t offset1, uint8_t offset2);
void pic_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
uint16_t pic_get_irr(void);
uint16_t pic_get_isr(void);
void pic_disable(void);

#endif /* PIC_H */

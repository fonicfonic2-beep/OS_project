#ifndef IRQ_H
#define IRQ_H

#include "types.h"
#include "isr.h"

/* Standard PIC remap: master IRQs 0..7 -> vectors 32..39, slave -> 40..47 */
#define IRQ0_VECTOR  32
#define IRQ15_VECTOR 47

/* IRQ line assignments (8259 PIC default) */
#define IRQ_PIT      0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_LPT2     5
#define IRQ_FLOPPY   6
#define IRQ_LPT1     7
#define IRQ_RTC      8
#define IRQ_MOUSE    12
#define IRQ_FPU      13
#define IRQ_ATA1     14
#define IRQ_ATA2     15

typedef void (*irq_handler_t)(registers_t* regs);

void irq_init(void);
void irq_install_handler(uint8_t irq, irq_handler_t h);
void irq_uninstall_handler(uint8_t irq);
void irq_eoi(uint8_t irq);
void irq_handler(registers_t* regs);   /* common C handler */

/* Assembly stub entries for IRQ lines 0..15 */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

#endif /* IRQ_H */

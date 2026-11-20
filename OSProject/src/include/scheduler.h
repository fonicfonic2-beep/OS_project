#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"
#include "isr.h"
#include "process.h"

/*
 * Simple round-robin preemptive scheduler.
 * scheduler_tick is invoked from the PIT IRQ handler (IRQ0).
 */

void          scheduler_init(void);
void          scheduler_tick(registers_t* regs);
void          scheduler_add(process_t* p);
void          scheduler_remove(process_t* p);
process_t*    scheduler_current(void);
void          yield(void);
void          scheduler_exit_current(int code);

#endif /* SCHEDULER_H */

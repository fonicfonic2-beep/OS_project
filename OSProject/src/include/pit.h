#ifndef PIT_H
#define PIT_H

#include "types.h"

#define PIT_CHANNEL0   0x40
#define PIT_CHANNEL1   0x41
#define PIT_CHANNEL2   0x42
#define PIT_CMD        0x43

#define PIT_BASE_FREQ  1193182

void     pit_init(uint32_t hz);
uint64_t pit_ticks(void);
uint64_t pit_millis(void);
void     pit_sleep(uint64_t ms);

#endif /* PIT_H */

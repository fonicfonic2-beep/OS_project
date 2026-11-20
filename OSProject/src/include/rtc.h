#ifndef RTC_H
#define RTC_H

#include "types.h"

#define RTC_INDEX_REG  0x70
#define RTC_DATA_REG   0x71

typedef struct datetime {
    uint8_t  sec;     /* 0..59   */
    uint8_t  min;     /* 0..59   */
    uint8_t  hour;    /* 0..23   */
    uint8_t  day;     /* 1..31   */
    uint8_t  month;   /* 1..12   */
    uint16_t year;    /* e.g. 2026 */
    uint8_t  weekday; /* 0..6 (Sunday=0) */
} datetime_t;

void rtc_init(void);
void rtc_read(datetime_t* out);
void rtc_write(datetime_t* in);

#endif /* RTC_H */

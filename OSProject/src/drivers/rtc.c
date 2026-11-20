/*
 * rtc.c — Real-Time Clock / CMOS (MC146818 compatible).
 *
 * Ports: 0x70 = index register (bit 7 = NMI disable), 0x71 = data.
 * We do not use the RTC periodic interrupt; polling is enough.
 */
#include "rtc.h"
#include "io.h"

/* RTC / CMOS register indices */
#define RTC_REG_SEC      0x00
#define RTC_REG_MIN      0x02
#define RTC_REG_HOUR     0x04
#define RTC_REG_WEEKDAY  0x06
#define RTC_REG_DAY      0x07
#define RTC_REG_MONTH    0x08
#define RTC_REG_YEAR     0x09
#define RTC_REG_CENTURY  0x32   /* often present on ACPI systems */
#define RTC_REG_A        0x0A
#define RTC_REG_B        0x0B

/* Status register A bits */
#define RTC_A_UIP        0x80   /* update in progress */
/* Status register B bits */
#define RTC_B_24H        0x02   /* 1 = 24-hour mode, 0 = 12-hour */
#define RTC_B_BINARY     0x04   /* 1 = binary, 0 = BCD */
#define RTC_B_PIE        0x40   /* periodic interrupt enable */
#define RTC_B_AIE        0x20   /* alarm interrupt enable */
#define RTC_B_UIE        0x10   /* update-ended interrupt enable */

#define RTC_NMI_DISABLE  0x80   /* OR into index to mask NMI */

static uint8_t rtc_in(uint8_t reg)
{
    outb(RTC_INDEX_REG, reg);
    return inb(RTC_DATA_REG);
}

static void rtc_out(uint8_t reg, uint8_t val)
{
    outb(RTC_INDEX_REG, reg);
    outb(RTC_DATA_REG, val);
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)((bcd & 0x0F) + ((bcd >> 4) * 10));
}

static uint8_t bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

/* Wait until an update is not in progress (caller holds no lock). */
static void rtc_wait_not_updating(void)
{
    while (rtc_in(RTC_REG_A) & RTC_A_UIP) {
        /* spin; an update takes at most ~244 us */
    }
}

void rtc_init(void)
{
    uint8_t status_b;

    /* Disable the three RTC interrupt sources; keep time format intact. */
    status_b = rtc_in(RTC_REG_B);
    status_b &= (uint8_t)~(RTC_B_PIE | RTC_B_AIE | RTC_B_UIE);
    rtc_out(RTC_REG_B, status_b);

    /* Re-enable NMI by writing an index with bit 7 clear. */
    outb(RTC_INDEX_REG, 0x00);
}

void rtc_read(datetime_t* out)
{
    uint8_t sec, min, hour, day, month, year, century, weekday;
    uint8_t status_b;
    uint8_t sec2, min2, hour2, day2, month2, year2, weekday2;

    if (out == NULL) {
        return;
    }

    /*
     * Read the whole clock twice.  If the two reads differ, an update
     * occurred in between and we re-read until they match.  This avoids
     * torn values around the 1-second rollover.
     */
    rtc_wait_not_updating();
    sec     = rtc_in(RTC_REG_SEC);
    min     = rtc_in(RTC_REG_MIN);
    hour    = rtc_in(RTC_REG_HOUR);
    weekday = rtc_in(RTC_REG_WEEKDAY);
    day     = rtc_in(RTC_REG_DAY);
    month   = rtc_in(RTC_REG_MONTH);
    year    = rtc_in(RTC_REG_YEAR);
    century = rtc_in(RTC_REG_CENTURY);

    do {
        sec2     = sec;
        min2     = min;
        hour2    = hour;
        weekday2 = weekday;
        day2     = day;
        month2   = month;
        year2    = year;

        rtc_wait_not_updating();
        sec     = rtc_in(RTC_REG_SEC);
        min     = rtc_in(RTC_REG_MIN);
        hour    = rtc_in(RTC_REG_HOUR);
        weekday = rtc_in(RTC_REG_WEEKDAY);
        day     = rtc_in(RTC_REG_DAY);
        month   = rtc_in(RTC_REG_MONTH);
        year    = rtc_in(RTC_REG_YEAR);
    } while (sec != sec2 || min != min2 || hour != hour2 ||
             day != day2 || month != month2 || year != year2 ||
             weekday != weekday2);

    status_b = rtc_in(RTC_REG_B);

    /* Convert BCD to binary unless the RTC is already in binary mode. */
    if (!(status_b & RTC_B_BINARY)) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = (uint8_t)(hour & 0x7F);  /* strip PM flag before BCD conv */
        hour  = bcd_to_bin(hour);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
        if (century != 0 && century != 0xFF) {
            century = bcd_to_bin(century);
        }
    } else {
        hour = (uint8_t)(hour & 0x7F);
    }

    /* 12-hour mode: bit 7 of hour was set for PM.  Convert to 24h. */
    if (!(status_b & RTC_B_24H)) {
        if (hour2 & 0x80) {
            /* PM: 12 stays 12, 1..11 become 13..23 */
            if (hour != 12) {
                hour = (uint8_t)(hour + 12);
            }
        } else {
            /* AM: 12 becomes 0 (midnight) */
            if (hour == 12) {
                hour = 0;
            }
        }
    }

    /* Assemble full year. */
    if (century != 0 && century != 0xFF) {
        out->year = (uint16_t)(century * 100u + year);
    } else {
        /* No century register: assume 21st century for years < 100. */
        out->year = (uint16_t)(2000u + year);
    }

    out->sec     = sec;
    out->min     = min;
    out->hour    = hour;
    out->day     = day;
    out->month   = month;
    out->weekday = (uint8_t)(weekday & 0x07);
}

void rtc_write(datetime_t* in)
{
    uint8_t status_b;
    bool    binary_mode;

    if (in == NULL) {
        return;
    }

    status_b    = rtc_in(RTC_REG_B);
    binary_mode = (status_b & RTC_B_BINARY) ? true : false;

    rtc_wait_not_updating();

    /* Disable interrupts and set binary mode so we can write raw values. */
    rtc_out(RTC_REG_B, (uint8_t)((status_b | RTC_B_BINARY) &
                                 ~(RTC_B_PIE | RTC_B_AIE | RTC_B_UIE)));

    rtc_out(RTC_REG_SEC,     in->sec);
    rtc_out(RTC_REG_MIN,     in->min);
    rtc_out(RTC_REG_HOUR,    in->hour);        /* always 24-hour */
    rtc_out(RTC_REG_WEEKDAY, in->weekday);
    rtc_out(RTC_REG_DAY,     in->day);
    rtc_out(RTC_REG_MONTH,   in->month);
    rtc_out(RTC_REG_YEAR,    (uint8_t)(in->year % 100u));

    if (in->year >= 100) {
        rtc_out(RTC_REG_CENTURY, (uint8_t)(in->year / 100u));
    }

    /* Restore the original mode (BCD/binary), keeping interrupts off. */
    if (!binary_mode) {
        status_b &= (uint8_t)~RTC_B_BINARY;
    }
    status_b &= (uint8_t)~(RTC_B_PIE | RTC_B_AIE | RTC_B_UIE);
    rtc_out(RTC_REG_B, status_b);

    /* Re-enable NMI. */
    outb(RTC_INDEX_REG, 0x00);
}

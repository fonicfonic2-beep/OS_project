/*
 * apps/clock.c — real-time clock display.
 *
 * Reads the RTC ten times, once per second, printing HH:MM:SS DD/MM/YYYY.
 * Any keypress aborts the loop early.
 */
#include "apps.h"
#include "types.h"
#include "console.h"
#include "i18n.h"
#include "rtc.h"
#include "pit.h"
#include "keyboard.h"
#include "stdio.h"

#define CLOCK_UPDATES 10

void app_clock(void)
{
    datetime_t dt;
    int i;

    console_printf("%s\n", tr("clock_title"));

    for (i = 0; i < CLOCK_UPDATES; i++) {
        rtc_read(&dt);
        console_printf("\r  %02u:%02u:%02u  %02u/%02u/%04u   ",
                       (unsigned)dt.hour, (unsigned)dt.min, (unsigned)dt.sec,
                       (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year);

        if (keyboard_haschar()) {
            (void)keyboard_getchar();
            break;
        }
        pit_sleep(1000);
        if (keyboard_haschar()) {
            (void)keyboard_getchar();
            break;
        }
    }
    console_printf("\n");
}

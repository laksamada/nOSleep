#include <stdbool.h>
#include "header/driver/cmos.h"
#include "header/cpu/portio.h"

#define CMOS_REG_SECOND   0x00
#define CMOS_REG_MINUTE   0x02
#define CMOS_REG_HOUR     0x04
#define CMOS_REG_DAY      0x07
#define CMOS_REG_MONTH    0x08
#define CMOS_REG_YEAR     0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

#define CMOS_UPDATE_IN_PROGRESS 0x80
#define CMOS_BINARY_MODE        0x04
#define CMOS_24_HOUR_MODE       0x02
#define CMOS_HOUR_PM            0x80

static uint8_t cmos_read(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (value & 0x0F) + (value >> 4) * 10;
}

static void cmos_read_raw(
    uint8_t *second, uint8_t *minute, uint8_t *hour,
    uint8_t *day, uint8_t *month, uint8_t *year
) {
    while (cmos_read(CMOS_REG_STATUS_A) & CMOS_UPDATE_IN_PROGRESS)
        ;

    *second = cmos_read(CMOS_REG_SECOND);
    *minute = cmos_read(CMOS_REG_MINUTE);
    *hour   = cmos_read(CMOS_REG_HOUR);
    *day    = cmos_read(CMOS_REG_DAY);
    *month  = cmos_read(CMOS_REG_MONTH);
    *year   = cmos_read(CMOS_REG_YEAR);
}

void cmos_get_time(struct CMOSTime *time) {
    uint8_t second, minute, hour, day, month, year;
    uint8_t prev_second, prev_minute, prev_hour, prev_day, prev_month, prev_year;

    cmos_read_raw(&second, &minute, &hour, &day, &month, &year);
    do {
        prev_second = second;
        prev_minute = minute;
        prev_hour   = hour;
        prev_day    = day;
        prev_month  = month;
        prev_year   = year;
        cmos_read_raw(&second, &minute, &hour, &day, &month, &year);
    } while (second != prev_second || minute != prev_minute || hour != prev_hour ||
             day != prev_day || month != prev_month || year != prev_year);

    uint8_t status = cmos_read(CMOS_REG_STATUS_B);
    bool pm = (hour & CMOS_HOUR_PM) != 0;
    hour &= ~CMOS_HOUR_PM;
    if (!(status & CMOS_BINARY_MODE)) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour   = bcd_to_binary(hour);
        day    = bcd_to_binary(day);
        month  = bcd_to_binary(month);
        year   = bcd_to_binary(year);
    }
    if (!(status & CMOS_24_HOUR_MODE)) {
        if (pm && hour != 12)
            hour += 12;
        else if (!pm && hour == 12)
            hour = 0;
    }

    time->second = second;
    time->minute = minute;
    time->hour   = hour;
    time->day    = day;
    time->month  = month;
    time->year   = 2000 + year;
}

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

void cmos_get_time(struct CMOSTime *time) {
    while (cmos_read(CMOS_REG_STATUS_A) & CMOS_UPDATE_IN_PROGRESS)
        ;

    uint8_t second = cmos_read(CMOS_REG_SECOND);
    uint8_t minute = cmos_read(CMOS_REG_MINUTE);
    uint8_t hour   = cmos_read(CMOS_REG_HOUR);
    uint8_t day    = cmos_read(CMOS_REG_DAY);
    uint8_t month  = cmos_read(CMOS_REG_MONTH);
    uint8_t year   = cmos_read(CMOS_REG_YEAR);
    uint8_t status = cmos_read(CMOS_REG_STATUS_B);

    bool pm = hour & CMOS_HOUR_PM;
    hour &= ~CMOS_HOUR_PM;
    if (!(status & CMOS_BINARY_MODE)) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour   = bcd_to_binary(hour);
        day    = bcd_to_binary(day);
        month  = bcd_to_binary(month);
        year   = bcd_to_binary(year);
    }
    if (!(status & CMOS_24_HOUR_MODE) && pm)
        hour = (hour + 12) % 24;

    time->second = second;
    time->minute = minute;
    time->hour   = hour;
    time->day    = day;
    time->month  = month;
    time->year   = 2000 + year;
}

#include "header/driver/cmos.h"
#include "header/cpu/portio.h"

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT  0x71

#define CMOS_SECONDS  0x00
#define CMOS_MINUTES  0x02
#define CMOS_HOURS    0x04
#define CMOS_STATUS_A 0x0A
#define CMOS_STATUS_B 0x0B

#define CMOS_UPDATE_IN_PROGRESS 0x80
#define CMOS_BINARY_MODE        0x04
#define CMOS_24_HOUR_MODE       0x02
#define CMOS_PM_BIT             0x80

static uint8_t cmos_read_register(uint8_t reg) {
    out(CMOS_INDEX_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (value >> 4) * 10 + (value & 0x0F);
}

void cmos_read_time(struct Time *time) {
    while (cmos_read_register(CMOS_STATUS_A) & CMOS_UPDATE_IN_PROGRESS) {
        // Wait for the RTC update to finish.
    }

    uint8_t seconds = cmos_read_register(CMOS_SECONDS);
    uint8_t minutes = cmos_read_register(CMOS_MINUTES);
    uint8_t hours = cmos_read_register(CMOS_HOURS);
    uint8_t status_b = cmos_read_register(CMOS_STATUS_B);

    uint8_t pm = hours & CMOS_PM_BIT;
    hours &= 0x7F;

    if (!(status_b & CMOS_BINARY_MODE)) {
        seconds = bcd_to_binary(seconds);
        minutes = bcd_to_binary(minutes);
        hours = bcd_to_binary(hours);
    }

    if (!(status_b & CMOS_24_HOUR_MODE)) {
        hours = hours % 12 + (pm ? 12 : 0);
    }

    time->hour = hours;
    time->minute = minutes;
    time->second = seconds;
}

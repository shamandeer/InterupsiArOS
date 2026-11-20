#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>

struct Time {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

/**
 * Read the CMOS RTC time in binary, 24-hour format.
 *
 * @param time Destination for the hour, minute, and second.
 */
void cmos_read_time(struct Time *time);

#endif

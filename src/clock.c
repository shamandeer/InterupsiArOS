#include <stdbool.h>
#include "header/driver/cmos.h"

// Syscall numbers, synced with src/cpu/interrupt.c
#define SYS_GET_TIME    13
#define SYS_PUT_CHAR_AT 14

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile(
        "movl %0, %%eax\n\t"
        "int $0x30"
        :
        : "rm"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "eax", "memory"
    );
}

int main(void) {
    struct Time time = { .second = 0xFF };
    uint8_t last_second = 0xFF;

    while (true) {
        syscall(SYS_GET_TIME, (uint32_t) &time, 0, 0);
        if (time.second == last_second)
            continue;

        last_second = time.second;
        const char display[] = {
            '0' + time.hour / 10, '0' + time.hour % 10, ':',
            '0' + time.minute / 10, '0' + time.minute % 10, ':',
            '0' + time.second / 10, '0' + time.second % 10,
        };

        for (uint32_t i = 0; i < sizeof(display); i++)
            syscall(SYS_PUT_CHAR_AT, (uint32_t) display[i], 24, 72 + i);
    }
}

#include <stdbool.h>
#include "header/driver/cmos.h"

// Syscall numbers, synced with src/cpu/interrupt.c
#define SYS_GET_TIME    13
#define SYS_PUT_CHAR_AT 14

// Pojok kanan atas layar (clock ada di baris 24, kolom 72-79)
#define SPINNER_ROW 0
#define SPINNER_COL 79

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
    const char frames[] = "|/-\\";
    struct Time time = { .second = 0xFF };
    uint8_t last_second = 0xFF;
    uint32_t index = 0;

    while (true) {
        syscall(SYS_GET_TIME, (uint32_t) &time, 0, 0);
        if (time.second == last_second)
            continue;

        last_second = time.second;
        syscall(SYS_PUT_CHAR_AT, (uint32_t) frames[index], SPINNER_ROW, SPINNER_COL);
        index = (index + 1) % 4;
    }
}

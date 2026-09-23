#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    // TODO : Implement
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    // TODO : Implement (done);
    uint16_t attr = (bg << 4) | (fg & 0xF); // i.e. bg = 0x00, fg = 0x0F, attr = 0x0F
    volatile uint16_t* where = (volatile uint16_t*)(FRAMEBUFFER_MEMORY_OFFSET) + (row * 80 + col);
    *where = (attr << 8) | ((unsigned char) c);
}

void framebuffer_clear(void) {
    // TODO : Implement
}

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/cpu/portio.h"

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (row >= 25 || col >= 80)
        return;

    uint32_t offset = ((uint32_t)(row * 80) + col) * 2;
    FRAMEBUFFER_MEMORY_OFFSET[offset]     = (uint8_t)c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t)(((bg & 0x0F) << 4) | (fg & 0x0F));
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t pos = (uint16_t)((r * 80) + c);

    // Send high byte of cursor position to VGA CRT controller
    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t)((pos >> 8) & 0xFF));

    // Send low byte of cursor position to VGA CRT controller
    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t)(pos & 0xFF));
}

void framebuffer_clear(void) {
    for (uint8_t row = 0; row < 25; row++) {
        for (uint8_t col = 0; col < 80; col++) {
            framebuffer_write(row, col, ' ', 0x07, 0x00);
        }
    }
    framebuffer_set_cursor(0, 0);
}

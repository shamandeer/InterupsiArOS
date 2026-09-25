#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();

    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    keyboard_state_activate();

    int row = 0, col = 0;
    while (true) {
        char c = 0;
        get_keyboard_buffer(&c);
        if (c != 0) {
            if (c == '\n') {
                row++;
                col = 0;
            } else {
                framebuffer_write(row, col, c, 0xF, 0);
                if (col >= 79) {
                    row++;
                    col = 0;
                } else {
                    col++;
                }
            }
            if (row >= 25) {
                row = 0;
            }
            framebuffer_set_cursor(row, col);
        }
    }
}
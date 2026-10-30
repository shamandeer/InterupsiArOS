#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/memory/swap.h"
#include "header/text/framebuffer.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    __asm__ volatile ("mov %%ebp, %0" : "=r"(stack_ptr) :);
    // Offset +8 skips saved ebp and return address to capture caller's kernel esp
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
}

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8)
        out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET);
    io_wait();
    out(PIC1_DATA, 0b0100);
    io_wait();
    out(PIC2_DATA, 0b0010);
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

#define FRAMEBUFFER_ROWS 25
#define FRAMEBUFFER_COLS 80

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

static void kernel_scroll_up(void) {
    memmove(
        FRAMEBUFFER_MEMORY_OFFSET,
        FRAMEBUFFER_MEMORY_OFFSET + FRAMEBUFFER_COLS * 2,
        (FRAMEBUFFER_ROWS - 1) * FRAMEBUFFER_COLS * 2
    );
    for (uint8_t col = 0; col < FRAMEBUFFER_COLS; col++)
        framebuffer_write(FRAMEBUFFER_ROWS - 1, col, ' ', 0x07, 0x00);
}

static void kernel_putchar_at_cursor(char c, uint8_t color) {
    if (c == '\0')
        return;
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            framebuffer_write(cursor_row, cursor_col, ' ', 0x07, 0x00);
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = FRAMEBUFFER_COLS - 1;
            framebuffer_write(cursor_row, cursor_col, ' ', 0x07, 0x00);
        }
    } else {
        framebuffer_write(cursor_row, cursor_col, c, color & 0x0F, (color >> 4) & 0x0F);
        cursor_col++;
        if (cursor_col >= FRAMEBUFFER_COLS) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= FRAMEBUFFER_ROWS) {
        kernel_scroll_up();
        cursor_row = FRAMEBUFFER_ROWS - 1;
    }
}

void kernel_puts(const char *buf, uint32_t count, uint8_t color) {
    for (uint32_t i = 0; i < count; i++)
        kernel_putchar_at_cursor(buf[i], color);
    framebuffer_set_cursor(cursor_row, cursor_col);
}

void syscall(struct InterruptFrame frame) {
    // Cast ebx/ecx from pushad frame into user-space request and return-code pointers
    switch (frame.cpu.general.eax) {
        case 0:
            *((int8_t *) frame.cpu.general.ecx) = read(*(struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;
        case 1:
            *((int8_t *) frame.cpu.general.ecx) = read_directory((struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;
        case 2:
            *((int8_t *) frame.cpu.general.ecx) = write((struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;
        case 3:
            *((int8_t *) frame.cpu.general.ecx) = delete(*(struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;
        case 4:
            get_keyboard_buffer((char *) frame.cpu.general.ebx);
            break;
        case 5:
            kernel_putchar_at_cursor((char) frame.cpu.general.ebx, (uint8_t) frame.cpu.general.ecx);
            framebuffer_set_cursor(cursor_row, cursor_col);
            break;
        case 6:
            kernel_puts((const char *) frame.cpu.general.ebx, frame.cpu.general.ecx, (uint8_t) frame.cpu.general.edx);
            break;
        case 7:
            keyboard_state_activate();
            break;
        case 8:
            framebuffer_clear();
            cursor_row = 0;
            cursor_col = 0;
            framebuffer_set_cursor(0, 0);
            break;
        case 9:
            cursor_row = (uint8_t) frame.cpu.general.ebx;
            cursor_col = (uint8_t) frame.cpu.general.ecx;
            if (cursor_row >= FRAMEBUFFER_ROWS)
                cursor_row = FRAMEBUFFER_ROWS - 1;
            if (cursor_col >= FRAMEBUFFER_COLS)
                cursor_col = FRAMEBUFFER_COLS - 1;
            framebuffer_set_cursor(cursor_row, cursor_col);
            break;
        case 10: // Reserved for Chapter 4 process exit syscall
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case 0x30:
            syscall(frame);
            break;
        case 0xE: {
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            page_fault_handler(cr2, frame.int_stack.error_code);
            break;
        }
    }
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

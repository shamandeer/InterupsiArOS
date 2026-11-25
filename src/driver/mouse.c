#include "header/driver/mouse.h"
#include "header/driver/ps2.h"
#include "header/text/framebuffer.h"

struct MouseState {
    uint8_t packet[3];
    uint8_t index;
    bool available;

    uint8_t row;
    uint8_t col;
    uint8_t buttons;
    int32_t dx_accumulator;
    int32_t dy_accumulator;

    bool cursor_visible;
    uint16_t saved_cell;
};

static struct MouseState mouse_state;

static volatile uint16_t *mouse_screen_cell(uint8_t row, uint8_t col) {
    return (volatile uint16_t *) FRAMEBUFFER_MEMORY_OFFSET + row * MOUSE_SCREEN_WIDTH + col;
}

static void mouse_framebuffer_write_hook(uint8_t row, uint8_t col) {
    if (!mouse_state.cursor_visible || row != mouse_state.row || col != mouse_state.col)
        return;

    // Teks di bawah kursor berubah: simpan isi baru sebelum di-invert lagi.
    volatile uint16_t *cell = mouse_screen_cell(row, col);
    mouse_state.saved_cell = *cell;

    uint8_t character = (uint8_t) (*cell & 0xFF);
    uint8_t attribute = (uint8_t) (*cell >> 8);
    attribute = (uint8_t) ((attribute << 4) | (attribute >> 4));
    *cell = (uint16_t) ((attribute << 8) | character);
}

static void mouse_cursor_restore(void) {
    if (!mouse_state.cursor_visible)
        return;

    *mouse_screen_cell(mouse_state.row, mouse_state.col) = mouse_state.saved_cell;
    mouse_state.cursor_visible = false;
}

static void mouse_cursor_draw(void) {
    volatile uint16_t *cell = mouse_screen_cell(mouse_state.row, mouse_state.col);

    mouse_state.saved_cell = *cell;
    mouse_state.cursor_visible = true;

    uint8_t character = (uint8_t) (*cell & 0xFF);
    uint8_t attribute = (uint8_t) (*cell >> 8);

    // Tukar foreground/background supaya sel terlihat sebagai kursor.
    attribute = (uint8_t) ((attribute << 4) | (attribute >> 4));
    *cell = (uint16_t) ((attribute << 8) | character);
}

void mouse_reset(void) {
    mouse_state.index = 0;
}

bool mouse_init(void) {
    mouse_state.available = false;

    // Set default, lalu mulai mengirim paket gerakan/tombol.
    if (!ps2_send_mouse_command(0xF6))
        return false;
    if (!ps2_send_mouse_command(0xF4))
        return false;

    ps2_flush_mouse();
    mouse_reset();

    mouse_state.available = true;
    mouse_state.buttons = 0;
    mouse_state.dx_accumulator = 0;
    mouse_state.dy_accumulator = 0;
    mouse_state.cursor_visible = false;
    return true;
}

bool mouse_get_event(struct MouseEvent *event) {
    if (event == (void *) 0)
        return false;

    uint8_t byte;
    while (ps2_mouse_pop(&byte)) {
        // Byte pertama paket standar selalu memiliki bit 3 set.
        if (mouse_state.index == 0 && !(byte & 0x08))
            continue;

        mouse_state.packet[mouse_state.index++] = byte;
        if (mouse_state.index < 3)
            continue;

        mouse_state.index = 0;

        uint8_t flags = mouse_state.packet[0];

        // Bit 6/7 menandakan overflow delta; buang paket yang tidak valid.
        if (flags & 0xC0)
            continue;

        event->dx = (int8_t) mouse_state.packet[1];
        event->dy = (int8_t) mouse_state.packet[2];
        event->buttons = flags & 0x07;
        return true;
    }

    return false;
}

void mouse_cursor_init(void) {
    if (!mouse_state.available)
        return;

    framebuffer_set_write_hook(mouse_framebuffer_write_hook);

    mouse_cursor_restore();
    mouse_state.row = MOUSE_SCREEN_HEIGHT / 2;
    mouse_state.col = MOUSE_SCREEN_WIDTH / 2;
    mouse_state.cursor_visible = false;
    mouse_cursor_draw();
}

void mouse_cursor_hide(void) {
    mouse_cursor_restore();
}

void mouse_cursor_show(void) {
    if (!mouse_state.available || mouse_state.cursor_visible)
        return;
    mouse_cursor_draw();
}

void mouse_poll(void) {
    if (!mouse_state.available)
        return;

    struct MouseEvent event;
    while (mouse_get_event(&event)) {
        mouse_state.buttons = event.buttons;
        mouse_state.dx_accumulator += event.dx;
        mouse_state.dy_accumulator += event.dy;

        int32_t move_x = mouse_state.dx_accumulator / MOUSE_SENSITIVITY;
        int32_t move_y = mouse_state.dy_accumulator / MOUSE_SENSITIVITY;
        mouse_state.dx_accumulator -= move_x * MOUSE_SENSITIVITY;
        mouse_state.dy_accumulator -= move_y * MOUSE_SENSITIVITY;

        if (move_x == 0 && move_y == 0)
            continue;

        int32_t next_col = (int32_t) mouse_state.col + move_x;
        int32_t next_row = (int32_t) mouse_state.row - move_y;

        if (next_col < 0)
            next_col = 0;
        else if (next_col >= MOUSE_SCREEN_WIDTH)
            next_col = MOUSE_SCREEN_WIDTH - 1;

        if (next_row < 0)
            next_row = 0;
        else if (next_row >= MOUSE_SCREEN_HEIGHT)
            next_row = MOUSE_SCREEN_HEIGHT - 1;

        mouse_cursor_restore();
        mouse_state.col = (uint8_t) next_col;
        mouse_state.row = (uint8_t) next_row;
        mouse_cursor_draw();
    }
}

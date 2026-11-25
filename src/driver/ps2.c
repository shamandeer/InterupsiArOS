#include "header/driver/ps2.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"

struct PS2State {
    bool keyboard_enabled;
    bool mouse_enabled;
    uint8_t keyboard_queue[PS2_DEVICE_QUEUE_SIZE];
    uint32_t keyboard_head;
    uint32_t keyboard_tail;
    uint8_t mouse_queue[PS2_DEVICE_QUEUE_SIZE];
    uint32_t mouse_head;
    uint32_t mouse_tail;
};

static struct PS2State ps2_state;

static bool ps2_wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000; i++)
        if (!(in(PS2_STATUS) & PS2_INPUT_FULL))
            return true;
    return false;
}

static bool ps2_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; i++)
        if (in(PS2_STATUS) & PS2_OUTPUT_FULL)
            return true;
    return false;
}

static bool ps2_write_command(uint8_t command) {
    if (!ps2_wait_input_empty())
        return false;
    out(PS2_COMMAND, command);
    return true;
}

static bool ps2_write_data(uint8_t data) {
    if (!ps2_wait_input_empty())
        return false;
    out(PS2_DATA, data);
    return true;
}

static uint8_t ps2_read_data(void) {
    return in(PS2_DATA);
}

static uint8_t ps2_read_status(void) {
    return in(PS2_STATUS);
}

static void ps2_flush_output(void) {
    for (uint32_t i = 0; i < 32 && (ps2_read_status() & PS2_OUTPUT_FULL); i++)
        (void) ps2_read_data();
}

bool ps2_controller_init(void) {
    // Jangan biarkan device mengirim data saat configuration byte diubah.
    if (!ps2_write_command(0xAD)) return false; // disable keyboard port
    if (!ps2_write_command(0xA7)) return false; // disable mouse port

    // Buang byte lama supaya tidak dianggap ACK untuk command berikutnya.
    ps2_flush_output();

    if (!ps2_write_command(0x20)) return false;
    if (!ps2_wait_output_full()) return false;
    uint8_t config = ps2_read_data();

    // Aktifkan IRQ keyboard (bit 0) dan mouse (bit 1) pada controller.
    // Interrupt tetap belum sampai ke CPU karena PIC masih mem-mask IRQ-nya.
    config |= 0x03;

    // Tabel keyboard saat ini memakai scancode set 1, sedangkan keyboard
    // PS/2 mengirim set 2. Biarkan controller melakukan translasi.
    config |= 0x40;

    if (!ps2_write_command(0x60)) return false;
    if (!ps2_write_data(config)) return false;

    if (!ps2_write_command(0xAE)) return false; // enable keyboard port
    if (!ps2_write_command(0xA0)) return false; // enable mouse port

    return true;
}

bool ps2_send_mouse_command(uint8_t command) {
    // Saat init belum ada IRQ, tapi keyboard bisa saja sudah mengisi buffer.
    ps2_flush_output();

    // 0xD4 memberitahu controller bahwa byte berikut ditujukan ke mouse.
    if (!ps2_write_command(0xD4)) return false;
    if (!ps2_write_data(command)) return false;

    if (!ps2_wait_output_full()) return false;
    uint8_t status = ps2_read_status();
    uint8_t response = ps2_read_data();

    return (status & PS2_AUX_DATA) && response == 0xFA;
}

static bool ps2_queue_push(uint8_t *queue, uint32_t *head, uint32_t *tail, uint8_t data) {
    uint32_t next = (*head + 1) % PS2_DEVICE_QUEUE_SIZE;
    if (next == *tail)
        return false;

    queue[*head] = data;
    *head = next;
    return true;
}

static bool ps2_queue_pop(uint8_t *queue, uint32_t *head, uint32_t *tail, uint8_t *data) {
    if (*tail == *head)
        return false;

    *data = queue[*tail];
    *tail = (*tail + 1) % PS2_DEVICE_QUEUE_SIZE;
    return true;
}

void ps2_handle_irq(void) {
    uint8_t status = ps2_read_status();
    if (!(status & PS2_OUTPUT_FULL))
        return;

    uint8_t data = ps2_read_data();

    if (status & PS2_AUX_DATA) {
        if (ps2_state.mouse_enabled)
            ps2_queue_push(ps2_state.mouse_queue, &ps2_state.mouse_head,
                           &ps2_state.mouse_tail, data);
    } else {
        if (ps2_state.keyboard_enabled)
            ps2_queue_push(ps2_state.keyboard_queue, &ps2_state.keyboard_head,
                           &ps2_state.keyboard_tail, data);
    }
}

void ps2_enable_keyboard_irq(void) {
    ps2_state.keyboard_enabled = true;
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void ps2_enable_mouse_irq(void) {
    ps2_state.mouse_enabled = true;
    out(PIC2_DATA, in(PIC2_DATA) & ~(1 << (IRQ_MOUSE - 8)));
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_CASCADE));
}

void ps2_enable_irqs(void) {
    ps2_enable_keyboard_irq();
    ps2_enable_mouse_irq();
}

bool ps2_keyboard_pop(uint8_t *data) {
    return ps2_queue_pop(ps2_state.keyboard_queue, &ps2_state.keyboard_head,
                         &ps2_state.keyboard_tail, data);
}

bool ps2_mouse_pop(uint8_t *data) {
    return ps2_queue_pop(ps2_state.mouse_queue, &ps2_state.mouse_head,
                         &ps2_state.mouse_tail, data);
}

void ps2_flush_keyboard(void) {
    ps2_state.keyboard_head = 0;
    ps2_state.keyboard_tail = 0;
}

void ps2_flush_mouse(void) {
    ps2_state.mouse_head = 0;
    ps2_state.mouse_tail = 0;
}

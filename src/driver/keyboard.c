#include "header/driver/keyboard.h"
#include "header/driver/ps2.h"

#define KEYBOARD_EVENT_QUEUE_SIZE 16

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

const char alt_keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '!', '@', '#', '$', '%', '^',  '&', '*', '(',  ')',  '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '{',  '}', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',   0, '|',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', '<', '>', '?',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

struct KeyboardState {
    bool shift_mode;
    bool read_extended_mode;
    bool keyboard_input_on;
    uint8_t queue[KEYBOARD_EVENT_QUEUE_SIZE];
    uint32_t queue_head;
    uint32_t queue_tail;
};

static struct KeyboardState keyboard_state;

static bool keyboard_enqueue(uint8_t event) {
    uint32_t next = (keyboard_state.queue_head + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    if (next == keyboard_state.queue_tail)
        return false;

    keyboard_state.queue[keyboard_state.queue_head] = event;
    keyboard_state.queue_head = next;
    return true;
}

static bool keyboard_dequeue(uint8_t *event) {
    if (keyboard_state.queue_head == keyboard_state.queue_tail)
        return false;

    *event = keyboard_state.queue[keyboard_state.queue_tail];
    keyboard_state.queue_tail = (keyboard_state.queue_tail + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    return true;
}

static void keyboard_handle_byte(uint8_t scancode) {
    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return;
    }

    bool release = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;

    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false;

        // Release event panah (mis. E0 C8) tidak perlu diteruskan.
        if (release)
            return;

        switch (code) {
            case EXT_SCANCODE_UP:        keyboard_enqueue(KEYBOARD_EVENT_UP);        break;
            case EXT_SCANCODE_DOWN:      keyboard_enqueue(KEYBOARD_EVENT_DOWN);      break;
            case EXT_SCANCODE_LEFT:      keyboard_enqueue(KEYBOARD_EVENT_LEFT);      break;
            case EXT_SCANCODE_RIGHT:     keyboard_enqueue(KEYBOARD_EVENT_RIGHT);     break;
            case EXT_SCANCODE_HOME:      keyboard_enqueue(KEYBOARD_EVENT_HOME);      break;
            case EXT_SCANCODE_END:       keyboard_enqueue(KEYBOARD_EVENT_END);       break;
            case EXT_SCANCODE_DELETE:    keyboard_enqueue(KEYBOARD_EVENT_DELETE);    break;
            case EXT_SCANCODE_PAGE_UP:   keyboard_enqueue(KEYBOARD_EVENT_PAGE_UP);   break;
            case EXT_SCANCODE_PAGE_DOWN: keyboard_enqueue(KEYBOARD_EVENT_PAGE_DOWN); break;
            default: break;
        }
        return;
    }

    // Shift kiri (0x2A) dan shift kanan (0x36).
    if (code == 0x2A || code == 0x36) {
        keyboard_state.shift_mode = !release;
        return;
    }

    if (release)
        return;

    char mapped_char = keyboard_state.shift_mode
                     ? alt_keyboard_scancode_1_to_ascii_map[code]
                     : keyboard_scancode_1_to_ascii_map[code];

    if (mapped_char != 0)
        keyboard_enqueue((uint8_t) mapped_char);
}

void keyboard_init(void) {
    keyboard_state.shift_mode = false;
    keyboard_state.read_extended_mode = false;
    keyboard_state.keyboard_input_on = false;
    keyboard_state.queue_head = 0;
    keyboard_state.queue_tail = 0;
    ps2_flush_keyboard();
}

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

bool keyboard_get_event(uint8_t *event) {
    if (event == (void *) 0)
        return false;

    if (!keyboard_state.keyboard_input_on) {
        uint8_t ignored;
        while (ps2_keyboard_pop(&ignored))
            ;
        return false;
    }

    uint8_t raw;
    while (ps2_keyboard_pop(&raw)) {
        keyboard_handle_byte(raw);
        if (keyboard_dequeue(event))
            return true;
    }

    return keyboard_dequeue(event);
}

void get_keyboard_buffer(uint8_t *buf) {
    if (buf == (void *) 0)
        return;

    uint8_t event = 0;
    if (keyboard_get_event(&event))
        *buf = event;
    else
        *buf = 0;
}

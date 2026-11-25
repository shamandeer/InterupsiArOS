#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EXT_SCANCODE_UP        0x48
#define EXT_SCANCODE_DOWN      0x50

#define EXTENDED_SCANCODE_BYTE 0xE0

// Event keyboard berada di luar rentang ASCII printable (0x20..0x7E).
#define KEYBOARD_EVENT_UP        0x80
#define KEYBOARD_EVENT_DOWN      0x81

// Nama lama yang sudah dipakai shell/history.
#define KEYBOARD_UP_BYTE   KEYBOARD_EVENT_UP
#define KEYBOARD_DOWN_BYTE KEYBOARD_EVENT_DOWN

extern const char keyboard_scancode_1_to_ascii_map[256];
extern const char alt_keyboard_scancode_1_to_ascii_map[256];

void keyboard_init(void);
void keyboard_state_activate(void);
void keyboard_state_deactivate(void);

// Ambil satu event keyboard. Return false jika belum ada event.
bool keyboard_get_event(uint8_t *event);

// Kompatibilitas dengan syscall lama: isi buf dengan event, atau 0 jika kosong.
void get_keyboard_buffer(uint8_t *buf);

#endif

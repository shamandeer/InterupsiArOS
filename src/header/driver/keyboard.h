#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EXT_SCANCODE_UP        0x48
#define EXT_SCANCODE_DOWN      0x50
#define EXT_SCANCODE_LEFT      0x4B
#define EXT_SCANCODE_RIGHT     0x4D
#define EXT_SCANCODE_HOME      0x47
#define EXT_SCANCODE_END       0x4F
#define EXT_SCANCODE_DELETE    0x53
#define EXT_SCANCODE_PAGE_UP   0x49
#define EXT_SCANCODE_PAGE_DOWN 0x51

#define KEYBOARD_DATA_PORT     0x60
#define EXTENDED_SCANCODE_BYTE 0xE0

// Event keyboard berada di luar rentang ASCII printable (0x20..0x7E).
#define KEYBOARD_EVENT_UP        0x80
#define KEYBOARD_EVENT_DOWN      0x81
#define KEYBOARD_EVENT_LEFT      0x82
#define KEYBOARD_EVENT_RIGHT     0x83
#define KEYBOARD_EVENT_HOME      0x84
#define KEYBOARD_EVENT_END       0x85
#define KEYBOARD_EVENT_DELETE    0x86
#define KEYBOARD_EVENT_PAGE_UP   0x87
#define KEYBOARD_EVENT_PAGE_DOWN 0x88

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

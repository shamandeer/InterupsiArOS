#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04

#define MOUSE_SCREEN_WIDTH  80
#define MOUSE_SCREEN_HEIGHT 25
#define MOUSE_SENSITIVITY   8

struct MouseEvent {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
};

bool mouse_init(void);
bool mouse_get_event(struct MouseEvent *event);
void mouse_reset(void);

// Kursor software VGA text mode.
void mouse_cursor_init(void);
void mouse_cursor_hide(void);
void mouse_cursor_show(void);
void mouse_poll(void);

#endif

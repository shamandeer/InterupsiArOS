#ifndef _PS2_H
#define _PS2_H

#include <stdint.h>
#include <stdbool.h>

#define PS2_STATUS       0x64
#define PS2_COMMAND      0x64
#define PS2_DATA         0x60

#define PS2_OUTPUT_FULL  0x01
#define PS2_INPUT_FULL   0x02
#define PS2_AUX_DATA     0x20

#define PS2_DEVICE_QUEUE_SIZE 16

bool ps2_controller_init(void);
bool ps2_send_mouse_command(uint8_t command);
void ps2_enable_keyboard_irq(void);
void ps2_enable_mouse_irq(void);
void ps2_enable_irqs(void);
void ps2_handle_irq(void);

bool ps2_keyboard_pop(uint8_t *data);
bool ps2_mouse_pop(uint8_t *data);
void ps2_flush_keyboard(void);
void ps2_flush_mouse(void);

#endif

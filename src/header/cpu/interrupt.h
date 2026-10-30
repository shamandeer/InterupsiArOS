#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PIC1_OFFSET          0x20
#define PIC2_OFFSET          0x28

#define PIC1                 0x20
#define PIC2                 0xA0
#define PIC1_COMMAND         PIC1
#define PIC1_DATA            (PIC1 + 1)
#define PIC2_COMMAND         PIC2
#define PIC2_DATA            (PIC2 + 1)

#define PIC_ACK              0x20
#define PIC_DISABLE_ALL_MASK 0xFF

#define ICW1_ICW4            0x01
#define ICW1_SINGLE          0x02
#define ICW1_INTERVAL4       0x04
#define ICW1_LEVEL           0x08
#define ICW1_INIT            0x10

#define ICW4_8086            0x01
#define ICW4_AUTO            0x02
#define ICW4_BUF_SLAVE       0x08
#define ICW4_BUF_MASTER      0x0C
#define ICW4_SFNM            0x10

#define IRQ_TIMER            0
#define IRQ_KEYBOARD         1
#define IRQ_CASCADE          2
#define IRQ_COM2             3
#define IRQ_COM1             4
#define IRQ_LPT2             5
#define IRQ_FLOPPY_DISK      6
#define IRQ_LPT1_SPUR        7

#define IRQ_CMOS             8
#define IRQ_PERIPHERAL_1     9
#define IRQ_PERIPHERAL_2     10
#define IRQ_PERIPHERAL_3     11
#define IRQ_MOUSE            12
#define IRQ_FPU              13
#define IRQ_PRIMARY_ATA      14
#define IRQ_SECOND_ATA       15

struct CPURegister {
    struct {
        uint32_t edi;
        uint32_t esi;
    } __attribute__((packed)) index;
    struct {
        uint32_t esp;
        uint32_t ebp;
    } __attribute__((packed)) stack;
    struct {
        uint32_t ebx;
        uint32_t edx;
        uint32_t ecx;
        uint32_t eax;
    } __attribute__((packed)) general;
    struct {
        uint32_t gs;
        uint32_t fs;
        uint32_t es;
        uint32_t ds;
    } __attribute__((packed)) segment;
} __attribute__((packed));

// Hardware iret stack frame; inter-privilege transition pushes user esp and ss above eflags
struct InterruptStack {
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} __attribute__((packed));

struct InterruptFrame {
    struct CPURegister    cpu;
    uint32_t              int_number;
    struct InterruptStack int_stack;
} __attribute__((packed));

extern struct TSSEntry _interrupt_tss_entry;

struct TSSEntry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t unused_register[23];
} __attribute__((packed));

void set_tss_kernel_current_stack(void);
void activate_keyboard_interrupt(void);
void io_wait(void);
void pic_ack(uint8_t irq);
void pic_remap(void);
void main_interrupt_handler(struct InterruptFrame frame);
void kernel_puts(const char *buf, uint32_t count, uint8_t color);
void syscall(struct InterruptFrame frame);

#endif

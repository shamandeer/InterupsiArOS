#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/driver/mouse.h"
#include "header/driver/ps2.h"
#include "header/driver/cmos.h"
#include "header/memory/swap.h"
#include "header/text/framebuffer.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"
#include "header/process/scheduler.h"
#include "header/process/process-info.h"

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
    // Simpan isi sel di bawah kursor sebelum layar digeser.
    mouse_cursor_hide();

    memmove(
        FRAMEBUFFER_MEMORY_OFFSET,
        FRAMEBUFFER_MEMORY_OFFSET + FRAMEBUFFER_COLS * 2,
        (FRAMEBUFFER_ROWS - 1) * FRAMEBUFFER_COLS * 2
    );
    for (uint8_t col = 0; col < FRAMEBUFFER_COLS; col++)
        framebuffer_write(FRAMEBUFFER_ROWS - 1, col, ' ', 0x07, 0x00);

    mouse_cursor_show();
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

_Static_assert(PROCESS_INFO_COUNT_MAX == PROCESS_COUNT_MAX, "process-info.h tidak sinkron dengan process.h");
_Static_assert(PROCESS_INFO_NAME_LENGTH == PROCESS_NAME_LENGTH_MAX, "process-info.h tidak sinkron dengan process.h");
_Static_assert(PROCESS_INFO_STATE_READY == READY && PROCESS_INFO_STATE_WAITING == WAITING
    && PROCESS_INFO_STATE_RUNNING == RUNNING, "process-info.h tidak sinkron dengan PROCESS_STATE");

// Pointer dari user harus utuh berada di bawah area kernel (higher half)
static bool is_user_buffer(const void *ptr, uint32_t size) {
    uint32_t addr = (uint32_t) ptr;
    return addr != 0 && addr < KERNEL_VIRTUAL_ADDRESS_BASE && size <= KERNEL_VIRTUAL_ADDRESS_BASE - addr;
}

// Hancurkan process pemanggil lalu pindah ke process lain, tidak pernah iret ke process yang sudah mati
__attribute__((noreturn)) static void syscall_exit(void) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL)
        process_destroy(current->metadata.pid);
    scheduler_switch_to_next_process();
}

// process_create_user_process tidak mengembalikan PID; PID selalu naik, jadi PID terbesar = process terbaru
static uint32_t newest_process_pid(void) {
    uint32_t pid = 0;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (_process_list[i].metadata.active && _process_list[i].metadata.pid > pid)
            pid = _process_list[i].metadata.pid;
    return pid;
}

// user_request: parent_inode + name + name_len file executable. Hasil PROCESS_CREATE_* ke *retcode, PID ke *pid
static void syscall_exec(const struct EXT2DriverRequest *user_request, int32_t *retcode, uint32_t *pid) {
    if (!is_user_buffer(user_request, sizeof(*user_request)) || !is_user_buffer(retcode, sizeof(*retcode))
            || !is_user_buffer(pid, sizeof(*pid)))
        return;

    struct EXT2DriverRequest request = *user_request;
    *pid = 0;
    if (request.name_len == 0 || !is_user_buffer(request.name, request.name_len)) {
        *retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        return;
    }
    // Nama berada di memori user pemanggil, padahal file dibaca setelah CR3 pindah ke page directory baru:
    // salin dulu ke stack kernel yang ter-map di semua page directory
    char name[UINT8_MAX + 1];
    memcpy(name, request.name, request.name_len);
    name[request.name_len] = '\0';
    request.name         = name;
    request.buf          = (void *) 0;
    request.buffer_size  = PROCESS_USER_PROGRAM_SIZE;
    request.is_directory = false;

    *retcode = process_create_user_process(request);
    if (*retcode == PROCESS_CREATE_SUCCESS)
        *pid = newest_process_pid();
}

static void syscall_kill(uint32_t pid, int8_t *result) {
    if (!is_user_buffer(result, sizeof(*result)))
        return;
    if (pid == 0) {
        *result = PROCESS_KILL_FORBIDDEN;
        return;
    }
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL && current->metadata.pid == pid)
        syscall_exit();
    *result = process_destroy(pid) ? PROCESS_KILL_SUCCESS : PROCESS_KILL_NOT_FOUND;
}

static void syscall_process_info(struct ProcessInfo *buf, uint32_t capacity, uint32_t *count) {
    if (capacity > PROCESS_COUNT_MAX)
        capacity = PROCESS_COUNT_MAX;
    if (!is_user_buffer(buf, capacity * sizeof(struct ProcessInfo)) || !is_user_buffer(count, sizeof(*count)))
        return;

    uint32_t n = 0;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX && n < capacity; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (!pcb->metadata.active)
            continue;
        buf[n].pid   = pcb->metadata.pid;
        buf[n].state = (uint8_t) pcb->metadata.state;
        memcpy(buf[n].name, pcb->metadata.name, PROCESS_INFO_NAME_LENGTH);
        n++;
    }
    *count = n;
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
            get_keyboard_buffer((uint8_t *) frame.cpu.general.ebx);
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
        case 10: // exit, ebx = exit code (belum dipakai)
            syscall_exit();
        case 11: // exec
            syscall_exec((const struct EXT2DriverRequest *) frame.cpu.general.ebx,
                (int32_t *) frame.cpu.general.ecx, (uint32_t *) frame.cpu.general.edx);
            break;
        case 12: // kill
            syscall_kill(frame.cpu.general.ebx, (int8_t *) frame.cpu.general.ecx);
            break;
        case 13: { // get_time: ebx = pointer to struct Time in user space
            uint32_t addr = frame.cpu.general.ebx;
            // Tolak NULL, overflow, dan rentang yang menyentuh kernel (>= 0xC0000000)
            if (addr == 0 ||
                addr > 0xFFFFFFFFU - sizeof(struct Time) ||
                addr + sizeof(struct Time) > 0xC0000000U)
                break;
            cmos_read_time((struct Time *) addr);
            break;
        }
        case 14: // put_char_at: ebx = char, ecx = row, edx = col (kursor shell tidak disentuh)
            if (frame.cpu.general.ecx >= FRAMEBUFFER_ROWS || frame.cpu.general.edx >= FRAMEBUFFER_COLS)
                break;
            framebuffer_write(
                (uint8_t) frame.cpu.general.ecx,
                (uint8_t) frame.cpu.general.edx,
                (char) frame.cpu.general.ebx,
                0xF, 0x0
            );
            break;
        case 15: // Reserved for heap and other stuff ntar
            break;
        case 16: // process info (ps)
            syscall_process_info((struct ProcessInfo *) frame.cpu.general.ebx,
                frame.cpu.general.ecx, (uint32_t *) frame.cpu.general.edx);
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            ps2_handle_irq();
            pic_ack(IRQ_KEYBOARD);
            break;
        case PIC2_OFFSET + (IRQ_MOUSE - 8):
            ps2_handle_irq();
            pic_ack(IRQ_MOUSE);
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
        case PIC1_OFFSET + IRQ_TIMER: {
            // Polling mouse di sini menjaga kursor tetap bergerak tanpa
            // menambah syscall atau thread khusus.
            mouse_poll();

            // intsetup.s sudah menaruh esp user di cpu.stack.esp untuk interrupt dari ring 3
            struct Context context = {
                .cpu = frame.cpu,
                .eip = frame.int_stack.eip,
                .eflags = frame.int_stack.eflags,
                .page_directory_virtual_addr = paging_get_current_page_directory_addr()
            };
            context.cpu.stack.esp = frame.int_stack.esp;
            scheduler_save_context_to_current_running_pcb(context);
            pic_ack(IRQ_TIMER);
            scheduler_switch_to_next_process();
            break;
        }
    }
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void activate_timer_interrupt(void) {
    __asm__ volatile("cli");
    // Setup how often PIT fire
    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));
    // Activate the interrupt
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}
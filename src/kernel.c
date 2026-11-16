#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/stdlib/string.h"
#include "header/process/scheduler.h"

static void kernel_halt_with_error(const char *msg) {
    for (uint8_t col = 0; msg[col] != '\0'; col++)
        framebuffer_write(0, col, msg[col], 0x0C, 0x00);
    while (true);
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();

    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    initialize_filesystem_ext2();
    swap_init();

    gdt_install_tss();
    set_tss_register();

    // Init process: shell sebagai process 0
    struct EXT2DriverRequest request = {
        .buf          = (uint8_t *) 0,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = ROOT_INODE,
        .buffer_size  = PROCESS_USER_PROGRAM_SIZE,
        .is_directory = false,
    };
    set_tss_kernel_current_stack();
    if (process_create_user_process(request) != PROCESS_CREATE_SUCCESS)
        kernel_halt_with_error("Error: failed to create init process 'shell'");
    paging_use_page_directory(_process_list[0].context.page_directory_virtual_addr);
    kernel_execute_user_program((void *) 0x0);
    while (true);
}

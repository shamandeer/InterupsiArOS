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

#define USER_PROGRAM_BASE      ((uint8_t *) 0)
#define USER_PROGRAM_MAX_SIZE  0x100000u
// kernel_execute_user_program() puts user esp at USER_PROGRAM_BASE + 4 MiB - 4
#define USER_STACK_PAGE        ((uint8_t *) 0x3FF000)

// Print error message at the first row then stop the kernel
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

    // Identity mapping of low memory is removed by kernel-entrypoint.s, so user page (program + stack) must be mapped first
    if (!paging_allocate_user_page_frame(&_paging_kernel_page_directory, USER_PROGRAM_BASE))
        kernel_halt_with_error("Error: failed to map user program page");
    if (!paging_allocate_user_page_frame(&_paging_kernel_page_directory, USER_STACK_PAGE))
        kernel_halt_with_error("Error: failed to map user stack page");

    // Load flat binary "shell" from root directory into virtual address 0x0
    struct EXT2DriverRequest req = {
        .buf          = USER_PROGRAM_BASE,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = ROOT_INODE,
        .buffer_size  = USER_PROGRAM_MAX_SIZE,
        .is_directory = false
    };
    int8_t retcode = read(req);
    if (retcode != 0)
        kernel_halt_with_error("Error: failed to read 'shell' from root directory");

    set_tss_kernel_current_stack();
    kernel_execute_user_program((void *) 0x0);
    while (true);
}

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

#define USER_PROGRAM_BASE  ((uint8_t *) 0)
#define USER_PROGRAM_PAGES 80u
#define USER_PROGRAM_SIZE  (USER_PROGRAM_PAGES * PAGE_FRAME_SIZE)
// Matches iret user esp (0x400000 - 4) pushed in kernel_execute_user_program
#define USER_STACK_TOP     0x400000u
#define USER_STACK_PAGES   8u

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

    for (uint32_t i = 0; i < USER_PROGRAM_PAGES; i++) {
        void *vaddr = USER_PROGRAM_BASE + i * PAGE_FRAME_SIZE;
        if (!paging_allocate_user_page_frame(&_paging_kernel_page_directory, vaddr))
            kernel_halt_with_error("Error: failed to map user program page");
    }
    memset(USER_PROGRAM_BASE, 0, USER_PROGRAM_SIZE);

    for (uint32_t i = 1; i <= USER_STACK_PAGES; i++) {
        void *vaddr = (void *) (USER_STACK_TOP - i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(&_paging_kernel_page_directory, vaddr))
            kernel_halt_with_error("Error: failed to map user stack page");
    }

    struct EXT2DriverRequest req = {
        .buf          = USER_PROGRAM_BASE,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = ROOT_INODE,
        .buffer_size  = USER_PROGRAM_SIZE,
        .is_directory = false,
    };
    int8_t retcode = read(req);
    if (retcode != 0)
        kernel_halt_with_error("Error: failed to read 'shell' from root directory");

    set_tss_kernel_current_stack();
    kernel_execute_user_program((void *) 0x0);
    while (true);
}

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

    paging_allocate_user_page_frame(&_paging_kernel_page_directory, (uint8_t *) 0);
    struct EXT2DriverRequest request = {
        .buf = (uint8_t *) 0,
        .name = "shell",
        .parent_inode = 2,
        .buffer_size = 0x100000,
        .name_len = 5,
    };
    read(request);

    set_tss_kernel_current_stack();
    kernel_execute_user_program((uint8_t *) 0);
    while (true);
}
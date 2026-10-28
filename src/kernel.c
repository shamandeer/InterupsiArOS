#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/swap.h"

// ==== TEST PART 4 (hapus sebelum commit) ====
static void test_print(uint8_t row, const char *label, uint32_t value) {
    uint8_t col = 0;
    for (; *label; label++)
        framebuffer_write(row, col++, *label, 0xF, 0);
    for (int shift = 28; shift >= 0; shift -= 4)
        framebuffer_write(row, col++, "0123456789ABCDEF"[(value >> shift) & 0xF], 0xE, 0);
}

static uint8_t test_slot_buf[PAGE_FRAME_SIZE];

static void test_swap_part4(void) {
    struct PageDirectory *pd = paging_get_current_page_directory_addr();
    uint32_t ok = 0;
    for (uint32_t i = 0; i < 1100; i++) {
        uint32_t *page = (uint32_t *) (0x400000 + i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(pd, page))
            break;
        *page = 0xC0DE0000 + i; // only touch the page just allocated, never a swapped one
        ok++;
    }

    uint8_t row = 13;
    test_print(row++, "alloc sukses  : ", ok);
    test_print(row++, "swap_out_count: ", swap_out_count);
    test_print(row++, "free frame    : ", page_manager_state.free_page_frame_count);
    test_print(row++, "free slot     : ", swap_free_slot_count());
    if (ok == 0)
        return;

    volatile struct PageTableEntry *pte0 = &paging_get_page_table(pd, 1, true)->table[0];
    test_print(row++, "page0 present : ", pte0->flag.present_bit);
    test_print(row++, "page0 swapped : ", pte0->ignored & PTE_SWAPPED_BIT);
    test_print(row++, "page0 slot    : ", pte_get_swap_slot(pte0));
    swap_read_page(pte_get_swap_slot(pte0), test_slot_buf); // into kernel buffer, page0 stays swapped
    test_print(row++, "isi slot page0: ", *(uint32_t *) test_slot_buf);
    test_print(row++, "page terakhir : ", *(uint32_t *) (0x400000 + (ok - 1) * PAGE_FRAME_SIZE));
}
// ==== END TEST PART 4 ====

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();

    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    keyboard_state_activate();
    initialize_filesystem_ext2();
    swap_init();
    test_swap_part4(); // TEST PART 4 (hapus sebelum commit)

    int row = 0, col = 0;
    while (true) {
        char c = 0;
        get_keyboard_buffer(&c);
        if (c != 0) {
            if (c == '\n') {
                row++;
                col = 0;
            } else {
                framebuffer_write(row, col, c, 0xF, 0);
                if (col >= 79) {
                    row++;
                    col = 0;
                } else {
                    col++;
                }
            }
            if (row >= 25) {
                row = 0;
            }
            framebuffer_set_cursor(row, col);
        }
    }
}
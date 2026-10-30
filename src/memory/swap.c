#include "header/memory/swap.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"

// 1 bit per slot, bit set = slot used
static uint32_t swap_slot_bitmap[SWAP_SLOT_COUNT / 32];
static uint32_t swap_free_slots = SWAP_SLOT_COUNT;
static uint32_t clock_hand = KERNEL_RESERVED_PAGE_FRAME_COUNT;
uint32_t swap_out_count;
uint32_t swap_in_count;

static bool is_slot_used(uint32_t slot) {
    return slot < SWAP_SLOT_COUNT && ((swap_slot_bitmap[slot / 32] >> (slot % 32)) & 1u);
}

void swap_init(void) {
    memset(swap_slot_bitmap, 0, sizeof(swap_slot_bitmap));
    swap_free_slots = SWAP_SLOT_COUNT;
    clock_hand      = KERNEL_RESERVED_PAGE_FRAME_COUNT;
    swap_out_count  = 0;
    swap_in_count   = 0;
}

uint32_t swap_slot_alloc(void) {

    for (uint32_t slot = 0; slot < SWAP_SLOT_COUNT; slot++) {
        if (!is_slot_used(slot)) {
            swap_slot_bitmap[slot / 32] |= 1u << (slot % 32);
            swap_free_slots--;
            return slot;
        }
    }
    return SWAP_INVALID_SLOT;
}

void swap_slot_free(uint32_t slot) {
    if (is_slot_used(slot)) {
        swap_slot_bitmap[slot / 32] &= ~(1u << (slot % 32));
        swap_free_slots++;
    }
}

uint32_t swap_free_slot_count(void) {
    return swap_free_slots;
}

void swap_write_page(uint32_t slot, const void *vaddr) {
    if (!is_slot_used(slot))
        return;
    write_blocks(vaddr, SWAP_START_LBA + slot * BLOCKS_PER_PAGE, BLOCKS_PER_PAGE);
}

void swap_read_page(uint32_t slot, void *vaddr) {
    if (!is_slot_used(slot))
        return;
    read_blocks(vaddr, SWAP_START_LBA + slot * BLOCKS_PER_PAGE, BLOCKS_PER_PAGE);
}

uint32_t swap_clock_pick_victim(uint32_t *hand, bool (*spare)(void *ctx, uint32_t frame), void *ctx) {
    const uint32_t user_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT;
    for (uint32_t step = 0; step < 2 * user_frame_count; step++) {
        uint32_t frame = *hand;
        *hand = frame + 1 < PAGE_FRAME_MAX_COUNT ? frame + 1 : KERNEL_RESERVED_PAGE_FRAME_COUNT;
        if (!page_manager_state.page_frame_map[frame] || !page_manager_state.frame_swappable[frame])
            continue;
        if (!spare(ctx, frame))
            return frame;
    }
    return PAGING_INVALID_FRAME;
}

// PTE for vaddr, NULL if its page table does not exist (never creates one)
static volatile struct PageTableEntry *vaddr_pte(struct PageDirectory *page_dir, uint32_t vaddr) {
    struct PageTable *page_table = paging_get_page_table(page_dir, (vaddr >> 22) & 0x3FF, true);
    return page_table ? &page_table->table[(vaddr >> 12) & 0x3FF] : NULL;
}

// Second chance: referenced page gets access_bit cleared and survives this round
static bool spare_if_accessed(void *ctx, uint32_t frame) {
    volatile struct PageTableEntry *pte = vaddr_pte(ctx, page_manager_state.frame_vaddr[frame]);
    if (pte == NULL || !pte->flag.present_bit)
        return true; // inconsistent metadata, never evict
    if (!pte->flag.access_bit)
        return false;
    pte->flag.access_bit = 0;
    flush_single_tlb((void *) (uintptr_t) page_manager_state.frame_vaddr[frame]);
    return true;
}

bool swap_out_one(struct PageDirectory *page_dir) {
    uint32_t frame = swap_clock_pick_victim(&clock_hand, spare_if_accessed, page_dir);
    if (frame == PAGING_INVALID_FRAME)
        return false;
    void *vaddr = (void *) (uintptr_t) page_manager_state.frame_vaddr[frame];
    volatile struct PageTableEntry *pte = vaddr_pte(page_dir, page_manager_state.frame_vaddr[frame]);

    uint32_t slot = swap_slot_alloc();
    if (slot == SWAP_INVALID_SLOT)
        return false;
    swap_write_page(slot, vaddr); // must happen while vaddr is still mapped
    pte_set_swapped(pte, slot);
    flush_single_tlb(vaddr);

    page_manager_state.page_frame_map[frame]  = false;
    page_manager_state.frame_vaddr[frame]     = 0;
    page_manager_state.frame_swappable[frame] = false;
    page_manager_state.free_page_frame_count++;

    swap_out_count++;
    return true;
}

bool swap_in(struct PageDirectory *page_dir, void *vaddr) {
    uint32_t page = (uint32_t) (uintptr_t) vaddr & ~(PAGE_FRAME_SIZE - 1);
    volatile struct PageTableEntry *pte = vaddr_pte(page_dir, page);
    if (pte == NULL || !pte_is_swapped(pte))
        return false;

    uint32_t slot  = pte_get_swap_slot(pte);  // before update_page_table_entry overwrites it with the frame
    uint32_t frame = paging_find_free_frame(); // may evict another page, never this one (it has no frame)
    if (frame == PAGING_INVALID_FRAME)
        return false;
    page_manager_state.frame_swappable[frame] = true;
    page_manager_state.frame_vaddr[frame]     = page;

    void *page_addr = (void *) (uintptr_t) page;
    struct PageTableEntryFlag flag = {
        .present_bit = true,
        .write_bit   = true,
        .us_bit      = true,
    };
    update_page_table_entry(page_dir, (void *) (uintptr_t) (frame * PAGE_FRAME_SIZE), page_addr, flag);
    pte_clear_swapped(pte);

    swap_read_page(slot, page_addr); // slot must still be allocated, swap_read_page rejects free slots
    swap_slot_free(slot);

    swap_in_count++;
    return true;
}

#define PANIC_FG 0xF // white
#define PANIC_BG 0x4 // red

static uint8_t panic_puts(uint8_t row, uint8_t col, const char *s) {
    for (; *s; s++)
        framebuffer_write(row, col++, *s, PANIC_FG, PANIC_BG);
    return col;
}

static uint8_t panic_put_hex(uint8_t row, uint8_t col, uint32_t value) {
    col = panic_puts(row, col, "0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        framebuffer_write(row, col++, "0123456789ABCDEF"[(value >> shift) & 0xF], PANIC_FG, PANIC_BG);
    return col;
}

// Show fault info on the two bottom rows, then stop the CPU for good
__attribute__((noreturn)) static void page_fault_panic(const char *reason, uint32_t fault_addr, uint32_t error_code) {
    for (uint8_t col = 0; col < 80; col++) {
        framebuffer_write(23, col, ' ', PANIC_FG, PANIC_BG);
        framebuffer_write(24, col, ' ', PANIC_FG, PANIC_BG);
    }
    uint8_t col = panic_puts(23, 0, "PAGE FAULT (");
    col = panic_puts(23, col, reason);
    col = panic_puts(23, col, ") CR2=");
    col = panic_put_hex(23, col, fault_addr);
    col = panic_puts(23, col, " ERR=");
    panic_put_hex(23, col, error_code);

    col = panic_puts(24, 0, error_code & 0x1 ? "P=1 protection  " : "P=0 not-present  ");
    col = panic_puts(24, col, error_code & 0x2 ? "W=1 write  " : "W=0 read  ");
    panic_puts(24, col, error_code & 0x4 ? "U=1 user" : "U=0 kernel");

    for (;;) {
#ifdef __i386__ // host test build (arm64) has no cli/hlt, never reaches here anyway
        __asm__ volatile("cli; hlt");
#endif
    }
}

void page_fault_handler(uint32_t fault_addr, uint32_t error_code) {
    if (error_code & 0x1)
        page_fault_panic("protection", fault_addr, error_code);

    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    volatile struct PageTableEntry *pte = vaddr_pte(page_dir, fault_addr);
    if (pte == NULL || !pte_is_swapped(pte))
        page_fault_panic("not allocated", fault_addr, error_code);

    if (!swap_in(page_dir, (void *) (uintptr_t) fault_addr))
        page_fault_panic("swap in gagal", fault_addr, error_code);
}

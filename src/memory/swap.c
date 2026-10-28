#include "header/memory/swap.h"
#include "header/stdlib/string.h"

// 1 bit per slot, bit set = slot used
static uint32_t swap_slot_bitmap[SWAP_SLOT_COUNT / 32];
static uint32_t swap_free_slots = SWAP_SLOT_COUNT;
static uint32_t clock_hand = KERNEL_RESERVED_PAGE_FRAME_COUNT;
uint32_t swap_out_count;

static bool is_slot_used(uint32_t slot) {
    return slot < SWAP_SLOT_COUNT && ((swap_slot_bitmap[slot / 32] >> (slot % 32)) & 1u);
}

void swap_init(void) {
    memset(swap_slot_bitmap, 0, sizeof(swap_slot_bitmap));
    swap_free_slots = SWAP_SLOT_COUNT;
    clock_hand      = KERNEL_RESERVED_PAGE_FRAME_COUNT;
    swap_out_count  = 0;
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

// PTE mapping a swappable frame, found via frame_vaddr. NULL if its page table does not exist
static volatile struct PageTableEntry *frame_pte(struct PageDirectory *page_dir, uint32_t frame) {
    uint32_t vaddr = page_manager_state.frame_vaddr[frame];
    struct PageTable *page_table = paging_get_page_table(page_dir, (vaddr >> 22) & 0x3FF, true);
    return page_table ? &page_table->table[(vaddr >> 12) & 0x3FF] : NULL;
}

// Second chance: referenced page gets access_bit cleared and survives this round
static bool spare_if_accessed(void *ctx, uint32_t frame) {
    volatile struct PageTableEntry *pte = frame_pte(ctx, frame);
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
    volatile struct PageTableEntry *pte = frame_pte(page_dir, frame);

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

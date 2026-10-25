#include "header/memory/swap.h"
#include "header/stdlib/string.h"

// 1 bit per slot, bit set = slot used
static uint32_t swap_slot_bitmap[SWAP_SLOT_COUNT / 32];

static bool is_slot_used(uint32_t slot) {
    return slot < SWAP_SLOT_COUNT && ((swap_slot_bitmap[slot / 32] >> (slot % 32)) & 1u);
}

void swap_init(void) {
    memset(swap_slot_bitmap, 0, sizeof(swap_slot_bitmap));
}

uint32_t swap_slot_alloc(void) {
    
    for (uint32_t slot = 0; slot < SWAP_SLOT_COUNT; slot++) {
        if (!is_slot_used(slot)) {
            swap_slot_bitmap[slot / 32] |= 1u << (slot % 32);
            return slot;
        }
    }
    return SWAP_INVALID_SLOT;
}

void swap_slot_free(uint32_t slot) {
    if (is_slot_used(slot))
        swap_slot_bitmap[slot / 32] &= ~(1u << (slot % 32));
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

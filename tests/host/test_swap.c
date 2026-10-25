// host tests for swap slot manager & swap page I/O (milestone 3), run with: make test-swap
#include <stdio.h>
#include <string.h>
#include "header/memory/swap.h"

void fake_disk_reset(void);
uint8_t *fake_disk_raw(void);

#define EXT2_AREA_SIZE (SWAP_START_LBA * BLOCK_SIZE)                                       // 4 MiB
#define DISK_SIZE      ((SWAP_START_LBA + SWAP_SLOT_COUNT * BLOCKS_PER_PAGE) * BLOCK_SIZE) // 8 MiB

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) passes++; \
    else { fails++; printf("  FAIL %s:%d  %s\n", __func__, __LINE__, #cond); } \
} while (0)

static void fill_pattern(uint8_t *buf, uint32_t size, uint32_t seed) {
    for (uint32_t i = 0; i < size; i++)
        buf[i] = (uint8_t) (i * 7u + seed * 13u + (i / BLOCK_SIZE) * 31u);
}

// first-fit allocator hands out 0..SWAP_SLOT_COUNT-1 in order
static bool alloc_all_slots(void) {
    for (uint32_t i = 0; i < SWAP_SLOT_COUNT; i++)
        if (swap_slot_alloc() != i)
            return false;
    return true;
}

static uint8_t *slot_on_disk(uint32_t slot) {
    return fake_disk_raw() + (SWAP_START_LBA + slot * BLOCKS_PER_PAGE) * BLOCK_SIZE;
}

/* ---------- tests ---------- */

static void test_alloc_until_full(void) {
    swap_init();
    CHECK(alloc_all_slots());
    CHECK(swap_slot_alloc() == SWAP_INVALID_SLOT); // slot ke-1025
}

static void test_free_and_reuse(void) {
    swap_init();
    alloc_all_slots();
    swap_slot_free(500);
    CHECK(swap_slot_alloc() == 500);
    CHECK(swap_slot_alloc() == SWAP_INVALID_SLOT);

    // double free & out of range free must not corrupt bitmap
    swap_slot_free(500);
    swap_slot_free(500);
    swap_slot_free(SWAP_SLOT_COUNT);
    swap_slot_free(SWAP_INVALID_SLOT);
    CHECK(swap_slot_alloc() == 500);
    CHECK(swap_slot_alloc() == SWAP_INVALID_SLOT);
}

static void test_roundtrip(void) {
    static uint8_t page[PAGE_FRAME_SIZE], back[PAGE_FRAME_SIZE];
    const uint32_t slots[] = {0, 1, 511, 1023};
    const uint32_t n = sizeof(slots) / sizeof(slots[0]);
    fake_disk_reset();
    swap_init();
    alloc_all_slots();

    for (uint32_t i = 0; i < n; i++) {
        fill_pattern(page, PAGE_FRAME_SIZE, i + 1);
        swap_write_page(slots[i], page);
    }
    for (uint32_t i = 0; i < n; i++) {
        fill_pattern(page, PAGE_FRAME_SIZE, i + 1);
        memset(back, 0, PAGE_FRAME_SIZE);
        swap_read_page(slots[i], back);
        CHECK(memcmp(back, page, PAGE_FRAME_SIZE) == 0);
        CHECK(memcmp(slot_on_disk(slots[i]), page, PAGE_FRAME_SIZE) == 0); // landed at LBA 8192 + 8s
    }

    // neighbour slot never written must stay zero
    memset(page, 0, PAGE_FRAME_SIZE);
    CHECK(memcmp(slot_on_disk(2), page, PAGE_FRAME_SIZE) == 0);
}

static void test_ext2_area_untouched(void) {
    static uint8_t snapshot[EXT2_AREA_SIZE], page[PAGE_FRAME_SIZE];
    uint8_t *raw = fake_disk_raw();
    fake_disk_reset();
    fill_pattern(raw, EXT2_AREA_SIZE, 99);
    memcpy(snapshot, raw, EXT2_AREA_SIZE);

    swap_init();
    alloc_all_slots();
    fill_pattern(page, PAGE_FRAME_SIZE, 7);
    for (uint32_t slot = 0; slot < SWAP_SLOT_COUNT; slot++)
        swap_write_page(slot, page);
    CHECK(memcmp(raw, snapshot, EXT2_AREA_SIZE) == 0);
}

static void test_invalid_slot_rejected(void) {
    static uint8_t snapshot[DISK_SIZE], page[PAGE_FRAME_SIZE], back[PAGE_FRAME_SIZE];
    uint8_t *raw = fake_disk_raw();
    fake_disk_reset();
    swap_init();
    uint32_t used = swap_slot_alloc();
    fill_pattern(page, PAGE_FRAME_SIZE, 3);
    memcpy(snapshot, raw, DISK_SIZE);

    // SWAP_INVALID_SLOT * 8 + 8192 wraps to LBA 8184, inside EXT2, if not rejected
    swap_write_page(SWAP_INVALID_SLOT, page);
    swap_write_page(SWAP_SLOT_COUNT, page);
    swap_write_page(used + 1, page); // in range but not allocated
    CHECK(memcmp(raw, snapshot, DISK_SIZE) == 0);

    memcpy(back, page, PAGE_FRAME_SIZE);
    swap_read_page(SWAP_INVALID_SLOT, back);
    swap_read_page(SWAP_SLOT_COUNT, back);
    swap_read_page(used + 1, back);
    CHECK(memcmp(back, page, PAGE_FRAME_SIZE) == 0);
}

int main(void) {
    test_alloc_until_full();
    test_free_and_reuse();
    test_roundtrip();
    test_ext2_area_untouched();
    test_invalid_slot_rejected();

    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}

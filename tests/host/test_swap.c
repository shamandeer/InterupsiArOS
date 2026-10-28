// host tests for swap slot manager & swap page I/O (milestone 3), run with: make test-swap
#include <stdio.h>
#include <string.h>
#include "header/memory/swap.h"

void fake_disk_reset(void);
uint8_t *fake_disk_raw(void);

// paging.c is not linked on host: own frame state + stubs so swap_out_one links (never called here)
struct PageManagerState page_manager_state;
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index, bool check_only) {
    (void) page_dir; (void) page_directory_index; (void) check_only;
    return NULL;
}
void flush_single_tlb(void *virtual_addr) {
    (void) virtual_addr;
}

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
    CHECK(swap_free_slot_count() == SWAP_SLOT_COUNT);
    CHECK(alloc_all_slots());
    CHECK(swap_free_slot_count() == 0);
    CHECK(swap_slot_alloc() == SWAP_INVALID_SLOT); // slot ke-1025
    CHECK(swap_free_slot_count() == 0);            // failed alloc must not underflow
}

static void test_free_and_reuse(void) {
    swap_init();
    alloc_all_slots();
    swap_slot_free(500);
    CHECK(swap_free_slot_count() == 1);
    CHECK(swap_slot_alloc() == 500);
    CHECK(swap_free_slot_count() == 0);
    CHECK(swap_slot_alloc() == SWAP_INVALID_SLOT);

    // double free & out of range free must not corrupt bitmap or counter
    swap_slot_free(500);
    swap_slot_free(500);
    swap_slot_free(SWAP_SLOT_COUNT);
    swap_slot_free(SWAP_INVALID_SLOT);
    CHECK(swap_free_slot_count() == 1);
    CHECK(swap_slot_alloc() == 500);
    CHECK(swap_slot_alloc() == SWAP_INVALID_SLOT);

    // swap_init resets counter too
    swap_init();
    CHECK(swap_free_slot_count() == SWAP_SLOT_COUNT);
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

/* ---------- clock victim selection ---------- */

#define K KERNEL_RESERVED_PAGE_FRAME_COUNT

static bool fake_access[PAGE_FRAME_MAX_COUNT];

// same contract as spare_if_accessed() in swap.c, access bit taken from fake_access
static bool spare_fake(void *ctx, uint32_t frame) {
    bool *access = ctx;
    if (!access[frame])
        return false;
    access[frame] = false;
    return true;
}

// frames K .. K+count-1 used and swappable, every access bit = referenced
static void clock_setup(uint32_t count, bool referenced) {
    memset(&page_manager_state, 0, sizeof(page_manager_state));
    memset(fake_access, 0, sizeof(fake_access));
    for (uint32_t f = K; f < K + count; f++) {
        page_manager_state.page_frame_map[f]  = true;
        page_manager_state.frame_swappable[f] = true;
        fake_access[f] = referenced;
    }
}

static void test_clock_all_unreferenced(void) {
    uint32_t hand = K;
    clock_setup(4, false);
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K);
    CHECK(hand == K + 1);
}

static void test_clock_all_referenced(void) {
    uint32_t hand = K;
    clock_setup(4, true);
    // round 1 clears every access bit, round 2 comes back to the first frame
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K);
    CHECK(hand == K + 1);
    CHECK(!fake_access[K] && !fake_access[K + 1] && !fake_access[K + 2] && !fake_access[K + 3]);
}

static void test_clock_skips_non_victims(void) {
    uint32_t hand = K;
    clock_setup(4, false);
    page_manager_state.frame_swappable[K] = false; // e.g. page table frame
    page_manager_state.page_frame_map[K + 1] = false; // free frame
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K + 2);
    CHECK(hand == K + 3);
}

static void test_clock_hand_continues(void) {
    uint32_t hand = K;
    clock_setup(4, false);
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K);
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K + 1);
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K + 2);
    // K+3 referenced: spared, hand wraps around past the end back to K
    fake_access[K + 3] = true;
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K);
    CHECK(!fake_access[K + 3]);
    CHECK(hand == K + 1);
}

static void test_clock_wraps_at_end(void) {
    uint32_t hand = PAGE_FRAME_MAX_COUNT - 1;
    clock_setup(1, false);
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == K);
}

static void test_clock_no_candidate(void) {
    uint32_t hand = K + 7;
    clock_setup(4, true);
    for (uint32_t f = K; f < K + 4; f++)
        page_manager_state.frame_swappable[f] = false;
    CHECK(swap_clock_pick_victim(&hand, spare_fake, fake_access) == PAGING_INVALID_FRAME);
    CHECK(hand == K + 7); // exactly two full rounds
}

int main(void) {
    test_alloc_until_full();
    test_free_and_reuse();
    test_roundtrip();
    test_ext2_area_untouched();
    test_invalid_slot_rejected();
    test_clock_all_unreferenced();
    test_clock_all_referenced();
    test_clock_skips_non_victims();
    test_clock_hand_continues();
    test_clock_wraps_at_end();
    test_clock_no_candidate();

    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}

#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"
#include "header/driver/disk.h"

/**
 * Swap area layout on storage.bin (8 MiB):
 * LBA 0    - 8191  : EXT2 (DISK_SPACE, 4 MiB)
 * LBA 8192 - 16383 : swap, slot s at LBA SWAP_START_LBA + s * BLOCKS_PER_PAGE
 */
#define SWAP_START_LBA    8192u                            // byte 4 MiB
#define SWAP_SLOT_COUNT   1024u                            // 4 MiB / 4 KiB
#define BLOCKS_PER_PAGE   (PAGE_FRAME_SIZE / BLOCK_SIZE)   // 8
#define SWAP_INVALID_SLOT 0xFFFFFFFFu
#define PTE_SWAPPED_BIT   0x1u                             // bit pertama dari field ignored (bit 9 PTE)

// Swap helpers below rely on PTE being exactly 32-bit as in Intel Manual 3a Figure 4-4
_Static_assert(sizeof(struct PageTableEntry) == 4, "PageTableEntry must be 4 bytes");

/**
 * Check whether a PTE describes a swapped out page
 *
 * @param pte Page table entry to check
 * @return    True when present_bit = 0 and swapped bit in ignored field is set
 */
static inline bool pte_is_swapped(volatile struct PageTableEntry *pte) {
    return !pte->flag.present_bit && (pte->ignored & PTE_SWAPPED_BIT);
}

/**
 * Get swap slot number stored in a swapped PTE
 *
 * @param pte Page table entry, must satisfy pte_is_swapped(pte)
 * @return    Swap slot number (stored in physical_page_base_address)
 */
static inline uint32_t pte_get_swap_slot(volatile struct PageTableEntry *pte) {
    return pte->physical_page_base_address;
}

/**
 * Mark PTE as swapped out: present_bit = 0, swapped bit = 1, base address = slot.
 * Caller must flush TLB for the respective virtual address afterward.
 *
 * @param pte  Page table entry to update
 * @param slot Swap slot holding the page content, must be < SWAP_SLOT_COUNT
 */
static inline void pte_set_swapped(volatile struct PageTableEntry *pte, uint32_t slot) {
    pte->flag.present_bit             = 0;
    pte->ignored                     |= PTE_SWAPPED_BIT;
    pte->physical_page_base_address   = slot;
}

/**
 * Clear swapped bit. Call this when mapping the page back to a frame,
 * because update_page_table_entry() does not touch the ignored field.
 *
 * @param pte Page table entry to update
 */
static inline void pte_clear_swapped(volatile struct PageTableEntry *pte) {
    pte->ignored &= ~PTE_SWAPPED_BIT;
}

/**
 * Initialize swap manager state, mark all swap slots as free.
 * Must be called once on kernel_setup() before any page can be swapped out.
 */
void swap_init(void);

/**
 * Allocate one free swap slot from slot bitmap
 *
 * @return Allocated slot number, or SWAP_INVALID_SLOT when swap area is full
 */
uint32_t swap_slot_alloc(void);

/**
 * Release swap slot back to slot bitmap
 *
 * @param slot Slot number returned by swap_slot_alloc(). Unallocated slot or slot >= SWAP_SLOT_COUNT is ignored
 */
void swap_slot_free(uint32_t slot);

/**
 * Get number of free swap slots, kept in sync by swap_slot_alloc() / swap_slot_free()
 *
 * @return Free slot count, 0 .. SWAP_SLOT_COUNT
 */
uint32_t swap_free_slot_count(void);

/**
 * Write one page (4 KiB, BLOCKS_PER_PAGE blocks) from memory into swap slot
 *
 * @param slot  Allocated swap slot. Unallocated or out of range slot is rejected, nothing is written
 * @param vaddr Page-aligned virtual address, page must be already mapped (present_bit = 1)
 */
void swap_write_page(uint32_t slot, const void *vaddr);

/**
 * Read one page (4 KiB, BLOCKS_PER_PAGE blocks) from swap slot into memory
 *
 * @param slot  Allocated swap slot. Unallocated or out of range slot is rejected, nothing is read
 * @param vaddr Page-aligned virtual address, page must be already mapped and writable
 */
void swap_read_page(uint32_t slot, void *vaddr);

/**
 * Evict one swappable page frame using Clock (second chance) with PTE access_bit.
 * Victim content is written to a new swap slot while still mapped, then the PTE
 * is marked swapped, TLB flushed, and the frame returned to page_manager_state.
 *
 * @param page_dir Current page directory (single address space)
 * @return         True when one frame is freed, false when no swappable frame or no free slot
 */
bool swap_out_one(struct PageDirectory *page_dir);

/**
 * Bring swapped page containing vaddr back to memory. Get a free frame
 * (evict with swap_out_one() if none), map it, read slot content, then free the slot.
 *
 * @param page_dir Current page directory (single address space)
 * @param vaddr    Any virtual address inside the page, its PTE must satisfy pte_is_swapped()
 * @return         True when page is back in memory, false otherwise
 */
bool swap_in(struct PageDirectory *page_dir, void *vaddr);

/**
 * Page fault (int 14) handler, called from main_interrupt_handler().
 * error_code bit 0 = 1 (protection violation) -> panic.
 * PTE swapped -> swap_in(), then return so faulting instruction is retried.
 * Otherwise -> panic with message containing fault_addr.
 *
 * @param fault_addr Faulting virtual address, value of CR2
 * @param error_code Error code pushed by CPU for page fault
 */
void page_fault_handler(uint32_t fault_addr, uint32_t error_code);

#endif

#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

#define GDT_MAX_ENTRY_COUNT 32

#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x8
#define GDT_KERNEL_DATA_SEGMENT_SELECTOR 0x10
#define GDT_USER_CODE_SEGMENT_SELECTOR   0x18
#define GDT_USER_DATA_SEGMENT_SELECTOR   0x20
#define GDT_TSS_SELECTOR                 0x28

extern struct GDTR _gdt_gdtr;

struct SegmentDescriptor {
    uint16_t segment_low;
    uint16_t base_low;

    uint8_t base_mid;
    uint8_t type_bit   : 4;
    uint8_t non_system : 1;
    uint8_t privilege  : 2; // DPL (0 = Ring 0 kernel, 3 = Ring 3 user)
    uint8_t valid_bit  : 1;

    uint8_t segment_high : 4;
    uint8_t avl          : 1;
    uint8_t long_mode    : 1;
    uint8_t opr_32_bit   : 1;
    uint8_t granularity  : 1;
    uint8_t base_high;
} __attribute__((packed));

struct GlobalDescriptorTable {
    struct SegmentDescriptor table[GDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

struct GDTR {
    uint16_t                     size;
    struct GlobalDescriptorTable *address;
} __attribute__((packed));

void gdt_install_tss(void);

#endif

#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"

struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {0},
        {
            .segment_low  = 0xFFFF,
            .segment_high = 0xF,
            .base_low     = 0,
            .base_mid     = 0,
            .base_high    = 0,
            .type_bit     = 0xA,
            .non_system   = 1,
            .privilege    = 0,
            .valid_bit    = 1,
            .long_mode    = 0,
            .opr_32_bit   = 1,
            .granularity  = 1,
        },
        {
            .segment_low  = 0xFFFF,
            .segment_high = 0xF,
            .base_low     = 0,
            .base_mid     = 0,
            .base_high    = 0,
            .type_bit     = 0x2,
            .non_system   = 1,
            .privilege    = 0,
            .valid_bit    = 1,
            .long_mode    = 0,
            .opr_32_bit   = 1,
            .granularity  = 1,
        },
        {
            // Ring 3 user code segment (DPL 3, selector 0x18)
            .segment_low  = 0xFFFF,
            .segment_high = 0xF,
            .base_low     = 0,
            .base_mid     = 0,
            .base_high    = 0,
            .type_bit     = 0xA,
            .non_system   = 1,
            .privilege    = 3,
            .valid_bit    = 1,
            .long_mode    = 0,
            .opr_32_bit   = 1,
            .granularity  = 1,
        },
        {
            // Ring 3 user data segment (DPL 3, selector 0x20)
            .segment_low  = 0xFFFF,
            .segment_high = 0xF,
            .base_low     = 0,
            .base_mid     = 0,
            .base_high    = 0,
            .type_bit     = 0x2,
            .non_system   = 1,
            .privilege    = 3,
            .valid_bit    = 1,
            .long_mode    = 0,
            .opr_32_bit   = 1,
            .granularity  = 1,
        },
        {
            .segment_high = (sizeof(struct TSSEntry) & (0xF << 16)) >> 16,
            .segment_low  = sizeof(struct TSSEntry),
            .base_high    = 0,
            .base_mid     = 0,
            .base_low     = 0,
            .non_system   = 0,
            .type_bit     = 0x9,
            .privilege    = 0,
            .valid_bit    = 1,
            .opr_32_bit   = 1,
            .long_mode    = 0,
            .granularity  = 0,
        },
        {0}
    }
};

struct GDTR _gdt_gdtr = {
    .size    = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table,
};

void gdt_install_tss(void) {
    // Split 32-bit _interrupt_tss_entry pointer across TSS descriptor base fields
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_high = (base & (0xFFu << 24)) >> 24;
    global_descriptor_table.table[5].base_mid  = (base & (0xFFu << 16)) >> 16;
    global_descriptor_table.table[5].base_low  = base & 0xFFFF;
}

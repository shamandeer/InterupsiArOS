#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"

/**
 * global_descriptor_table, predefined GDT.
 * Initial SegmentDescriptor already set properly according to Intel Manual & OSDev.
 * Table entry : [{Null Descriptor}, {Kernel Code}, {Kernel Data (variable, etc)}, ...].
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {
            // Null descriptor
        },
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
            // User code descriptor (DPL 3, selector 0x18)
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
            // User data descriptor (DPL 3, selector 0x20)
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
            // TSS descriptor (DPL 0, selector 0x28), base is filled by gdt_install_tss()
            .segment_high = (sizeof(struct TSSEntry) & (0xF << 16)) >> 16,
            .segment_low  = sizeof(struct TSSEntry),
            .base_high    = 0,
            .base_mid     = 0,
            .base_low     = 0,
            .non_system   = 0, // S bit
            .type_bit     = 0x9,
            .privilege    = 0, // DPL
            .valid_bit    = 1, // P bit
            .opr_32_bit   = 1, // D/B bit
            .long_mode    = 0, // L bit
            .granularity  = 0, // G bit
        },
        {0}
    }
};

/**
 * _gdt_gdtr, predefined system GDTR. 
 * GDT pointed by this variable is already set to point global_descriptor_table above.
 * From: https://wiki.osdev.org/Global_Descriptor_Table, GDTR.size is GDT size minus 1.
 */
struct GDTR _gdt_gdtr = {

    .size = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table,
    // TODO : Implement, this GDTR will point to global_descriptor_table. (done)
    //        Use sizeof operator
};

void gdt_install_tss(void) {
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_high = (base & (0xFFu << 24)) >> 24;
    global_descriptor_table.table[5].base_mid  = (base & (0xFFu << 16)) >> 16;
    global_descriptor_table.table[5].base_low  = base & 0xFFFF;
}
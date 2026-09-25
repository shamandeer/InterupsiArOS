#include "header/cpu/idt.h"
#include "header/cpu/gdt.h"

/**
 * interrupt_descriptor_table, global IDT.
 * All IDTGate initially empty, will be filled by initialize_idt().
 */
struct InterruptDescriptorTable interrupt_descriptor_table;

/**
 * _idt_idtr, predefined system IDTR.
 * IDT pointed by this variable is already set to point interrupt_descriptor_table above.
 * Same as GDTR, IDTR.size is IDT size minus 1.
 */
struct IDTR _idt_idtr = {
    .size    = sizeof(interrupt_descriptor_table) - 1,
    .address = &interrupt_descriptor_table,
};

void initialize_idt(void) {
    // TODO : Implement (done)
    for (int i = 0; i < ISR_STUB_TABLE_LIMIT; i++) {
        set_interrupt_gate(i, isr_stub_table[i], GDT_KERNEL_CODE_SEGMENT_SELECTOR, 0);
    }
    __asm__ volatile("lidt %0" : : "m"(_idt_idtr));
    __asm__ volatile("sti");
}

void set_interrupt_gate(
    uint8_t  int_vector,
    void     *handler_address,
    uint16_t gdt_seg_selector,
    uint8_t  privilege
) {
    struct IDTGate *idt_int_gate = &interrupt_descriptor_table.table[int_vector];
    // TODO : Set handler offset, privilege & segment (done)
    // 32-bit handler address split into 2 x 16-bit (offset_low & offset_high)
    idt_int_gate->offset_low  = (uint32_t) handler_address & 0xFFFF;
    idt_int_gate->offset_high = ((uint32_t) handler_address >> 16) & 0xFFFF;
    idt_int_gate->segment     = gdt_seg_selector;
    idt_int_gate->privilege   = privilege;
    idt_int_gate->_reserved   = 0;

    // Target system 32-bit and flag this as valid interrupt gate
    idt_int_gate->_r_bit_1    = INTERRUPT_GATE_R_BIT_1;
    idt_int_gate->_r_bit_2    = INTERRUPT_GATE_R_BIT_2;
    idt_int_gate->_r_bit_3    = INTERRUPT_GATE_R_BIT_3;
    idt_int_gate->gate_32     = 1;
    idt_int_gate->valid_bit   = 1;
}

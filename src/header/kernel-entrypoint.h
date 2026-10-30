#ifndef _KERNEL_ENTRYPOINT
#define _KERNEL_ENTRYPOINT

#include "header/cpu/gdt.h"

/**
 * Load GDT from gdtr and complete init for protected mode. This procedure implemented in asm.
 * Note: This procedure will not activate CPU interrupt flag
 * 
 * @param gdtr Pointer to already defined & initialized GDTR
 * @warning Invalid address / definition of GDT will cause bootloop after calling this procedure.
 */
extern void load_gdt(struct GDTR *gdtr);

/**
 * Load TSS selector (GDT_TSS_SELECTOR) into task register. This procedure implemented in asm.
 * Call gdt_install_tss() first so the TSS descriptor has a valid base address.
 */
extern void set_tss_register(void);

/**
 * Execute user program from kernel, one way jump. This function is defined in asm source code.
 *
 * @param virtual_addr Pointer into user program that already in memory
 * @warning            Never returns. User stack is placed at the end of the 4 MiB page starting at virtual_addr
 */
extern void kernel_execute_user_program(void *virtual_addr);

#endif
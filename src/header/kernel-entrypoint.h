#ifndef _KERNEL_ENTRYPOINT
#define _KERNEL_ENTRYPOINT

#include "header/cpu/gdt.h"

extern void load_gdt(struct GDTR *gdtr);
extern void set_tss_register(void);
// Enters Ring 3 via iret with user esp placed at virtual_addr + 0x400000 - 4
extern void kernel_execute_user_program(void *virtual_addr);

#endif

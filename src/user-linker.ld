ENTRY(_start)

SECTIONS {
    . = 0x00000000; /* Assuming OS will load this program at virtual address 0 */

    /* Put the entrypoint (crt0.o) in the front of executable */
    .text ALIGN(4): {
        [a-z\/]*crt0.o(.text)
        *(.text)
    }

    .data ALIGN(4): {
        *(.data)
    }

    .bss ALIGN(4): {
        *(COMMON)
        *(.bss)
    }

    .rodata ALIGN(4): {
        *(.rodata*)
    }

    _linker_user_program_end = .;
    ASSERT ((_linker_user_program_end <= 1 * 1024 * 1024), "Error: User program linking result is more than 1 MiB")
}

ENTRY(_start)

SECTIONS {
    . = 0x00000000;

    /* Keep crt0 _start at offset 0x0 for flat binary Ring 3 entry */
    .text ALIGN(4): {
        [a-z\/]*crt0.o(.text)
        *(.text)
    }

    .rodata ALIGN(0x1000): {
        *(.rodata*)
    }

    .data ALIGN(0x1000): {
        *(.data)
    }

    .bss ALIGN(0x1000): {
        *(COMMON)
        *(.bss)
    }

    _linker_user_program_end = .;
    ASSERT ((_linker_user_program_end <= 1 * 1024 * 1024), "Error: User program linking result is more than 1 MiB")
}

ENTRY(_start)

SECTIONS {
    . = 0x00000000;

    .text ALIGN(4): {
        [a-z\/]*crt0.o(.text)
        *(.text*)
    }

    .rodata ALIGN(4): {
        *(.rodata*)
    }

    .data ALIGN(4): {
        *(.data*)
    }

    .bss ALIGN(16): {
        *(COMMON)
        *(.bss*)
    }

    _linker_user_program_end = .;
    ASSERT ((_linker_user_program_end <= 2 * 1024 * 1024), "Error: Doom lebih dari 2 MiB")

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

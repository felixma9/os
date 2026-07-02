ENTRY(_start)

SECTIONS
{
    /* Matches MEMORY_KERNEL_ADDR in src/bootloader/stage2/memdefs.h */
    . = 0x30000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
        *(COMMON)
    }
}

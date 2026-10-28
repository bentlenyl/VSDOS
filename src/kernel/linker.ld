ENTRY(kernel_entry)

SECTIONS
{
    . = 0x00010000;

    .text :
    {
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;
    __kernel_start = 0x00010000;
    ASSERT(__kernel_end < 0x00090000, "Kernel overlaps the boot stack")

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}

/*
 * (c) 2007 Freescale Semiconductor
 *
 *
 *
 */

ENTRY(_start)

OUTPUT_ARCH(powerpc:common)

SECTIONS
{
    . = 0x20000000;

    .text : {
        *(.text)
    }

    . = ALIGN(4096);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(4096);
    .data : {
        *(.data)
        *(.sdata)
    }

    bss_start = .;
    .bss : {
        *(.sbss)
        *(.bss)
    }
    bss_end = .;
    _end = .;

}

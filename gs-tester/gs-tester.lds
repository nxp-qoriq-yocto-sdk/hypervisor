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
    . = 0x0;

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

    .bss : {
        *(.sbss)
        *(.bss)
    }

}



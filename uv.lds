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
	. = 0x40000000;

	.text : {
		*(.text)
	}

	.rodata : {
		*(.rodata)
		*(.rodata.*)
	}

	. = ALIGN(4096);
	.data : {
		*(.data)
		*(.sdata)
		
		. = ALIGN(8);
		extable_begin = .;
		*(.extable)
		extable_end = .;
	}

	. = ALIGN(8);
	bss_start = .;
	.bss : {
		*(.sbss)
		*(.bss)
	}
	bss_end = .;
	_end = .;
}

/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

ENTRY(_start)

OUTPUT_ARCH(powerpc:common64)

SECTIONS
{
	. = 0x00400000;

	.text : {
		*(.text)
	}

	.notes : {
		*(.note.*)
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

		. = ALIGN(8);
		shellcmd_begin = .;
		*(.shellcmd)
		shellcmd_end = .;

		. = ALIGN(8);
		bootparam_begin = .;
		*(.bootparam)
		bootparam_end = .;

		. = ALIGN(8);
		hvdbgstub_begin = .;
		*(.hvdbgstub)
		hvdbgstub_end = .;

		. = ALIGN(8);
		virtual_driver_begin = .;
		*(.virtual.drivers)
		virtual_driver_end = .;

		. = ALIGN(8);
		driver_begin = .;
		*(.libos.drivers)
		driver_end = .;
	}

	. = ALIGN(8);
	toc_start = .;
	.got : {
		*(.got)
		*(.toc)
	}

	. = ALIGN(8);
	bss_start = .;
	.bss : {
		*(.sbss)
		*(.bss)
		*(.stack)
	}
	bss_end = .;
	_end = .;
}

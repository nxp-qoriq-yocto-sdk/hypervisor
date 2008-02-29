/** @file
 * ELF image parser
 *
 * This file contains functions that will parse and ELF image and extract
 * the executable binary from it.
 */
/* Copyright (C) 2008 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * This software is provided by the author "as is" and any express or
 * implied warranties, including, but not limited to, the implied warranties
 * of merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the author be liable for any direct, indirect,
 * incidental, special, exemplary, or consequential damages (including, but
 * not limited to, procurement of substitute goods or services; loss of use,
 * data, or profits; or business interruption) however caused and on any
 * theory of liability, whether in contract, strict liability, or tort
 * (including negligence or otherwise) arising in any way out of the use of
 * this software, even if advised of the possibility of such damage.
 */

#include <stdint.h>
#include <string.h>
#include <libos/console.h>
#include <paging.h>
#include <percpu.h>
#include <libos/errors.h>

#define PT_NULL      0
#define PT_LOAD      1

#define EI_CLASS     4  /* File class */
#define ELFCLASS32   1  /* 32-bit objects */
#define EI_DATA      5  /* Data encoding */
#define ELFDATA2MSB  2  /* 2's complement, big endian */

/*
 * ELF header
 */
struct elf_header {
	uint8_t ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint32_t entry;
	uint32_t phoff;
	uint32_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
};

/*
 * ELF program header table
 */
struct program_header {
	uint32_t type;
	uint32_t offset;
	uint32_t vaddr;
	uint32_t paddr;
	uint32_t filesz;
	uint32_t memsz;
	uint32_t flags;
	uint32_t align;
};

/**
 * Return non-zero if image is an ELF binary
 */
int is_elf(void *image)
{
	return strncmp(image, "\177ELF", 4) == 0;
}

/**
 * Parse an ELF image and load the segments into guest memory
 *
 * @image: Pointer to the ELF image
 * @length: length of the ELF image, or -1 to ignore length checking
 * @target: guest physical address of the destination
 *
 * Per ePAPR spec, the destination address encoded in the ELF file is
 * ignored.
 */
int load_elf(guest_t *guest, void *image, unsigned long length, physaddr_t target)
{
	struct elf_header *hdr = (struct elf_header *) image;
	struct program_header *phdr = (struct program_header *) (image + hdr->phoff);
	unsigned long base = -1;
	unsigned int i;

	if (strncmp(image, "\177ELF", 4)) {
		printf("guest %s: invalid ELF magic\n", guest->name);
		return ERR_BADIMAGE;
	}

	if (length < sizeof(struct elf_header)) {
		printf("guest %s: truncated ELF image\n", guest->name);
		return ERR_BADIMAGE;
	}

	if (length < hdr->phoff + (hdr->phnum * sizeof(struct program_header))) {
		printf("guest %s: truncated ELF image\n", guest->name);
		return ERR_BADIMAGE;
	}

	/* We only support 32-bit for now */
	if (hdr->ident[EI_CLASS] != ELFCLASS32) {
		printf("guest %s: only 32-bit ELF images are supported\n", guest->name);
		return ERR_BADIMAGE;
	}

	/* ePAPR only supports big-endian ELF images */
	if (hdr->ident[EI_DATA] != ELFDATA2MSB) {
		printf("guest %s: only big-endian ELF images are supported\n", guest->name);
		return ERR_BADIMAGE;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base address is the smallest vaddr of all PT_LOAD
	 * segments.
	 */

	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].offset + phdr[i].filesz > length) {
			printf("guest %s: truncated ELF image\n", guest->name);
			return ERR_BADIMAGE;
		}
		if (phdr[i].filesz > phdr[i].memsz) {
			printf("guest %s: invalid ELF program header file size\n", guest->name);
			return ERR_BADIMAGE;
		}
		if ((phdr[i].type == PT_LOAD) && (phdr[i].vaddr < base))
			base = phdr[i].vaddr;
	}

	/* Copy each PT_LOAD segment to memory */

	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].type == PT_LOAD) {
			copy_to_gphys(guest->gphys,
				target + (phdr[i].vaddr - base),
				image + phdr[i].offset, phdr[i].filesz);
			zero_to_gphys(guest->gphys,
				target + (phdr[i].vaddr - base) + phdr[i].filesz,
				phdr[i].memsz - phdr[i].filesz);
		}
	}

	return 0;
}

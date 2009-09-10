/** @file
 * ELF image parser
 *
 * This file contains functions that will parse and ELF image and extract
 * the executable binary from it.
 */
/* Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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
#include <elf.h>
#include <limits.h>

/* Elf file types that we support */
#define ET_EXEC   2
#define ET_DYN    3

/* Program header types that we support */
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
 * Parse an ELF image and load the segments into guest memory
 *
 * @image: Pointer to the ELF image
 * @length: length of the ELF image, or -1 to ignore length checking
 * @target: guest physical address of the destination
 * @entry: guest phys address of entry point, if not NULL
 *
 * Note: the destination address encoded in the ELF file is ignored.  This
 * will change when we support variable address (ET_DYN) ELF images.
 *
 * 'plowest' contains the starting physical address of the segment that has
 * the lowest starting physical address.
 */
int load_elf(guest_t *guest, phys_addr_t image, size_t *length,
             phys_addr_t target, register_t *entryp)
{
	struct elf_header hdr;
	struct program_header phdr;
	unsigned long plowest = ULONG_MAX;
	unsigned long entry = ULONG_MAX;
	unsigned int i, ret;

	if (copy_from_phys(&hdr, image, sizeof(hdr)) != sizeof(hdr))
		return ERR_UNHANDLED;

	if (strncmp((char *)hdr.ident, "\177ELF", 4))
		return ERR_UNHANDLED;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "loading ELF image from %#llx\n", image);

	/* We only support 32-bit for now */
	if (hdr.ident[EI_CLASS] != ELFCLASS32) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_elf: only 32-bit ELF images are supported\n");
		return ERR_BADIMAGE;
	}

	/* ePAPR only supports big-endian ELF images */
	if (hdr.ident[EI_DATA] != ELFDATA2MSB) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_elf: only big-endian ELF images are supported\n");
		return ERR_BADIMAGE;
	}

	/* We only support ET_EXEC images for now */
	if (hdr.type != ET_EXEC) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "load_elf: only fixed address ELF images are supported\n");
		return ERR_BADIMAGE;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base load address is the smallest paddr of all
	 * PT_LOAD segments.  Note that since we don't support ET_DYN
	 */

	for (i = 0; i < hdr.phnum; i++) {
		phys_addr_t phdr_phys = image + hdr.phoff + i * hdr.phentsize;
	
		if (copy_from_phys(&phdr, phdr_phys, sizeof(phdr)) != sizeof(phdr)) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: cannot read program header\n");
			return ERR_BADIMAGE;
		}

		if (phdr.offset + phdr.filesz > *length) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: truncated ELF image\n");
			return ERR_BADIMAGE;
		}

		if (phdr.filesz > phdr.memsz) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: invalid ELF segment size\n");
			return ERR_BADIMAGE;
		}

		if (phdr.type == PT_LOAD && phdr.paddr < plowest)
			plowest = phdr.paddr;
	}

	if (plowest == ULONG_MAX) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "load_elf: no PT_LOAD program headers in ELF image\n");
		return ERR_BADIMAGE;
	}

	/* If the image load address is set as -1 in the partition config
	 * (config device tree), the load address is set as the  lowest
	 *  segment physical address.
	 */
	if (~target == 0)
		target = plowest;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "loading ELF image to %#llx\n", target);

	/* Copy each PT_LOAD segment to memory */

	for (i = 0; i < hdr.phnum; i++) {
		phys_addr_t phdr_phys = image + hdr.phoff + i * hdr.phentsize;
	
		if (copy_from_phys(&phdr, phdr_phys, sizeof(phdr)) != sizeof(phdr)) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: cannot read program header %d\n", i);
			return ERR_BADIMAGE;
		}

		if (phdr.type == PT_LOAD) {
			phys_addr_t seg_target = target + phdr.paddr - plowest;

			if (phdr.vaddr <= hdr.entry &&
			    hdr.entry <= phdr.vaddr + phdr.memsz - 1) {
				/* This segment contains the entry point.
				 * Translate it into a physical address.
				 *
				 * If we're overriding the physical address,
				 * translate the entry point to match.  We
				 * assume the virtual-to-physical offset is
				 * the same for all segments in this case.
				 *
				 * If we're not recolating, then seg_target =
				 * phdr.paddr and this is a no-op.
				 */
				if (entry != ULONG_MAX) {
					printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
					         "%s: ELF entry point %#x is in "
					         "multiple segments.\n",
					         __func__, hdr.entry);

					/* It's fatal if we actually need 
					 * the entry.
					 */
					if (entryp)
						return ERR_BADIMAGE;
				}

				entry = hdr.entry - phdr.vaddr + seg_target;
			}

			ret = copy_phys_to_gphys(guest->gphys, seg_target,
			                         image + phdr.offset,
			                         phdr.filesz, 1);
			if (ret != phdr.filesz) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "load_elf: cannot copy segment %d\n", i);
				return ERR_BADADDR;
			}

			ret = zero_to_gphys(guest->gphys, seg_target + phdr.filesz,
			                    phdr.memsz - phdr.filesz, 1);
			if (ret != phdr.memsz - phdr.filesz) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "load_elf: cannot zero segment %d\n", i);
				return ERR_BADADDR;
			}
		}
	}

	if (entryp) {
		if (entry == ULONG_MAX) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: ELF image has invalid entry address %#x\n",
			         __func__, hdr.entry);
			return ERR_BADIMAGE;
		}

		*entryp = entry;
		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
		         "ELF physical entry point %#lx\n", entry);
	}

	return 0;
}

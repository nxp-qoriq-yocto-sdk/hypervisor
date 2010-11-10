/** @file
 * ELF image parser
 *
 * This file contains functions that will parse and ELF image and extract
 * the executable binary from it.
 */
/* Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
#define EI_NIDENT   16  /* Size of ident field */
#define ELFCLASS32   1  /* 32-bit objects */
#define ELFCLASS64   2  /* 64-bit objects */
#define EI_DATA      5  /* Data encoding */
#define ELFDATA2MSB  2  /* 2's complement, big endian */

#define SELECT(a,b) ((hdr.h32.ident[EI_CLASS] == ELFCLASS32)? \
		     (a).h32.b : (a).h64.b)

/*
 * ELF header
 */
struct elf_header_32 {
	uint8_t ident[EI_NIDENT];
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

struct elf_header_64 {
	uint8_t ident[EI_NIDENT]; /* ELF identification */
	uint16_t type;		/* Object file type */
	uint16_t machine;	/* Machine type */
	uint32_t version;	/* Object file version */
	uint64_t entry;		/* Entry point address */
	uint64_t phoff;		/* Program header offset */
	uint64_t shoff;		/* Section header offset */
	uint32_t flags;		/* Processor-specific flags */
	uint16_t ehsize;	/* ELF header size */
	uint16_t phentsize;	/* Size of program header entry */
	uint16_t phnum;		/* Number of program header entries */
	uint16_t shentsize;	/* Size of section header entry */
	uint16_t shnum;		/* Number of section header entries */
	uint16_t shstrndx;	/* Section name string table index */
};

/*
 * Internal representation of the ELF header
 */
union elf_header_int {
	struct elf_header_32 h32;
	struct elf_header_64 h64;
};

struct program_header_64 {
	uint32_t type;		/* Type of segment */
	uint32_t flags;		/* Segment attributes */
	uint64_t offset;	/* Offset in file */
	uint64_t vaddr;		/* Virtual address in memory */
	uint64_t paddr;		/* Reserved */
	uint64_t filesz;	/* Size of segment in file */
	uint64_t memsz;		/* Size of segment in memory */
	uint64_t align;		/* Alignment of segment */
};

/*
 * ELF program header table
 */
struct program_header_32 {
	uint32_t type;
	uint32_t offset;
	uint32_t vaddr;
	uint32_t paddr;
	uint32_t filesz;
	uint32_t memsz;
	uint32_t flags;
	uint32_t align;
};

union program_header_int {
	struct program_header_32 h32;
	struct program_header_64 h64;
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
	union elf_header_int hdr;
	union program_header_int phdr;
	uintptr_t plowest = ULONG_MAX;
	uintptr_t entry_addr = ULONG_MAX;
	unsigned int i, ret;

	if (copy_from_phys(hdr.h32.ident, image, EI_NIDENT) != EI_NIDENT)
		return ERR_UNHANDLED;

	if (strncmp((const char *)hdr.h32.ident, "\177ELF", 4))
		return ERR_UNHANDLED;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "loading ELF image from %#llx\n", image);

#ifndef CONFIG_LIBOS_64BIT
	if (hdr.h32.ident[EI_CLASS] != ELFCLASS32) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_elf: only 32-bit ELF images are supported\n");
		return ERR_BADIMAGE;
	}
#else
	if (hdr.h32.ident[EI_CLASS] != ELFCLASS32 &&
	    hdr.h32.ident[EI_CLASS] != ELFCLASS64) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_elf: unsupported ELF class (!= 32 or 64 bit)\n");
		return ERR_BADIMAGE;
	}
#endif

	/* ePAPR only supports big-endian ELF images */
	if (hdr.h32.ident[EI_DATA] != ELFDATA2MSB) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_elf: only big-endian ELF images are supported\n");
		return ERR_BADIMAGE;
	}

	unsigned int size = sizeof(struct elf_header_32);
#ifdef CONFIG_LIBOS_64BIT
	if (hdr.h32.ident[EI_CLASS] == ELFCLASS64)
		size = sizeof(struct elf_header_64);
#endif
	if (copy_from_phys(&hdr, image, size) != size) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "load_elf: image read error\n");
		return ERR_BADIMAGE;
	}

	/* We only support ET_EXEC images for now */
	if (hdr.h32.type != ET_EXEC) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "load_elf: only fixed address ELF images are supported\n");
		return ERR_BADIMAGE;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base load address is the smallest paddr of all
	 * PT_LOAD segments.  Note that since we don't support ET_DYN
	 */

	uint16_t phnum = SELECT(hdr, phnum);
	uint64_t phoff = SELECT(hdr, phoff);
	uint64_t entry = SELECT(hdr, entry);
	uint16_t phentsize = SELECT(hdr, phentsize);
	size_t phdr_len = (hdr.h32.ident[EI_CLASS] == ELFCLASS32)?
			  sizeof(struct program_header_32) :
			  sizeof(struct program_header_64);
	for (i = 0; i < phnum; i++) {
		phys_addr_t phdr_phys = image + phoff + i * phentsize;
	
		if (copy_from_phys(&phdr, phdr_phys, phdr_len) != phdr_len) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: cannot read program header\n");
			return ERR_BADIMAGE;
		}

		uint64_t offset = SELECT(phdr, offset);
		uint64_t filesz = SELECT(phdr, filesz);
		uint64_t memsz  = SELECT(phdr, memsz);
		uint64_t paddr  = SELECT(phdr, paddr);

		if (offset + filesz > *length) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: truncated ELF image\n");
			return ERR_BADIMAGE;
		}

		if (filesz > memsz) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: invalid ELF segment size\n");
			return ERR_BADIMAGE;
		}

		if (phdr.h32.type == PT_LOAD && paddr < plowest)
			plowest = paddr;
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

	for (i = 0; i < phnum; i++) {
		phys_addr_t phdr_phys = image + phoff + i * phentsize;
	
		if (copy_from_phys(&phdr, phdr_phys, phdr_len) != phdr_len) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_elf: cannot read program header %d\n", i);
			return ERR_BADIMAGE;
		}

		uint64_t paddr  = SELECT(phdr, paddr);
		uint64_t vaddr  = SELECT(phdr, vaddr);
		uint64_t offset = SELECT(phdr, offset);
		uint64_t filesz = SELECT(phdr, filesz);
		uint64_t memsz  = SELECT(phdr, memsz);

		if (phdr.h32.type == PT_LOAD) {
			phys_addr_t seg_target = target + paddr - plowest;

			if (vaddr <= entry &&
			    entry <= vaddr + memsz - 1) {
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
				if (entry_addr != ULONG_MAX) {
					printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
					         "%s: ELF entry point %#llx is in "
					         "multiple segments.\n",
					         __func__, entry);

					/* It's fatal if we actually need 
					 * the entry.
					 */
					if (entryp)
						return ERR_BADIMAGE;
				}

				entry_addr = entry - vaddr + seg_target;
			}

			ret = copy_phys_to_gphys(guest->gphys, seg_target,
			                         image + offset,
			                         filesz, 1);
			if (ret != filesz) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "load_elf: cannot copy segment %d\n", i);
				return ERR_BADADDR;
			}

			ret = zero_to_gphys(guest->gphys, seg_target + filesz,
			                    memsz - filesz, 1);
			if (ret != memsz - filesz) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "load_elf: cannot zero segment %d\n", i);
				return ERR_BADADDR;
			}
		}
	}

	if (entryp) {
		if (entry_addr == ULONG_MAX) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: ELF image has invalid entry address %#llx\n",
			         __func__, entry);
			return ERR_BADIMAGE;
		}

		*entryp = entry_addr;
		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
		         "ELF physical entry point %#lx\n", entry_addr);
	}

	return 0;
}

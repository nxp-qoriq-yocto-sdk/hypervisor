/*
 * Copyright (C) 2008-2012 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
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

/**
 * Sample Linux partition management utility
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <asm/ioctl.h>
#include <limits.h>
#include <inttypes.h>

#include "parse_dt.h"

/*
 * fsl_hypervisor.h is copied from include/linux in the Linux kernel source
 * tree.  So every time that file is updated, you need to copy it here.
 */
#include "fsl_hypervisor.h"

struct strlist {
	const char *str;
	struct strlist *next;
};

#define PROP_MAX 4096

struct parameters {
	int h;
	const char *f;
	const char *n, *p;

	char prop[4096];
	int proplen;

	unsigned long a;
	unsigned long e;
	unsigned int h_specified:1;
	unsigned int a_specified:1;
	unsigned int e_specified:1;
	unsigned int l_specified:1;
	unsigned int r_specified:1;
};

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

#define SELECT_CLASS(a,b) ((hdr->h.ident[EI_CLASS] == ELFCLASS32)? \
			(a)->h32.b : (a)->h64.b)

#define ELF_HEADER(b) SELECT_CLASS(hdr,b)

#define PROGRAM_HEADER(b) SELECT_CLASS(phdr,b)

static int verbose = 0;
static int quiet = 0;

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

struct elf_header_generic {
	uint8_t ident[EI_NIDENT]; /* ELF identification */
	uint16_t type;		/* Object file type */
};

/*
 * Internal representation of the ELF header
 */
union elf_header_int {
	struct elf_header_generic h;
	struct elf_header_32 h32;
	struct elf_header_64 h64;
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

union program_header_int {
	uint32_t type;
	struct program_header_32 h32;
	struct program_header_64 h64;
};

/*
 *  uImage header
 */
struct image_header {
	uint32_t magic;   /* Image Header Magic Number */
	uint32_t hcrc;    /* Image Header CRC Checksum */
	uint32_t time;    /* Image Creation Timestamp */
	uint32_t size;    /* Image Data Size */
	uint32_t load;    /* Data  Load  Address */
	uint32_t ep;      /* Entry Point Address */
	uint32_t dcrc;    /* Image Data CRC Checksum */
	uint8_t os;       /* Operating System */
	uint8_t arch;     /* CPU architecture */
	uint8_t type;     /* Image Type */
	uint8_t comp;     /* Compression Type */
	uint8_t name[32]; /* Image Name */
	uint8_t data[0];
};

#define UIMAGE_SIGNATURE 0x27051956

/* Support functions */

/**
 * Display command line parameters
 */
static void usage(void)
{
	printf("Freescale Hypervisor Partition Manager %s\n", VERSION);
	printf("Usage:\n");

	printf("   partman status\n");

	printf("   partman load -h <handle> -f <file> [-a <address>] [-r]\n"
	       "      Load an image.\n"
	       "      Use -r to tell that the image is a filesystem.\n");

	printf("   partman start -h <handle> [-l] [-f <file>] [-e <addr>] [-a <addr>]\n"
	       "     Start a partition.\n"
	       "     Optionally load a file with -f, or use -l to tell\n"
	       "     the hypervisor to reload the images itself.\n");

	printf("   partman stop -h <handle>\n");
	printf("      Stop a partition.\n");

	printf("   partman restart -h <handle>\n");
	printf("      Restart a partition.\n");

	printf("   partman doorbell -f <file>\n");
	printf("      Monitor doorbells.\n");

	printf("   partman doorbell -h <handle>\n");
	printf("      Ring a doorbell.\n");

	printf("   partman setprop -h <handle> -p <path> -n <propname> [-t <data> [-t <data>]]\n");
	printf("      Set a guest device tree property.\n");

	printf("   partman getprop -h <handle> -p <path> -n <propname>\n"
	       "      Get a guest device tree property.\n");

	printf("   -v for verbose output\n");
	printf("   -q for quiet mode (errors reported via return status only)\n");
}

/* epapr error codes */
#define EV_EPERM		1	/* Operation not permitted */
#define EV_ENOENT		2	/*  Entry Not Found */
#define EV_EIO			3	/* I/O error occured */
#define EV_EAGAIN		4	/* The operation had insufficient
					 * resources to complete and should be
					 * retried
					 */
#define EV_ENOMEM		5	/* There was insufficient memory to
					 * complete the operation */
#define EV_EFAULT		6	/* Bad guest address */
#define EV_ENODEV		7	/* No such device */
#define EV_EINVAL		8	/* An argument supplied to the hcall
					   was out of range or invalid */
#define EV_INTERNAL		9	/* An internal error occured */
#define EV_CONFIG		10	/* A configuration error was detected */
#define EV_INVALID_STATE	11	/* The object is in an invalid state */
#define EV_UNIMPLEMENTED	12	/* Unimplemented hypercall */
#define EV_BUFFER_OVERFLOW	13	/* Caller-supplied buffer too small */

/**
 * Call the hypervisor management driver.
 *
 * Return code:
 * 0: success
 * <0: ioctl error condition
 * >0: Hypervisor error condition
 */
static int hv(unsigned int cmd, void *p)
{
	int ret = 0;

	int f = open("/dev/fsl-hv", O_RDWR | O_SYNC);
	if (f == -1) {
		ret = errno;
		if (!quiet)
			perror(__func__);
		return ret;
	}

	if (ioctl(f, cmd, p) == -1) {
		ret = errno;
		if (!quiet)
			perror(__func__);
	} else {
		ret = *((uint32_t *)p);
		if (ret) {
			const char *err_str;
			switch (ret) {
			case EV_EINVAL:
				err_str = "invalid handle";
				break;
			case EV_EFAULT:
				err_str = "bad address";
				break;
			case EV_INVALID_STATE:
				err_str = "target partition state invalid";
				break;
			default:
				err_str = "unknown error";
			}
			if (!quiet)
				printf("Error : (%u) %s\n", ret, err_str);
		}
	}

	close(f);

	return ret;
}

/**
 * Copy a block of memory to another partition via the hypervisor management
 * driver.
 */
static int copy_to_partition(unsigned int partition, void *buffer,
	unsigned long target, size_t count)
{
	struct fsl_hv_ioctl_memcpy im;

	if (!count)
		return 0;

	im.source = -1;
	im.target = partition;
	im.local_vaddr = (__u64) (unsigned long) buffer;
	im.remote_paddr = target;
	im.count = count;

	return hv(FSL_HV_IOCTL_MEMCPY, &im);
}

/**
 * Parse an ELF image, and copy the program headers into the target guest
 *
 * @partition: the partition handle to copy to
 * @elf: Pointer to the ELF image
 * @elf_length: length of the ELF image, or -1 to ignore length checking
 * @bin_length: pointer to returned buffer length
 * @load_address: load address, or -1 to use ELF load address
 * @entryp: pointer to returned entry address
 *
 * 'plowest' contains the starting physical address of the segment that has
 * the lowest starting physical address.
 *
 * Returns 0 for failure or non-zero for success
 */
static int parse_and_copy_elf(unsigned int partition, void *elf,
			      size_t elf_length, unsigned long load_address,
			      unsigned long *entryp)
{
	union elf_header_int *hdr = NULL;
	unsigned long entry_address = ULONG_MAX;
	unsigned long plowest = ULONG_MAX;
	unsigned int i;

	if (elf_length < sizeof(struct elf_header_generic)) {
		if (!quiet)
			printf("%s: truncated ELF image\n", __func__);
		return 0;
	}

	hdr = (union elf_header_int *)elf;

	/* ePAPR only supports big-endian ELF images */
	if (hdr->h.ident[EI_DATA] != ELFDATA2MSB) {
		if (!quiet)
			printf("%s: only big-endian ELF images are supported\n", __func__);
		return 0;
	}

	/* We only support ET_EXEC images for now */
	if (hdr->h.type != ET_EXEC) {
		if (!quiet)
			printf("%s: only fixed address ELF images are supported\n", __func__);
		return 0;
	}

	/* TODO: check if hv supports 64-bit */
	if (hdr->h.ident[EI_CLASS] != ELFCLASS32 &&
	    hdr->h.ident[EI_CLASS] != ELFCLASS64) {
		if (!quiet)
			printf("%s: only 32-bit and 64-bit ELF images are supported\n", __func__);
		return 0;
	}

	size_t hdr_len = (hdr->h.ident[EI_CLASS] == ELFCLASS32) ?
			sizeof(struct elf_header_32) :
			sizeof(struct elf_header_64);
	if (elf_length < hdr_len) {
		if (!quiet)
			printf("%s: truncated ELF image\n", __func__);
		return 0;
	}

	uint16_t phnum = ELF_HEADER(phnum);
	uint64_t phoff = ELF_HEADER(phoff);
	uint64_t entry = ELF_HEADER(entry);
	uint16_t phentsize = ELF_HEADER(phentsize);

	size_t phdr_len = (hdr->h.ident[EI_CLASS] == ELFCLASS32)?
			  sizeof(struct program_header_32) :
			  sizeof(struct program_header_64);
	if (elf_length < phoff + (phnum * phdr_len)) {
		if (!quiet)
			printf("%s: truncated ELF image\n", __func__);
		return 0;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base address is the smallest paddr of all PT_LOAD
	 * segments.
	 */
	for (i = 0; i < phnum; i++) {
		union program_header_int *phdr = (union program_header_int *)(elf + phoff + i * phentsize);

		uint64_t offset = PROGRAM_HEADER(offset);
		uint64_t filesz = PROGRAM_HEADER(filesz);
		uint64_t memsz  = PROGRAM_HEADER(memsz);
		uint64_t paddr  = PROGRAM_HEADER(paddr);

		if (offset + filesz > elf_length) {
			if (!quiet)
				printf("%s: truncated ELF image\n", __func__);
			return 0;
		}

		if (filesz > memsz) {
			if (!quiet)
				printf("%s: invalid ELF program header file size\n", __func__);
			return 0;
		}

		if (phdr->type == PT_LOAD && paddr < plowest)
			plowest = paddr;
	}

	if (plowest == ULONG_MAX) {
		if (!quiet)
			printf("%s: no PT_LOAD program headers in ELF image\n",
			       __func__);
		return 0;
	}

	/* If the user did not specify -a, then we use the real paddr values
	 * in the ELF image.
	 */
	if (~load_address == 0)
		load_address = plowest;

	/* Copy each PT_LOAD segment to the target partition */

	for (i = 0; i < phnum; i++) {
		union program_header_int *phdr = (union program_header_int *)(elf + phoff + i * phentsize);

		if (phdr->type == PT_LOAD) {
			uint64_t offset = PROGRAM_HEADER(offset);
			uint64_t filesz = PROGRAM_HEADER(filesz);
			uint64_t memsz  = PROGRAM_HEADER(memsz);
			uint64_t paddr  = PROGRAM_HEADER(paddr);
			uint64_t vaddr  = PROGRAM_HEADER(vaddr);

			unsigned long seg_target = load_address + (paddr - plowest);
			int ret;

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
				if (entry_address != ULONG_MAX) {
					if (!quiet)
						printf("%s: ELF entry point %" PRIx64
						       "is in multiple segments.\n",
						       __func__, entry);

					/* It's fatal if we actually need
					 * the entry.
					 */
					if (entryp)
						return 0;
				}

				entry_address = entry - vaddr + seg_target;
			}

			if (verbose)
				printf("%s: copying %" PRIx64 " bytes from "
				       "ELF image offset %" PRIx64 " to guest "
				       "physical address %lx\n",
				       __func__, filesz, offset, seg_target);

			ret = copy_to_partition(partition, elf + offset, seg_target, filesz);
			if (ret) {
				if (!quiet)
					printf("Copy failed, error=%i\n", ret);
				return 0;
			}

			if (memsz > filesz) {
				void *buffer;
				unsigned long rem_size;
				unsigned long copy_offset;

				if (verbose)
					printf("%s: writing %" PRIx64 " null bytes to "
					       "guest physical address %"  PRIx64 "\n",
					       __func__, memsz - filesz, seg_target + filesz);

#define CHUNK_SIZE	65536

				buffer = malloc(CHUNK_SIZE);
				if (!buffer) {
					if (!quiet)
						printf("could not allocate %d bytes\n", CHUNK_SIZE);
					return 0;
				}
				memset(buffer, 0, CHUNK_SIZE);

				rem_size = memsz - filesz;
				copy_offset = filesz;
				while (rem_size) {
					unsigned long copy_size;

					if (rem_size > CHUNK_SIZE)
						copy_size = CHUNK_SIZE;
					else
						copy_size = rem_size;


					ret = copy_to_partition(partition,
							buffer,
				                        seg_target + copy_offset,
				                        copy_size);
					if (ret) {
						if (!quiet)
							printf("Copy failed, error=%i\n", ret);
						return 0;
					}

					rem_size = rem_size - copy_size;
					copy_offset = copy_offset + copy_size;
				}

				free(buffer);
			}
		}
	}

	if (verbose) {
		printf("%s: load address is 0x%lx\n", __func__, load_address);
		printf("%s: physical entry address is %" PRIx64 "\n", __func__, entry);
	}

	if (entryp) {
		if (entry_address == ULONG_MAX) {
			printf("%s: ELF image has invalid entry address %" PRIx64 "\n",
			       __func__, entry);
			return 0;
		}

		*entryp = entry_address;
	}

	return 1;
}

/*FIXME: 32bit version has the following limitations
	- can't load images at guest physical addresses bigger than 4GB
	- can't load images bigger that 3GB (due to mmap)*/
/**
 * Load an image file into memory and parse it if it's an ELF
 *
 * @p: the program input parameters
 * @load_address: load address, or -1 to use ELF load address
 * @entry_address: pointer to returned entry address
 *
 * Returns 0 for failure or non-zero for success
 */
static int load_and_copy_image_file(struct parameters *p,
                                    unsigned long load_address,
				    unsigned long *entry_address)
{
	off_t off;
	int f;
	void *mapped = MAP_FAILED;
	void *buffer = NULL;
	struct stat buf;
	int ret;
	unsigned int partition = p->h;
	const char *filename = p->f;

	f = open(filename, O_RDONLY);
	if (f == -1) {
		if (!quiet)
			perror(__func__);
		return 0;
	}

	if (fstat(f, &buf)) {
		if (!quiet)
			perror(__func__);
		goto fail;
	}

	off = buf.st_size;
	if (off < 4) {
		if (!quiet)
			printf("%s: file %s is too small\n", __func__, filename);
		goto fail;
	}

	mapped = mmap(0, off, PROT_READ, MAP_SHARED, f, 0);
	if (mapped == MAP_FAILED) {
		if (!quiet)
			perror(__func__);
		goto fail;
	}

	if (memcmp(mapped, "\177ELF", 4) == 0) {
		ret = parse_and_copy_elf(partition, mapped, off, load_address, entry_address);
		if (!ret) {
			if (!quiet)
				printf("Could not load file %s,  partition handle %i, "
				       "load address %lx, error=%i\n",
				       filename, partition, load_address, ret);
			goto fail;
		}
	} else if (ntohl(((struct image_header *)mapped)->magic ==
							UIMAGE_SIGNATURE)) {
		struct image_header *hdr;

		hdr = (struct image_header *)mapped;

		if (p->r_specified) {

			char *path;
			char *prop_start;
			char *prop_end;
			unsigned long end_address;
			struct fsl_hv_ioctl_prop param;

			if (verbose)
				printf("Filesystem image: updating initrd-start/end\n");

			/* set linux,initrd-start as load address */
			path       = "/chosen";
			prop_start = "linux,initrd-start";
			prop_end   = "linux,initrd-end";

			param.handle = partition;
			param.path = (uint64_t)(uintptr_t)path;
			param.propname = (uint64_t)(uintptr_t)prop_start;
			param.propval = (uint64_t)(uintptr_t)&load_address;
			param.proplen = sizeof(uintptr_t);

			ret = hv(FSL_HV_IOCTL_SETPROP, &param);
			if (ret && !quiet) {
				printf("Could not set %s error=%i\n",
					prop_start, ret);
				goto fail;
			}

			/* set linux,initrd-end as initramfs end address */
			end_address = load_address + ntohl(hdr->size);
			param.propname = (uint64_t)(uintptr_t)prop_end;
			param.propval = (uint64_t)(uintptr_t)&end_address;


			ret = hv(FSL_HV_IOCTL_SETPROP, &param);
			if (ret && !quiet) {
				printf("Could not set %s error=%i\n",
					prop_end, ret);
				goto fail;
			}
		}
		ret = copy_to_partition(partition,
					&hdr->data,
					load_address, ntohl(hdr->size));
		if (ret && !quiet) {
			printf("Could not load file %s, partition handle %i, "
			       "at address %lx, error=%i\n",
				filename, partition, load_address, ret);
			goto fail;
		}

		if (entry_address)
			*entry_address = load_address;
	} else {
		// If -a was not specified, then assume 0
		if (load_address == (unsigned long) -1)
			load_address = 0;

		ret = copy_to_partition(partition, mapped, load_address, off);
		if (ret) {
			if (!quiet)
				printf("Could not load file %s, partition handle %i "
				       "at address %lx, error=%i\n",
				       filename, partition, load_address, ret);
			goto fail;
		}

		if (entry_address)
			*entry_address = load_address;
	}

	munmap(mapped, off);
	close(f);

	return 1;

fail:
	if (mapped != MAP_FAILED)
		munmap(mapped, off);

	free(buffer);
	close(f);

	return 0;
}

/**
 * get_full_node_name - returns the full path name of the given node
 * @param node: the node
 * @param name: pointer to a buffer to hold the path
 * @param lenghth: size of @name
 *
 * Traverses the ancestors of the given node and print out a full path name
 * of the node.
 *
 * Returns a pointer to the buffer.
 */
static char *get_full_node_name(gdt_node_t *node, char *name, size_t length)
{
	char temp[PATH_MAX];

	strncpy(name, node->name, length);
	node = node->parent;

	while (node && node->name) {
		snprintf(temp, PATH_MAX, "%s/%s", node->name, name);
		strncpy(name, temp, length);
		node = node->parent;
	}

	return name;
}

/* Command parsers */

/**
 * Displays a list of partitions and their status
 */
static int cmd_status(void)
{
	gdt_node_t *node = NULL;

	printf("Partition Name                  Handle  Status\n"
	       "----------------------------------------------\n");

	while ((node = gdt_find_next_compatible(node, "fsl,hv-partition-handle")) != NULL) {
		gdt_prop_t *prop;
		uint32_t handle;
		const char *label;
		struct fsl_hv_ioctl_status status;
		int ret;

		prop = gdt_get_property(node, "hv-handle");
		if (!prop)
			prop = gdt_get_property(node, "reg");

		if (!prop || (prop->len != sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "hv-handle/reg", node->name);
			return EV_ENOENT;
		}
		handle = *((uint32_t *) prop->data);

		prop = gdt_get_property(node, "label");
		if (!prop || !prop->len) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "label", node->name);
			return EV_ENOENT;
		}
		label = prop->data;

		printf("%-31s %-7u ", label, handle);

		status.partition = handle;
		ret = hv(FSL_HV_IOCTL_PARTITION_GET_STATUS, (void *) &status);
		if (ret) {
			printf("error %i\n", ret);
		} else {
			switch (status.status) {
			case 0: printf("stopped\n"); break;
			case 1: printf("running\n"); break;
			case 2: printf("starting\n"); break;
			case 3: printf("stopping\n"); break;
			case 4: printf("pausing\n"); break;
			case 5: printf("paused\n"); break;
			case 6: printf("resuming\n"); break;
			default: printf("unknown %i\n", status.status); break;
			}
		}
	}

	printf("\n"
	       "Byte Channel Name               Handle  RX Int  TX Int\n"
	       "------------------------------------------------------\n");

	node = NULL;
	while ((node = gdt_find_next_compatible(node, "fsl,hv-byte-channel-handle")) != NULL) {
		gdt_prop_t *prop;
		uint32_t handle;
		uint32_t *irq;

		prop = gdt_get_property(node, "hv-handle");
		if (!prop)
			prop = gdt_get_property(node, "reg");

		if (!prop || (prop->len != sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "hv-handle/reg", node->name);
			return EV_ENOENT;
		}
		handle = *((uint32_t *) prop->data);

		prop = gdt_get_property(node, "interrupts");
		if (!prop ||(prop->len != 4 * sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "interrupts", node->name);
			return EV_ENOENT;
		}
		irq = prop->data;

		printf("%-31s %-7u %-7u %u\n", node->name, handle, irq[0], irq[2]);
	}

	printf("\n"
	       "Doorbell Name                   Handle  Type\n"
	       "--------------------------------------------\n");

	node = NULL;
	while ((node = gdt_find_next_compatible(node, "epapr,hv-receive-doorbell")) != NULL) {
		gdt_prop_t *prop;
		char name[32];
		uint32_t *irq;

		prop = gdt_get_property(node, "interrupts");
		if (!prop ||(prop->len != 2 * sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "interrupts", node->name);
			return EV_ENOENT;
		}
		irq = prop->data;

		printf("%-31s %-7u %s\n", get_full_node_name(node, name, sizeof(name)),
		       irq[0], "receive");
	}

	node = NULL;
	while ((node = gdt_find_next_compatible(node, "epapr,hv-send-doorbell")) != NULL) {
		gdt_prop_t *prop;
		uint32_t handle;
		char name[32];

		prop = gdt_get_property(node, "hv-handle");
		if (!prop)
			prop = gdt_get_property(node, "reg");

		if (!prop || (prop->len != sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "hv-handle/reg", node->name);
			return EV_ENOENT;
		}
		handle = *((uint32_t *) prop->data);

		printf("%-31s %-7u %s\n", get_full_node_name(node, name, sizeof(name)),
		       handle, "send");
	}

	return 0;
}

/**
 * Load an ELF or binary file into another partition
 */
static int cmd_load_image(struct parameters *p)
{
	unsigned long load_address;
	unsigned long entry_address;
	int ret = 0;

	if (!p->h_specified || !p->f) {
		fprintf(stderr, "partman load: requires a handle (-h) "
		                "and file (-f).\n");
		usage();
		return EV_EINVAL;
	}

	if (verbose)
		printf("Loading file %s\n", p->f);

	load_address = p->a_specified ? p->a : (unsigned long) -1;

	ret = load_and_copy_image_file(p, load_address, &entry_address);
	if (!ret && !quiet) {
		printf("Could not load and copy file %s\n", p->f);
		return EV_EIO;
	}

	if (verbose)
		printf("(Entry address is 0x%lx)\n", entry_address);

	return ret ? 0 : 1;
}

static int cmd_start(struct parameters *p)
{
	struct fsl_hv_ioctl_start im;

	if (!p->h_specified) {
		fprintf(stderr, "partman start: requires a handle (-h).\n");
		usage();
		return EV_EINVAL;
	}

	if (verbose)
		printf("Starting partition %i\n", p->h);

	if (!p->e_specified)
		p->e = 0;

	if (!p->a_specified)
		p->a = 0;

	if (p->f) {
		unsigned long load_address;
		unsigned long entry_address;
		int ret = 0;

		if (verbose)
			printf("Loading and copying file %s\n", p->f);

		load_address = p->a_specified ? p->a : (unsigned long) -1;

		ret = load_and_copy_image_file(p, load_address, &entry_address);
		if (!ret)
			return EV_EIO;

		if (!p->e_specified)
			p->e = entry_address;
	}

	im.partition = p->h;
	im.entry_point = p->e;
	im.load = p->l_specified;

	if (verbose)
		printf("Starting partition at entry point 0x%lx\n", p->e);

	return hv(FSL_HV_IOCTL_PARTITION_START, &im);
}

static int cmd_stop(struct parameters *p)
{
	struct fsl_hv_ioctl_stop im;

	if (!p->h_specified) {
		fprintf(stderr, "partman stop: requires a handle (-h).\n");
		usage();
		return EV_EINVAL;
	}

	if (verbose)
		printf("Stopping partition %i\n", p->h);

	im.partition = p->h;

	return hv(FSL_HV_IOCTL_PARTITION_STOP, &im);
}

static int cmd_restart(struct parameters *p)
{
	struct fsl_hv_ioctl_restart im;

	if (!p->h_specified) {
		fprintf(stderr, "partman restart: requires a handle (-h).\n");
		usage();
		return EV_EINVAL;
	}

	if (verbose)
		printf("Restarting partition %i\n", p->h);

	im.partition = p->h;

	return hv(FSL_HV_IOCTL_PARTITION_RESTART, &im);
}

static int cmd_doorbells(struct parameters *p)
{
	uint32_t dbell;
	ssize_t ret;
	char command[PATH_MAX + 32];
	char temp[PATH_MAX + 1];

	// Either -h or -f, but not both, must be specified

	if ((p->h_specified && p->f) || (!p->h_specified && !p->f)) {
		fprintf(stderr, "partman doorbell: requires a handle (-h) "
		                "*or* file (-f), but not both.\n");
		usage();
		return EV_EINVAL;
	}

	// If -h is specified, user wants to send a doorbell
	if (p->h_specified) {
		struct fsl_hv_ioctl_doorbell im;

		im.doorbell = p->h;
		return hv(FSL_HV_IOCTL_DOORBELL, &im);
	}

	// The user specified -f, so he wants to monitor doorbells.
	// Verify that the passed-in script exists and can actually be
	// executed.
	snprintf(command, sizeof(command), "test -x %s", p->f);
	if (system(command)) {
		if (!quiet)
			printf("%s is not a executable file\n", p->f);
		return EV_EINVAL;
	}

	int f = open("/dev/fsl-hv", O_RDONLY);
	if (f == -1) {
		ret = errno;
		if (!quiet)
			perror(__func__);
		return ret;
	}

	while (1) {
		ret = read(f, &dbell, sizeof(dbell));
		if (ret <= 0) {
			if (!quiet)
				perror(__func__);
			break;
		}

		snprintf(command, sizeof(command), "%s %u", realpath(p->f, temp), dbell);

		system(command);
	}

	close(f);

	return 0;
}

/**
 * verify_dev - verify that the management driver is loaded and /dev/fsl-hv is correct
 *
 * This function shouldn't be neccessary, but too many people are having
 * problems with their root file systems, so here we try to help them out.
 *
 * returns 0 if it isn't, 1 if it's okay
 */
int verify_dev(void)
{
	struct stat statbuf;
	int ret;
	FILE *f;
	unsigned int major, minor;

	f = fopen("/sys/class/misc/fsl-hv/dev", "r");
	if (!f) {
		if (!quiet) {
			printf("Freescale HV management driver has not been loaded.\n");
			printf("Please load the driver or rebuild the kernel with this driver enabled.\n");
		}
		return 0;
	}
	ret = fscanf(f, "%u:%u", &major, &minor);
	if ((ret != 2) || (major == 0)) {
		if (!quiet)
			printf("Could not read the sysfs entry for the HV management driver.\n");
		return 0;
	}
	fclose(f);

	if (stat("/dev/fsl-hv", &statbuf))
	{
		switch (errno) {
		case EACCES:
			if (!quiet) {
				printf("Permissions on /dev/fsl-hv are incorrect.  As root, try this:\n");
				printf("chmod a+rw /dev/fsl-hv\n");
				printf("and try partman again\n");
			}
			return 0;
		case ENOENT:
			if (!quiet) {
				printf("/dev/fsl-hv does not exist.  Please check your root file system.\n");
				printf("As a temporary fix, do this as root:\n");
				printf("mknod /dev/fsl-hv c %u %u\n", major, minor);
			}
			return 0;
		default:
			if (!quiet)
				printf("Something is wrong with /dev/fsl-hv (errno=%i).\n", errno);
			return 0;
		}
	}

	if ((statbuf.st_mode & S_IFMT) != S_IFCHR) {
		if (!quiet) {
			printf("/dev/fsl-hv is not a character device.  Please check your root file system.\n");
			printf("As a temporary fix, do this as root:\n");
			printf("rm -f /dev/fsl-hv\n");
			printf("mknod /dev/fsl-hv c %u %u\n", major, minor);
		}
		return 0;
	}

	if ((major(statbuf.st_rdev) != major) || (minor(statbuf.st_rdev) != minor)) {
		if (!quiet) {
			printf("Major and/or minor number of /dev/fsl-hv are wrong.\n");
			printf("Please check your root file system.  As a temporary fix, do this as root:\n");
			printf("rm -f /dev/fsl-hv\n");
			printf("mknod /dev/fsl-hv c %u %u\n", major, minor);
		}
		return 0;
	}

	return 1;
}

/**
 * get_handle - return the numeric handle that matches the name of the node
 * @param name: the full name of the node to search for
 *
 * This function tries to match a node's full name to its numeric handle.
 *
 * Returns the handle number, or -1 if error
 */
static int get_handle(const char *name)
{
	gdt_node_t *node = NULL;
	gdt_prop_t *prop;
	char *short_name = strrchr(name, '/');
	char full_name[PATH_MAX];

	if (!short_name)
		node = gdt_find_next_name(NULL, name);
	else {
		// short_name is the part of the name after the last '/'
		short_name++;

		while ((node = gdt_find_next_name(node, short_name)) != NULL) {
			// We found a match on the short name, so let's see if it
			// matches the full name.
			get_full_node_name(node, full_name, PATH_MAX);
			if (strcmp(full_name, name) == 0)
				break;
		}
	}

	if (!node)
		return -1;

	// Receive doorbells have their handle in the "interrupts" property.
	// Everyone else has it in the "hv-handle" property. However if the
	// hv-handle property is missing the handle is read from the reg
	// property to maintain compatibility with older versions of Topaz

	if (gdt_is_compatible(node, "epapr,hv-receive-doorbell"))
		prop = gdt_get_property(node, "interrupts");
	else {
		prop = gdt_get_property(node, "hv-handle");
		if (!prop)
			prop = gdt_get_property(node, "reg");
	}

	if (!prop || (prop->len < sizeof(uint32_t)))
		return -1;

	return *((uint32_t *) (prop->data));
}

/**
 * get_part_handle_by_label - return the numeric handle of a partition having the specified label
 * @param label: label of the partition to search for
 *
 * This function tries to find a partition by its "label" property and return its numeric handle.
 *
 * Returns the handle number, or -1 if error
 */
static int get_part_handle_by_label(const char *label)
{
	gdt_node_t *node = NULL;
	gdt_prop_t *prop;
	int handle = -1;

	while ((node = gdt_find_next_compatible(node, "fsl,hv-partition-handle")) != NULL) {
		prop = gdt_get_property(node, "label");
		if (!prop || !prop->len) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "label", node->name);
			return -1;
		}
		if (!strcmp(prop->data, label)) {
			prop = gdt_get_property(node, "hv-handle");
			if (!prop)
				prop = gdt_get_property(node, "reg");

			if (!prop || (prop->len != sizeof(uint32_t))) {
				printf("%s: '%s' property for %s is missing or invalid\n",
				       __func__, "hv-handle/reg", node->name);
				return -1;
			}
			handle = *((uint32_t *) prop->data);
			break;
		}
	}

	return handle;
}

static int cmd_getprop(struct parameters *p)
{
	struct fsl_hv_ioctl_prop param;
	int ret;

	if (!p->h_specified || !p->n || !p->p) {
		fprintf(stderr, "partman getprop: requires a handle (-h), "
		                "a path (-p) and a property name (-n)\n");
		usage();
		return EV_EINVAL;
	}

	if (verbose)
		printf("Getting property %s of %s in guest %i\n",
		       p->n, p->p, p->h);

	param.handle = p->h;
	param.path = (uint64_t)(uintptr_t)p->p;
	param.propname = (uint64_t)(uintptr_t)p->n;
	param.propval = (uint64_t)(uintptr_t)p->prop;
	param.proplen = sizeof(p->prop);

	ret = hv(FSL_HV_IOCTL_GETPROP, &param);
	if (ret == 0) {
		// FIXME: pretty-print strlists, cells, bytes, etc.
		fwrite(p->prop, param.proplen, 1, stdout);
		printf("\n");
	}

	return ret;
}

static int cmd_setprop(struct parameters *p)
{
	struct fsl_hv_ioctl_prop param;

	if (!p->h_specified || !p->n || !p->p) {
		fprintf(stderr, "partman setprop: requires a handle (-h), "
		                "a path (-p) and a property name (-n)\n");
		usage();
		return EV_EINVAL;
	}

	if (verbose)
		printf("Getting property %s of %s in guest %i\n",
		       p->n, p->p, p->h);

	param.handle = p->h;
	param.path = (uint64_t)(uintptr_t)p->p;
	param.propname = (uint64_t)(uintptr_t)p->n;
	param.propval = (uint64_t)(uintptr_t)p->prop;
	param.proplen = p->proplen;

	return hv(FSL_HV_IOCTL_SETPROP, &param);
}

static void add_to_prop(struct parameters *p, const char *arg)
{
	/* FIXME: check for <> or [] */
	size_t len = strlen(arg) + 1;

	if (p->proplen + len > PROP_MAX) {
		fprintf(stderr, "partman: %s: property too long\n", __func__);
		exit(1);
	}

	memcpy(p->prop + p->proplen, arg, len);
	p->proplen += len;
}

int main(int argc, char *argv[])
{
	struct parameters p = {};
	int c;
	const char *handlestr = NULL, *cmdstr = NULL;
	int partition_cmd = 0;

	opterr = 0;

	while ((c = getopt(argc, argv, "-:vqh:f:a:e:n:p:t:l:r")) != -1) {
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'h':
			handlestr = optarg;
			break;
		case 'f':
			p.f = optarg;
			break;
		case 'a':
			p.a = strtoul(optarg, NULL, 0);
			p.a_specified = 1;
			break;
		case 'e':
			p.e = strtoul(optarg, NULL, 0);
			p.e_specified = 1;
			break;
		case 'n':
			p.n = optarg;
			break;
		case 'p':
			p.p = optarg;
			break;
		case 't':
			add_to_prop(&p, optarg);
			break;
		case 'l':
			p.l_specified = 1;
			break;
		case 'r':
			p.r_specified = 1;
			break;
		case 1:
			if (cmdstr) {
				fprintf(stderr, "%s: unexpected command '%s'\n",
					argv[0], optarg);
				usage();
				return 1;
			}
			cmdstr = optarg;
			break;
		case '?':
			fprintf(stderr, "%s: unrecognized option '%c'\n",
			        argv[0], optopt);
			usage();
			return 1;
		case ':':
			fprintf(stderr, "%s: missing argument to option '%c'\n",
			        argv[0], optopt);
			usage();
			return 1;
		default:
			/* Shouldn't happen... */
			usage();
			return 1;
		}
	}

	if (!cmdstr) {
		usage();
		return 1;
	}

	if (verbose)
		printf("Freescale Hypervisor Partition Manager %s\n\n", VERSION);

	if (!verify_dev())
		return 1;

	if (!gdt_load_tree("/proc/device-tree/hypervisor/handles")) {
		printf("Could not load device tree\n");
		return 1;
	}

	/* Check if it's a partition related command */
	if (!strcmp(cmdstr, "load") ||
	    !strcmp(cmdstr, "start") ||
	    !strcmp(cmdstr, "stop") ||
	    !strcmp(cmdstr, "restart") ||
	    !strcmp(cmdstr, "getprop") ||
	    !strcmp(cmdstr, "setprop"))
		partition_cmd = 1;

	/* Parse the generic handle argument and obtain its numeric equivalent.
	 * The following cases are handled:
	 * 1. Partition specified by its label
	 * 2. Generic handle specified by the name of the node describing it
	 * 3. Explicit numeric handle specified as argument
	 * 4. No handle argument
	 */
	if (handlestr) {
		int ret;

		ret = partition_cmd ? get_part_handle_by_label(handlestr) : get_handle(handlestr);
		if (ret < 0) {
			char *endptr;
			p.h = strtol(handlestr, &endptr, 0);
			if (!strlen(handlestr) || *endptr != '\0') {
				printf("Invalid handle\n");
				return 1;
			}
		} else {
			p.h = ret;
		}
		p.h_specified = 1;
	}

	if (!strcmp(cmdstr, "status"))
		return cmd_status();
	if (!strcmp(cmdstr, "load"))
		return cmd_load_image(&p);
	if (!strcmp(cmdstr, "start"))
		return cmd_start(&p);
	if (!strcmp(cmdstr, "stop"))
		return cmd_stop(&p);
	if (!strcmp(cmdstr, "restart"))
		return cmd_restart(&p);
	if (!strcmp(cmdstr, "doorbell"))
		return cmd_doorbells(&p);
	if (!strcmp(cmdstr, "getprop"))
		return cmd_getprop(&p);
	if (!strcmp(cmdstr, "setprop"))
		return cmd_setprop(&p);

	fprintf(stderr, "%s: unknown command \"%s\"\n", argv[0], cmdstr);
	usage();

	return 1;
}

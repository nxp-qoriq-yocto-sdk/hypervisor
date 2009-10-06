/*
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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

#include "parse_dt.h"

/*
 * fsl_hypervisor.h is copied from drivers/misc in the Linux kernel source
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
};

/* Elf file types that we support */
#define ET_EXEC   2
#define ET_DYN    3

/* Program header types that we support */
#define PT_LOAD      1

#define EI_CLASS     4  /* File class */
#define ELFCLASS32   1  /* 32-bit objects */
#define EI_DATA      5  /* Data encoding */
#define ELFDATA2MSB  2  /* 2's complement, big endian */

static int verbose = 0;
static int quiet = 0;

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

	printf("   partman load -h <handle> -f <file> [-a <address>]\n"
	       "      Load an image.\n");

	printf("   partman start -h <handle> [-f <file>] [-e <addr>] [-a <addr>]\n"
	       "     Start a partition.\n");

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

#define FH_ERR_INVALID_STATE 1026

/**
 * Call the hypervisor management driver.
 *
 * Return code:
 * 0: success
 * <0: ioctl error condition
 * >0: Hypervisor error condition
 */
static int hv(unsigned int cmd, union fsl_hv_ioctl_param *p)
{
	int ret = 0;

	int f = open("/dev/fsl-hv", O_RDWR | O_SYNC);
	if (f == -1) {
		ret = errno;
		if (!quiet)
			perror(__func__);
		return ret;
	}

	if (ioctl(f, _IOWR(0, cmd, union fsl_hv_ioctl_param), p) == -1) {
		ret = errno;
		if (!quiet)
			perror(__func__);
	} else {
		ret = p->ret;
		if (ret) {
			const char *err_str;
			switch (ret) {
			case EINVAL:
				err_str = "invalid handle";
				break;
			case EFAULT:
				err_str = "bad address";
				break;
			case FH_ERR_INVALID_STATE:
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

	return hv(FSL_HV_IOCTL_MEMCPY, (union fsl_hv_ioctl_param *)&im);
}

/**
 * Parse an ELF image, and copy the program headers into the target guest
 *
 * @partition: the partition handle to copy to
 * @elf: Pointer to the ELF image
 * @elf_length: length of the ELF image, or -1 to ignore length checking
 * @bin_length: pointer to returned buffer length
 * @load_address: load address, or -1 to use ELF load address
 * @entry_address: pointer to returned entry address
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
	struct elf_header *hdr = (struct elf_header *) elf;
	struct program_header *phdr = (struct program_header *) (elf + hdr->phoff);
	unsigned long entry = ULONG_MAX;
	unsigned long plowest = ULONG_MAX;
	unsigned int i;

	if (elf_length < sizeof(struct elf_header)) {
		if (!quiet)
			printf("%s: truncated ELF image\n", __func__);
		return 0;
	}

	if (elf_length < hdr->phoff + (hdr->phnum * sizeof(struct program_header))) {
		if (!quiet)
			printf("%s: truncated ELF image\n", __func__);
		return 0;
	}

	/* We only support 32-bit for now */
	if (hdr->ident[EI_CLASS] != ELFCLASS32) {
		if (!quiet)
			printf("%s: only 32-bit ELF images are supported\n", __func__);
		return 0;
	}

	/* ePAPR only supports big-endian ELF images */
	if (hdr->ident[EI_DATA] != ELFDATA2MSB) {
		if (!quiet)
			printf("%s: only big-endian ELF images are supported\n", __func__);
		return 0;
	}

	/* We only support ET_EXEC images for now */
	if (hdr->type != ET_EXEC) {
		if (!quiet)
			printf("%s: only fixed address ELF images are supported\n", __func__);
		return 0;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base address is the smallest paddr of all PT_LOAD
	 * segments.
	 */
	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].offset + phdr[i].filesz > elf_length) {
			if (!quiet)
				printf("%s: truncated ELF image\n", __func__);
			return 0;
		}
		if (phdr[i].filesz > phdr[i].memsz) {
			if (!quiet)
				printf("%s: invalid ELF program header file size\n", __func__);
			return 0;
		}
		if (phdr[i].type == PT_LOAD && phdr[i].paddr < plowest)
			plowest = phdr[i].paddr;
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
	if (load_address == (unsigned long) -1)
		load_address = plowest;

	/* Copy each PT_LOAD segment to the target partition */

	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].type == PT_LOAD) {
			unsigned long seg_target = load_address + (phdr[i].paddr - plowest);
			int ret;

			if (phdr[i].vaddr <= hdr->entry &&
			    hdr->entry <= phdr[i].vaddr + phdr[i].memsz - 1) {
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
					if (!quiet)
						printf("%s: ELF entry point %#x "
						       "is in multiple segments.\n",
						       __func__, hdr->entry);

					/* It's fatal if we actually need 
					 * the entry.
					 */
					if (entryp)
						return 0;
				}

				entry = hdr->entry - phdr[i].vaddr + seg_target;
			}

			if (verbose)
				printf("%s: copying 0x%x bytes from ELF image offset 0x%x to guest physical address 0x%lx\n",
				       __func__, phdr[i].filesz, phdr[i].offset, seg_target);
			ret = copy_to_partition(partition, elf + phdr[i].offset, seg_target, phdr[i].filesz);
			if (ret) {
				if (!quiet)
					printf("Copy failed, error=%i\n", ret);
				return 0;
			}

			if (phdr[i].memsz > phdr[i].filesz) {
				void *buffer;

				if (verbose)
					printf("%s: writing 0x%x null bytes to guest physical address 0x%lx\n",
					       __func__, phdr[i].memsz - phdr[i].filesz, seg_target + phdr[i].filesz);

				buffer = malloc(phdr[i].memsz - phdr[i].filesz);
				if (!buffer) {
					if (!quiet)
						printf("could not allocate %u bytes\n",
						       phdr[i].memsz - phdr[i].filesz);
					return 0;
				}
				memset(buffer, 0, phdr[i].memsz - phdr[i].filesz);
				ret = copy_to_partition(partition, buffer,
				                        seg_target + phdr[i].filesz,
				                        phdr[i].memsz - phdr[i].filesz);
				free(buffer);
				if (ret) {
					if (!quiet)
						printf("Copy failed, error=%i\n", ret);
					return 0;
				}
			}
		}
	}

	if (verbose) {
		printf("%s: load address is 0x%lx\n", __func__, load_address);
		printf("%s: physical entry address is 0x%lx\n", __func__, entry);
	}

	if (entryp) {
		if (entry == ULONG_MAX) {
			printf("%s: ELF image has invalid entry address %x\n",
			       __func__, hdr->entry);
			return 0;
		}

		*entryp = entry;
	}

	return 1;
}

/**
 * Load an image file into memory and parse it if it's an ELF
 *
 * @partition: the partition handle to copy to
 * @filename: file name of image
 * @load_address: load address, or -1 to use ELF load address
 * @entry_address: pointer to returned entry address
 *
 * Returns 0 for failure or non-zero for success
 */
static int load_and_copy_image_file(unsigned int partition,
				    const char *filename,
                                    unsigned long load_address,
				    unsigned long *entry_address)
{
	off_t off;
	int f;
	void *mapped = MAP_FAILED;
	void *buffer = NULL;
	struct stat buf;
	int ret;

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
				printf("Could not load file %s to partition %i at address %lx, error=%i\n",
				       filename, partition, load_address, ret);
			goto fail;
		}
	} else if (ntohl(((struct image_header *)mapped)->magic ==
							UIMAGE_SIGNATURE)) {
		struct image_header *hdr;

		hdr = (struct image_header *)mapped;
		ret = copy_to_partition(partition,
					&hdr->data,
					load_address, ntohl(hdr->size));
		if (ret && !quiet) {
			printf("Could not load file %s to partition %i "
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
				printf("Could not load file %s to partition %i at address %lx, error=%i\n",
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
		uint32_t reg;
		const char *label;
		struct fsl_hv_ioctl_status status;
		int ret;

		prop = gdt_get_property(node, "reg");
		if (!prop || (prop->len != sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "reg", node->name);
			return -ENOENT;
		}
		reg = * ((uint32_t *) prop->data);

		prop = gdt_get_property(node, "label");
		if (!prop || !prop->len) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "label", node->name);
			return -ENOENT;
		}
		label = prop->data;

		printf("%-31s %-7u ", label, reg);

		status.partition = reg;
		ret = hv(FSL_HV_IOCTL_PARTITION_GET_STATUS, (void *) &status);
		if (ret) {
			printf("error %i\n", ret);
		} else {
			switch (status.status) {
			case 0: printf("stopped\n"); break;
			case 1: printf("running\n"); break;
			case 2: printf("starting\n"); break;
			case 3: printf("stopping\n"); break;
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
		uint32_t reg;
		uint32_t *irq;

		prop = gdt_get_property(node, "reg");
		if (!prop || (prop->len != sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "reg", node->name);
			return -ENOENT;
		}
		reg = * ((uint32_t *) prop->data);

		prop = gdt_get_property(node, "interrupts");
		if (!prop ||(prop->len != 4 * sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "interrupts", node->name);
			return -ENOENT;
		}
		irq = prop->data;

		printf("%-31s %-7u %-7u %u\n", node->name, reg, irq[0], irq[2]);
	}

	printf("\n"
	       "Doorbell Name                   Handle  Type\n"
	       "--------------------------------------------\n");

	node = NULL;
	while ((node = gdt_find_next_compatible(node, "fsl,hv-doorbell-receive-handle")) != NULL) {
		gdt_prop_t *prop;
		char name[32];
		uint32_t *irq;

		prop = gdt_get_property(node, "interrupts");
		if (!prop ||(prop->len != 2 * sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "interrupts", node->name);
			return -ENOENT;
		}
		irq = prop->data;

		printf("%-31s %-7u %s\n", get_full_node_name(node, name, sizeof(name)),
		       irq[0], "receive");
	}

	node = NULL;
	while ((node = gdt_find_next_compatible(node, "fsl,hv-doorbell-send-handle")) != NULL) {
		gdt_prop_t *prop;
		uint32_t reg;
		char name[32];

		prop = gdt_get_property(node, "reg");
		if (!prop || (prop->len != sizeof(uint32_t))) {
			printf("%s: '%s' property for %s is missing or invalid\n",
			       __func__, "reg", node->name);
			return -ENOENT;
		}
		reg = * ((uint32_t *) prop->data);

		printf("%-31s %-7u %s\n", get_full_node_name(node, name, sizeof(name)),
		       reg, "send");
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
		return EINVAL;
	}

	if (verbose)
		printf("Loading file %s\n", p->f);

	load_address = p->a_specified ? p->a : (unsigned long) -1;

	ret = load_and_copy_image_file(p->h, p->f, load_address, &entry_address);
	if (!ret && !quiet) {
		printf("Could not load and copy file %s\n", p->f);
		return EIO;
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
		return EINVAL;
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

		ret = load_and_copy_image_file(p->h, p->f, load_address, &entry_address);
		if (!ret)
			return EIO;

		if (!p->e_specified)
			p->e = entry_address;
	}

	im.partition = p->h;
	im.entry_point = p->e;

	if (verbose)
		printf("Starting partition at entry point 0x%lx\n", p->e);

	return hv(FSL_HV_IOCTL_PARTITION_START, (union fsl_hv_ioctl_param *)&im);
}

static int cmd_stop(struct parameters *p)
{
	struct fsl_hv_ioctl_stop im;

	if (!p->h_specified) {
		fprintf(stderr, "partman stop: requires a handle (-h).\n");
		usage();
		return EINVAL;
	}

	if (verbose)
		printf("Stopping partition %i\n", p->h);

	im.partition = p->h;

	return hv(FSL_HV_IOCTL_PARTITION_STOP, (union fsl_hv_ioctl_param *)&im);
}

static int cmd_restart(struct parameters *p)
{
	struct fsl_hv_ioctl_restart im;

	if (!p->h_specified) {
		fprintf(stderr, "partman restart: requires a handle (-h).\n");
		usage();
		return EINVAL;
	}

	if (verbose)
		printf("Restarting partition %i\n", p->h);

	im.partition = p->h;

	return hv(FSL_HV_IOCTL_PARTITION_RESTART, (union fsl_hv_ioctl_param *)&im);
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
		return EINVAL;
	}

	// If -h is specified, user wants to send a doorbell
	if (p->h_specified) {
		struct fsl_hv_ioctl_doorbell im;

		im.doorbell = p->h;
		return hv(FSL_HV_IOCTL_DOORBELL, (union fsl_hv_ioctl_param *)&im);
	}

	// The user specified -f, so he wants to monitor doorbells.
	// Verify that the passed-in script exists and can actually be
	// executed.
	snprintf(command, sizeof(command), "test -x %s", p->f);
	if (system(command)) {
		if (!quiet)
			printf("%s is not a executable file\n", p->f);
		return EINVAL;
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
	// Everyone else has it in the "reg" property.

	if (gdt_is_compatible(node, "fsl,hv-doorbell-receive-handle"))
		prop = gdt_get_property(node, "interrupts");
	else
		prop = gdt_get_property(node, "reg");

	if (!prop || (prop->len < sizeof(uint32_t)))
		return -1;

	return *((uint32_t *) (prop->data));
}

static int cmd_getprop(struct parameters *p)
{
	struct fsl_hv_ioctl_prop param;
	int ret;

	if (!p->h_specified || !p->n || !p->p) {
		fprintf(stderr, "partman getprop: requires a handle (-h), "
		                "a path (-p) and a property name (-n)\n");
		usage();
		return EINVAL;
	}

	if (verbose)
		printf("Getting property %s of %s in guest %i\n",
		       p->n, p->p, p->h);

	param.handle = p->h;
	param.path = (uint64_t)(uintptr_t)p->p;
	param.propname = (uint64_t)(uintptr_t)p->n;
	param.propval = (uint64_t)(uintptr_t)p->prop;
	param.proplen = sizeof(p->prop);

	ret = hv(FSL_HV_IOCTL_GETPROP, (union fsl_hv_ioctl_param *)&param);
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
		return EINVAL;
	}

	if (verbose)
		printf("Getting property %s of %s in guest %i\n",
		       p->n, p->p, p->h);

	param.handle = p->h;
	param.path = (uint64_t)(uintptr_t)p->p;
	param.propname = (uint64_t)(uintptr_t)p->n;
	param.propval = (uint64_t)(uintptr_t)p->prop;
	param.proplen = p->proplen;

	return hv(FSL_HV_IOCTL_SETPROP, (union fsl_hv_ioctl_param *)&param);
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
	const char *handlestr;

	opterr = 0;

	while ((c = getopt(argc, argv, ":vqh:f:a:e:n:p:t:")) != -1) {
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

	if (optind > argc - 1) {
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

	if (handlestr) {
		int ret = get_handle(handlestr);
		if (ret < 0)
			p.h = strtol(handlestr, NULL, 0);
		else
			p.h = ret;
		p.h_specified = 1;
	}

	if (!strcmp(argv[optind], "status"))
		return cmd_status();
	if (!strcmp(argv[optind], "load"))
		return cmd_load_image(&p);
	if (!strcmp(argv[optind], "start"))
		return cmd_start(&p);
	if (!strcmp(argv[optind], "stop"))
		return cmd_stop(&p);
	if (!strcmp(argv[optind], "restart"))
		return cmd_restart(&p);
	if (!strcmp(argv[optind], "doorbell"))
		return cmd_doorbells(&p);
	if (!strcmp(argv[optind], "getprop"))
		return cmd_getprop(&p);
	if (!strcmp(argv[optind], "setprop"))
		return cmd_setprop(&p);

	fprintf(stderr, "%s: unknown command \"%s\"\n", argv[0], argv[optind]);
	usage();
	return 1;
}

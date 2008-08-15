
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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
 *
 * Support for restarting a parition has been disabled, since the hypervisor
 * does not fully support restarting a partition, and since the partman
 * utility does not accept "-1" as a partition handle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <stropts.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>

/*
 * fsl_hypervisor.h is copied from drivers/misc in the Linux kernel source
 * tree.  So every time that file is updated, you need to copy it here.
 */
#include "fsl_hypervisor.h"

enum command {
	e_none = 0,
	e_status,
	e_load,
	e_start,
	e_stop,
	e_restart
};

struct parameters {
	int h;
	const char *f;
	unsigned long a;
	unsigned long e;
	unsigned int h_specified:1;
	unsigned int f_specified:1;
	unsigned int a_specified:1;
	unsigned int e_specified:1;
};

#define PT_NULL      0
#define PT_LOAD      1

#define EI_CLASS     4  /* File class */
#define ELFCLASS32   1  /* 32-bit objects */
#define EI_DATA      5  /* Data encoding */
#define ELFDATA2MSB  2  /* 2's complement, big endian */

static int verbose = 0;

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

/* Support functions */

/**
 * Display command line parameters
 */
static void usage(void)
{
	printf("Usage:\n\n");

	printf("Show partition status:\n\tpartman -s\n\n");

	printf("Load image:\n\tpartman -l -h <handle> -f <file> [-a <address>]\n");
	printf("\t\tDefault value for -a is 0, or the base physical address\n");
	printf("\t\tfor ELF images.\n\n");

	printf("Start partition:\n\tpartman -g -h <handle> [-f <file>] [-e <address>] [-a <address>]\n");
	printf("\t\tDefault value for -a is 0, or the base physical address\n");
	printf("\t\tfor ELF images.\n");
	printf("\t\tDefault value for -e is 0, or the entry point for ELF\n");
	printf("\t\timages.\n\n");

	printf("Stop partition:\n\tpartman -x -h <handle>\n\n");
/*
	printf("Restart partition:\n\tpartman -r -h <handle> [-f <file>] [-e <address>] [-a <address>]\n");
	printf("\t\tDefault value for -a is 0, or the base physical address\n");
	printf("\t\tfor ELF images.\n");
	printf("\t\tDefault value for -e is 0, or the entry point for ELF\n");
	printf("\t\timages.\n\n");
*/
	printf("Specify -v for verbose output\n\n");
}

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
		if (verbose)
			perror(__func__);
		return ret;
	}

	if (ioctl(f, cmd, p) == -1) {
		ret = errno;
		if (verbose)
			perror(__func__);
	} else {
		ret = p->ret;
		if (verbose && ret)
			printf("Hypervisor returned error %u\n", ret);
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

	return hv(FSL_HV_IOCTL_MEMCPY, (void *) &im);
}

/**
 * Parse an ELF image into a buffer
 *
 * @elf: Pointer to the ELF image
 * @elf_length: length of the ELF image, or -1 to ignore length checking
 * @bin_length: pointer to returned buffer length
 * @load_address: pointer to returned load address
 * @entry_offset: pointer to returned offset of entry address within buffer
 */
static void *parse_elf(void *elf, size_t elf_length, size_t *bin_length,
	unsigned long *load_address, unsigned long *entry_offset)
{
	struct elf_header *hdr = (struct elf_header *) elf;
	struct program_header *phdr = (struct program_header *) (elf + hdr->phoff);
	void *buffer = NULL;
	size_t size = 0;
	unsigned long base = -1;
	unsigned long vbase = -1;
	unsigned int i;

	if (elf_length < sizeof(struct elf_header)) {
		if (verbose)
			printf("%s: truncated ELF image\n", __func__);
		return NULL;
	}

	if (elf_length < hdr->phoff + (hdr->phnum * sizeof(struct program_header))) {
		if (verbose)
			printf("%s: truncated ELF image\n", __func__);
		return NULL;
	}

	/* We only support 32-bit for now */
	if (hdr->ident[EI_CLASS] != ELFCLASS32) {
		if (verbose)
			printf("%s: only 32-bit ELF images are supported\n", __func__);
		return NULL;
	}

	/* ePAPR only supports big-endian ELF images */
	if (hdr->ident[EI_DATA] != ELFDATA2MSB) {
		if (verbose)
			printf("%s: only big-endian ELF images are supported\n", __func__);
		return NULL;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base address is the smallest paddr of all PT_LOAD
	 * segments.
	 */
	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].offset + phdr[i].filesz > elf_length) {
			if (verbose)
				printf("%s: truncated ELF image\n", __func__);
			return NULL;
		}
		if (phdr[i].filesz > phdr[i].memsz) {
			if (verbose)
				printf("%s: invalid ELF program header file size\n", __func__);
			return NULL;
		}
		if (phdr[i].type == PT_LOAD) {
			if (phdr[i].paddr < base)
				base = phdr[i].paddr;
			if (phdr[i].vaddr < vbase)
				vbase = phdr[i].vaddr;
		}
	}

	/* Copy each PT_LOAD segment to memory */

	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].type == PT_LOAD) {
			size_t newsize = (phdr[i].paddr - base) + phdr[i].memsz;

			if (size < newsize) {
				buffer = realloc(buffer, newsize);
				size = newsize;
			}

			memcpy(buffer + (phdr[i].paddr - base), elf + phdr[i].offset, phdr[i].filesz);
			memset(buffer + (phdr[i].paddr - base) + phdr[i].filesz, 0, phdr[i].memsz - phdr[i].filesz);
		}
	}

	if (verbose) {
		printf("%s: load address is 0x%lx\n", __func__, base);
		printf("%s: virtual base address is 0x%lx\n", __func__, vbase);
		printf("%s: entry offset is 0x%lx\n", __func__, hdr->entry - vbase);
		printf("%s: image size is 0x%x\n", __func__, size);
	}

	if (load_address)
		*load_address = base;

	if (entry_offset)
		*entry_offset = hdr->entry - vbase;

	if (bin_length)
		*bin_length = size;

	return buffer;
}

/**
 * Read a device tree property file into memory
 */
static void *read_property(const char *filename, size_t *size)
{
	off_t off;
	int f;
	void *buffer = NULL;
	struct stat buf;

	f = open(filename, O_RDONLY);
	if (f == -1) {
		if (verbose)
			perror(__func__);
		return NULL;
	}

	if (fstat(f, &buf)) {
		if (verbose)
			perror(__func__);
		goto fail;
	}

	off = buf.st_size;
	if (!off) {
		if (verbose)
			printf("%s: device tree property file %s is zero bytes long\n",
				__func__, filename);
		goto fail;
	}

	buffer = malloc(off);
	if (!buffer) {
		if (verbose)
			perror(__func__);
		goto fail;
	}

	if (read(f, buffer, off) != off) {
		if (verbose)
			perror(__func__);
		goto fail;
	}

	if (size)
		*size = off;

	close(f);
	return buffer;

fail:
	free(buffer);
	close(f);

	return NULL;
}

/**
 * Load an image file into memory and parse it if it's an ELF
 *
 * @filename: file name of image
 * @size: returned size of loaded image
 * @load_address: pointer to returned load address
 * @entry_offset: pointer to returned offset of entry address within buffer
 *
 * Returns a pointer to a buffer if success.  The buffer must be released
 * with free().
 *
 * Returns NULL if there was an error
 */
static void *load_image_file(const char *filename, size_t *size,
	unsigned long *load_address, unsigned long *entry_offset)
{
	off_t off;
	int f;
	void *mapped = MAP_FAILED;
	void *buffer = NULL;
	struct stat buf;

	f = open(filename, O_RDONLY);
	if (f == -1) {
		if (verbose)
			perror(__func__);
		return NULL;
	}

	if (fstat(f, &buf)) {
		if (verbose)
			perror(__func__);
		goto fail;
	}

	off = buf.st_size;
	if (off < 4) {
		if (verbose)
			printf("%s: file %s is too small\n", __func__, filename);
		goto fail;
	}

	mapped = mmap(0, off, PROT_READ, MAP_SHARED, f, 0);
	if (mapped == MAP_FAILED) {
		if (verbose)
			perror(__func__);
		goto fail;
	}

	if (memcmp(mapped, "\177ELF", 4) == 0) {
		buffer = parse_elf(mapped, off, size, load_address, entry_offset);
	} else {
		buffer = malloc(off);
		if (!buffer) {
			if (verbose)
				perror(__func__);
			goto fail;
		}
		memcpy(buffer, mapped, off);

		if (size)
			*size = off;
		if (load_address)
			*load_address = 0;
		if (entry_offset)
			*entry_offset = 0;
	}

	munmap(mapped, off);
	close(f);

	return buffer;

fail:
	if (mapped != MAP_FAILED)
		munmap(mapped, off);

	free(buffer);
	close(f);

	return NULL;
}

/**
 * Returns non-zero if the property is compatible
 *
 * @haystack - pointer to NULL-terminated strings that contain the value of
 *           the 'compatible' property to test
 * @length - length of 'haystack'
 * @needle - compatible string to search for
 */
static int is_compatible(const char *haystack, size_t length, const char *needle)
{
	while (length > 0) {
		int l;

		if (strcmp(haystack, needle) == 0)
			return 1;
		l = strlen(haystack) + 1;
		haystack += l;
		length -= l;
	}

	return 0;
}

/* Command parsers */

static const char handle_path[] = "/proc/device-tree/hypervisor/handles";
/**
 * Displays a list of partitions and their status
 */
static int cmd_status(void)
{
	DIR *dir;
	struct dirent *dp;
	int ret = 0;

	printf("Partition status\n\n");

	if (chdir(handle_path)) {
		printf("Cannot find %s path\n", handle_path);
		if (verbose)
			perror(__func__);
		return errno;
	}

	dir = opendir(".");
	if (!dir) {
		printf("Cannot open directory %s\n", handle_path);
		if (verbose)
			perror(__func__);
		return errno;
	}

	printf("Partition Name                  Handle  Status\n"
	       "----------------------------------------------\n");

	while ((dp = readdir(dir)))
		if ((dp->d_type == DT_DIR) && (dp->d_name[0] != '.')) {
			size_t size;
			char filename[300];
			char *strprop;
			uint32_t *reg;
			struct fsl_hv_ioctl_status status;

			snprintf(filename, sizeof(filename) - 1, "%s/compatible", dp->d_name);
			strprop = read_property(filename, &size);
			if (!strprop) {
				printf("Could not read property %s\n", filename);
				goto exit;
			}
			if (!is_compatible(strprop, size, "fsl,hv-partition-handle"))
				continue;
			free(strprop);

			sprintf(filename, "%s/reg", dp->d_name);
			reg = read_property(filename, &size);
			if (!reg) {
				printf("%s: 'reg' property missing\n", __func__);
				goto exit;
			}
			if (size != sizeof(uint32_t)) {
				printf("%s: 'reg' property is wrong size\n", __func__);
				goto exit;
			}

			sprintf(filename, "%s/label", dp->d_name);
			strprop = read_property(filename, &size);
			if (!strprop)
				goto exit;

			strprop[size - 1] = 0;
			printf("%-31s %-7u ", strprop, *reg);
			free(strprop);

			status.partition = *reg;
			ret = hv(FSL_HV_IOCTL_PARTITION_GET_STATUS, (void *) &status);
			if (ret) {
				printf("error %i\n", ret);
				ret = 0;
			} else {
				switch (status.status) {
				case 0: printf("stopped\n"); break;
				case 1: printf("running\n"); break;
				case 2: printf("starting\n"); break;
				case 3: printf("stopping\n"); break;
				default: printf("unknown %i\n", status.status); break;
				}
			}
			free(reg);
		}

exit:
	closedir(dir);

	return ret;
}

/**
 * Load an ELF or binary file into another partition
 */
static int cmd_load_image(struct parameters *p)
{
	void *image;
	size_t filesize;
	unsigned long load_address;
	unsigned long entry_offset;
	int ret = 0;

	if (!p->h_specified || !p->f_specified) {
		usage();
		return EINVAL;
	}

	printf("Loading file %s\n", p->f);

	image = load_image_file(p->f, &filesize, &load_address, &entry_offset);
	if (!image) {
		printf("Could not load file %s\n", p->f);
		return EIO;
	}

	if (!p->a_specified)
		p->a = load_address;

	printf("Copying image to partition %i at address 0x%lx\n", p->h, p->a);

	ret = copy_to_partition(p->h, image, p->a, filesize);

	free(image);

	if (ret)
		printf("Copy failed with error %i\n", ret);
	else
		printf("(Entry address is 0x%lx)\n", load_address + entry_offset);

	return ret;
}

static int cmd_start(struct parameters *p)
{
	struct fsl_hv_ioctl_start im;
	int ret;

	if (!p->h_specified) {
		usage();
		return EINVAL;
	}

	printf("Starting partition %i\n", p->h);

	if (!p->e_specified)
		p->e = 0;

	if (!p->a_specified)
		p->a = 0;

	if (p->f_specified) {
		void *image;
		size_t filesize;
		unsigned long load_address;
		unsigned long entry_offset;
		int ret = 0;

		printf("Loading file %s\n", p->f);

		image = load_image_file(p->f, &filesize, &load_address, &entry_offset);
		if (!image)
			return EIO;

		if (!p->a_specified)
			p->a = load_address;

		if (!p->e_specified)
			p->e = p->a + entry_offset;

		printf("Copying image to partition at address 0x%lx\n", p->a);

		ret = copy_to_partition(p->h, image, p->a, filesize);

		free(image);
		if (ret) {
			printf("Could not copy file %s to partition %i, error=%i\n",
				p->f, p->h, ret);
			return ret;
		}
	}

	im.partition = p->h;
	im.entry_point = p->e;

	printf("Starting partition at entry point 0x%lx\n", p->e);

	ret = hv(FSL_HV_IOCTL_PARTITION_START, (void *) &im);
	if (ret)
		printf("Hypervisor returned error %i\n", ret);

	return ret;
}

static int cmd_stop(struct parameters *p)
{
	struct fsl_hv_ioctl_stop im;
	int ret;

	if (!p->h_specified) {
		usage();
		return EINVAL;
	}

	printf("Stopping partition %i\n", p->h);

	im.partition = p->h;

	ret = hv(FSL_HV_IOCTL_PARTITION_STOP, (void *) &im);
	if (ret)
		printf("Hypervisor returned error %i\n", ret);

	return ret;
}

static int cmd_restart(struct parameters *p)
{
	struct fsl_hv_ioctl_restart im;
	int ret;

	if (!p->h_specified) {
		usage();
		return EINVAL;
	}

	printf("Restarting partition %i\n", p->h);

	if (p->f_specified) {
		void *image;
		size_t filesize;
		unsigned long load_address;
		unsigned long entry;
		int ret = 0;

		printf("Loading image %s\n", p->f);

		image = load_image_file(p->f, &filesize, &load_address, &entry);
		if (!image)
			return EIO;

		if (!p->a_specified)
			p->a = load_address;

		if (!p->e_specified)
			p->e = entry;

		printf("Copying image to partition at address 0x%lx\n", p->a);

		ret = copy_to_partition(p->h, image, p->a, filesize);

		free(image);
		if (ret) {
			printf("Could not copy file %s to partition %i, error=%i\n",
				p->f, p->h, ret);
			return ret;
		}
	}

	im.partition = p->h;

	printf("Restarting partition\n");

	ret = hv(FSL_HV_IOCTL_PARTITION_RESTART, (void *) &im);
	if (ret)
		printf("Hypervisor returned error %i\n", ret);

	return ret;
}

int main(int argc, char *argv[])
{
	enum command cmd = e_none;
	struct parameters p;
	int c;
	int ret = 0;

	memset(&p, 0, sizeof(p));

	opterr = 0;

	while ((c = getopt(argc, argv, "slgxvh:f:a:e:")) != -1) {
		switch (c) {
		case 's':
			cmd = e_status;
			break;
		case 'l':
			cmd = e_load;
			break;
		case 'g':
			cmd = e_start;
			break;
		case 'x':
			cmd = e_stop;
			break;
		case 'r':
			cmd = e_restart;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
			p.h = strtol(optarg, NULL, 0);
			p.h_specified = 1;
			break;
		case 'f':
			p.f = optarg;
			p.f_specified = 1;
			break;
		case 'a':
			p.a = strtol(optarg, NULL, 0);
			p.a_specified = 1;
			break;
		case 'e':
			p.e = strtol(optarg, NULL, 0);
			p.e_specified = 1;
			break;
		case '?':
			break;
		default:
			usage();
			return 0;
		}
	}

	switch (cmd) {
	case e_status:
		ret = cmd_status();
		break;
	case e_load:
		ret = cmd_load_image(&p);
		break;
	case e_start:
		ret = cmd_start(&p);
		break;
	case e_stop:
		ret = cmd_stop(&p);
		break;
	case e_restart:
		ret = cmd_restart(&p);
		break;
	default:
		usage();
		break;
	}

	return ret ? 1 : 0;
}

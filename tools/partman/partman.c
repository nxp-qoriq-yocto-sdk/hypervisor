
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
#include <sys/types.h>
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
	e_restart,
	e_doorbell,
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

	printf("Monitor doorbells:\n\tpartman -d -f <file>\n");
	printf("\t\t<file> is a shell script or program to run on every doorbell.\n");
	printf("\t\tThe first parameter to the script is the doorbell handle.\n\n");

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
 * Parse an ELF image, and copy the program headers into the target guest
 *
 * @partition: the partition handle to copy to
 * @elf: Pointer to the ELF image
 * @elf_length: length of the ELF image, or -1 to ignore length checking
 * @bin_length: pointer to returned buffer length
 * @load_address: load address, or -1 to use ELF load address
 * @entry_address: pointer to returned entry address
 *
 * Returns 0 for failure or non-zero for success
 */
static int parse_and_copy_elf(unsigned int partition, void *elf,
			      size_t elf_length, unsigned long load_address,
			      unsigned long *entry_address)
{
	struct elf_header *hdr = (struct elf_header *) elf;
	struct program_header *phdr = (struct program_header *) (elf + hdr->phoff);
	unsigned long base = -1;
	unsigned long vbase = -1;
	unsigned int i;

	if (elf_length < sizeof(struct elf_header)) {
		if (verbose)
			printf("%s: truncated ELF image\n", __func__);
		return 0;
	}

	if (elf_length < hdr->phoff + (hdr->phnum * sizeof(struct program_header))) {
		if (verbose)
			printf("%s: truncated ELF image\n", __func__);
		return 0;
	}

	/* We only support 32-bit for now */
	if (hdr->ident[EI_CLASS] != ELFCLASS32) {
		if (verbose)
			printf("%s: only 32-bit ELF images are supported\n", __func__);
		return 0;
	}

	/* ePAPR only supports big-endian ELF images */
	if (hdr->ident[EI_DATA] != ELFDATA2MSB) {
		if (verbose)
			printf("%s: only big-endian ELF images are supported\n", __func__);
		return 0;
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
			return 0;
		}
		if (phdr[i].filesz > phdr[i].memsz) {
			if (verbose)
				printf("%s: invalid ELF program header file size\n", __func__);
			return 0;
		}
		if (phdr[i].type == PT_LOAD) {
			if (phdr[i].paddr < base)
				base = phdr[i].paddr;
			if (phdr[i].vaddr < vbase)
				vbase = phdr[i].vaddr;
		}
	}

	/* If the user did not specify -a, then we use the real paddr values
	 * in the ELF image.
	 */
	if (load_address == (unsigned long) -1)
		load_address = base;

	/* Copy each PT_LOAD segment to the target partition */

	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].type == PT_LOAD) {
			unsigned long target_addr = load_address + (phdr[i].paddr - base);
			int ret;

			if (verbose)
				printf("%s: copying 0x%x bytes from ELF image offset 0x%x to guest physical address 0x%lx\n",
				       __func__, phdr[i].filesz, phdr[i].offset, target_addr);
			ret = copy_to_partition(partition, elf + phdr[i].offset, target_addr, phdr[i].filesz);
			if (ret) {
				if (verbose)
					printf("Copy failed, error=%i\n", ret);
				return 0;
			}

			if (phdr[i].memsz > phdr[i].filesz) {
				void *buffer;

				if (verbose)
					printf("%s: writing 0x%x null bytes to guest physical address 0x%lx\n",
					       __func__, phdr[i].memsz - phdr[i].filesz, target_addr + phdr[i].filesz);

				buffer = malloc(phdr[i].memsz - phdr[i].filesz);
				if (!buffer) {
					if (verbose)
						printf("could not allocate %u bytes\n",
						       phdr[i].memsz - phdr[i].filesz);
					return 0;
				}
				memset(buffer, 0, phdr[i].memsz - phdr[i].filesz);
				ret = copy_to_partition(partition, buffer,
                                                        target_addr + phdr[i].filesz,
                                                        phdr[i].memsz - phdr[i].filesz);
				free(buffer);
				if (ret) {
					if (verbose)
						printf("Copy failed, error=%i\n", ret);
					return 0;
				}
			}
		}
	}

	if (verbose) {
		printf("%s: load address is 0x%lx\n", __func__, load_address);
		printf("%s: entry address is 0x%lx\n", __func__,
		       load_address + (hdr->entry - vbase));
	}

	if (entry_address)
		*entry_address = load_address + (hdr->entry - vbase);

	return 1;
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
		if (verbose)
			perror(__func__);
		return 0;
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
		ret = parse_and_copy_elf(partition, mapped, off, load_address, entry_address);
		if (!ret) {
			if (verbose)
				printf("Could not load file %s to partition %i at address %lx, error=%i\n",
				       filename, partition, load_address, ret);
			goto fail;
		}
	} else {
		// If -a was not specified, then assume 0
		if (load_address == (unsigned long) -1)
			load_address = 0;

		ret = copy_to_partition(partition, mapped, load_address, off);
		if (ret) {
			if (verbose)
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
	unsigned long load_address;
	unsigned long entry_address;
	int ret = 0;

	if (!p->h_specified || !p->f_specified) {
		usage();
		return EINVAL;
	}

	printf("Loading file %s\n", p->f);

	load_address = p->a_specified ? p->a : (unsigned long) -1;

	ret = load_and_copy_image_file(p->h, p->f, load_address, &entry_address);
	if (!ret) {
		printf("Could not load and copy file %s\n", p->f);
		return EIO;
	}

	printf("(Entry address is 0x%lx)\n", entry_address);

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
		unsigned long load_address;
		unsigned long entry_address;
		int ret = 0;

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
		unsigned long load_address;
		unsigned long entry_address;
		int ret = 0;

		printf("Loading and copying image %s\n", p->f);

		load_address = p->a_specified ? p->a : (unsigned long) -1;

		ret = load_and_copy_image_file(p->h, p->f, load_address, &entry_address);
		if (!ret)
			return EIO;
	}

	// FIXME: restart partition should take an entry address
	im.partition = p->h;

	printf("Restarting partition\n");

	ret = hv(FSL_HV_IOCTL_PARTITION_RESTART, (void *) &im);
	if (ret)
		printf("Hypervisor returned error %i\n", ret);

	return ret;
}

static int cmd_doorbells(struct parameters *p)
{
	uint32_t dbell;
	ssize_t ret;
	char command[PATH_MAX + 32];
	char temp[PATH_MAX + 1];

	if (!p->f_specified) {
		usage();
		return EINVAL;
	}

	// Verify that the passed-in script exists and can actually be
	// executed.
	snprintf(command, sizeof(command), "test -x %s", p->f);
	if (system(command)) {
		printf("%s is not a executable file\n", p->f);
		return EINVAL;
	}

	int f = open("/dev/fsl-hv", O_RDONLY);
	if (f == -1) {
		ret = errno;
		if (verbose)
			perror(__func__);
		return ret;
	}

	while (1) {
		ret = read(f, &dbell, sizeof(dbell));
		if (ret <= 0) {
			if (verbose)
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
		printf("Freescale HV management driver has not been loaded.\n");
		printf("Please load the driver or rebuild the kernel with this driver enabled.\n");
		return 0;
	}
	ret = fscanf(f, "%u:%u", &major, &minor);
	if ((ret != 2) || (major == 0)) {
		printf("Could not read the sysfs entry for the HV management driver.\n");
		return 0;
	}
	fclose(f);

	if (stat("/dev/fsl-hv", &statbuf))
	{
		switch (errno) {
		case EACCES:
			printf("Permissions on /dev/fsl-hv are incorrect.  As root, try this:\n");
			printf("chmod a+rw /dev/fsl-hv\n");
			printf("and try partman again\n");
			return 0;
		case ENOENT:
			printf("/dev/fsl-hv does not exist.  Please check your root file system.\n");
			printf("As a temporary fix, do this as root:\n");
			printf("mknod /dev/fsl-hv c %u %u\n", major, minor);
			return 0;
		default:
			printf("Something is wrong with /dev/fsl-hv (errno=%i).\n", errno);
			return 0;
		}
	}

	if ((statbuf.st_mode & S_IFMT) != S_IFCHR) {
		printf("/dev/fsl-hv is not a character device.  Please check your root file system.\n");
		printf("As a temporary fix, do this as root:\n");
		printf("rm -f /dev/fsl-hv\n");
		printf("mknod /dev/fsl-hv c %u %u\n", major, minor);
		return 0;
	}

	if ((major(statbuf.st_rdev) != major) || (minor(statbuf.st_rdev) != minor)) {
		printf("Major and/or minor number of /dev/fsl-hv are wrong.\n");
		printf("Please check your root file system.  As a temporary fix, do this as root:\n");
		printf("rm -f /dev/fsl-hv\n");
		printf("mknod /dev/fsl-hv c %u %u\n", major, minor);
		return 0;
	}

	return 1;
}

int main(int argc, char *argv[])
{
	enum command cmd = e_none;
	struct parameters p;
	int c;
	int ret = 0;

	if (!verify_dev())
		return 1;

	memset(&p, 0, sizeof(p));

	opterr = 0;

	while ((c = getopt(argc, argv, "slgxdvh:f:a:e:")) != -1) {
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
		case 'd':
			cmd = e_doorbell;
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
	case e_doorbell:
		ret = cmd_doorbells(&p);
		break;
	default:
		usage();
		break;
	}

	return ret ? 1 : 0;
}

/**
 * Sample Linux partition management utility
 *
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

#include <fsl_hypervisor.h>

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
	printf("\t\tDefault value for -a is 0, or the base physical address for ELF images.\n\n");

	printf("Start partition:\n\tpartman -g -h <handle> [-f <file>] [-e <address>] [-a <address>]\n");
	printf("\t\tDefault value for -a is 0, or the base physical address for ELF images.\n");
	printf("\t\tDefault value for -e is 0, or the entry point for ELF images.\n\n");

	printf("Stop partition:\n\tpartman -x -h <handle>\n\n");

	printf("Restart partition:\n\tpartman -r -h <handle> [-f <file>] [-e <address>] [-a <address>]\n");
	printf("\t\tDefault value for -a is 0, or the base physical address for ELF images.\n");
	printf("\t\tDefault value for -e is 0, or the entry point for ELF images.\n");
}

/**
 * Call the hypervisor management driver
 */
static int hv(unsigned int cmd, union fsl_hv_ioctl_param *p)
{
	int ret = 0;

	int f = open("/dev/fsl-hv", O_RDWR | O_SYNC);
	if (f == -1) {
		ret = errno;
		perror(__func__);
		return ret;
	}

	if (ioctl(f, cmd, p) == -1) {
		ret = errno;
		perror(__func__);
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
 * @entry: pointer to returned offset of entry address within buffer
 */
static void *parse_elf(void *elf, size_t elf_length, size_t *bin_length,
	unsigned long *load_address, unsigned long *entry)
{
	struct elf_header *hdr = (struct elf_header *) elf;
	struct program_header *phdr = (struct program_header *) (elf + hdr->phoff);
	void *buffer = NULL;
	size_t size = 0;
	unsigned long base = -1;
	unsigned int i;

	if (elf_length < sizeof(struct elf_header)) {
		printf("Truncated ELF image\n");
		return NULL;
	}

	if (elf_length < hdr->phoff + (hdr->phnum * sizeof(struct program_header))) {
		printf("Truncated ELF image\n");
		return NULL;
	}

	/* We only support 32-bit for now */
	if (hdr->ident[EI_CLASS] != ELFCLASS32) {
		printf("Only 32-bit ELF images are supported\n");
		return NULL;
	}

	/* ePAPR only supports big-endian ELF images */
	if (hdr->ident[EI_DATA] != ELFDATA2MSB) {
		printf("Only big-endian ELF images are supported\n");
		return NULL;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base address is the smallest paddr of all PT_LOAD
	 * segments.
	 */
	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].offset + phdr[i].filesz > elf_length) {
			printf("Truncated ELF image\n");
			return NULL;
		}
		if (phdr[i].filesz > phdr[i].memsz) {
			printf("Invalid ELF program header file size\n");
			return NULL;
		}
		if ((phdr[i].type == PT_LOAD) && (phdr[i].paddr < base))
			base = phdr[i].paddr;
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

	if (load_address)
		*load_address = base;

	if (entry)
		*entry = hdr->entry - base;

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
		perror(__func__);
		return NULL;
	}

	if (fstat(f, &buf)) {
		perror(__func__);
		goto fail;
	}

	off = buf.st_size;
	if (!off) {
		printf("%s: file %s is zero bytes long\n", __func__, filename);
		goto fail;
	}

	buffer = malloc(off);
	if (!buffer) {
		perror(__func__);
		goto fail;
	}

	if (read(f, buffer, off) != off) {
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
 * Returns a pointer to a buffer if success.  The buffer must be released
 * with free().
 *
 * Returns NULL if there was an error
 */
static void *load_image_file(const char *filename, size_t *size,
	unsigned long *load_address, unsigned long *entry)
{
	off_t off;
	int f;
	void *mapped = NULL;
	void *buffer = NULL;
	struct stat buf;

	f = open(filename, O_RDONLY);
	if (f == -1) {
		perror(__func__);
		return NULL;
	}

	if (fstat(f, &buf)) {
		perror(__func__);
		goto fail;
	}

	off = buf.st_size;
	if (off < 4) {
		printf("%s: file %s is too small\n", __func__, filename);
		goto fail;
	}

	mapped = mmap(0, off, PROT_READ, MAP_SHARED, f, 0);
	if (!mapped) {
		perror(__func__);
		goto fail;
	}

	if (memcmp(mapped, "\177ELF", 4) == 0) {
		buffer = parse_elf(mapped, off, size, load_address, entry);
	} else {
		buffer = malloc(off);
		if (!buffer) {
			perror(__func__);
			goto fail;
		}
		memcpy(buffer, mapped, off);

		if (size)
			*size = off;
		if (load_address)
			*load_address = 0;
		if (entry)
			*entry = 0;
	}

	munmap(mapped, off);
	close(f);

	return buffer;

fail:
	if (mapped)
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

/**
 * Displays a list of partitions and their status
 */
static int cmd_status(void)
{
	DIR *dir;
	struct dirent *dp;
	int ret = 0;

	if (chdir("/proc/device-tree/hypervisor/handles")) {
		perror(__func__);
		return errno;
	}

	dir = opendir(".");
	if (!dir) {
		perror(__func__);
		return errno;
	}

	printf("Partition\n"
	       "handle              Status\n"
	       "---------------------------------\n");

	while ((dp = readdir(dir)))
		if ((dp->d_type == DT_DIR) && (dp->d_name[0] != '.')) {
			size_t size;
			char filename[300];
			char *compatible;
			uint32_t *reg;

			sprintf(filename, "%s/compatible", dp->d_name);
			compatible = read_property(filename, &size);
			if (!compatible)
				goto exit;
			if (!is_compatible(compatible, size, "fsl,hv-partition-handle"))
				continue;
			free(compatible);

			sprintf(filename, "%s/reg", dp->d_name);
			reg = read_property(filename, &size);
			if (!reg)
				goto exit;
			if (size != sizeof(uint32_t)) {
				printf("%s: 'reg' property is wrong size\n", __func__);
				goto exit;
			}

			printf("  %2u                unknown\n", *reg);
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
	int ret = 0;

	if (!p->h_specified || !p->f_specified) {
		usage();
		ret = EINVAL;
		goto exit;
	}

	image = load_image_file(p->f, &filesize, &load_address, NULL);
	if (!image) {
		ret = EIO;
		goto exit;
	}

	if (!p->a_specified)
		p->a = load_address;

	ret = copy_to_partition(p->h, image, p->a, filesize);

exit:
	free(image);

	return ret;
}

static int cmd_start(struct parameters *p)
{
	struct fsl_hv_ioctl_start im;

	if (!p->h_specified) {
		usage();
		return EINVAL;
	}

	if (p->f_specified) {
		void *image;
		size_t filesize;
		unsigned long load_address;
		unsigned long entry;
		int ret = 0;

		image = load_image_file(p->f, &filesize, &load_address, &entry);
		if (!image)
			return EIO;

		if (!p->a_specified)
			p->a = load_address;

		if (!p->e_specified)
			p->e = entry;

		ret = copy_to_partition(p->h, image, p->a, filesize);

		free(image);
		if (ret)
			return ret;
	}

	if (!p->e_specified) {
		usage();
		return EINVAL;
	}

	im.partition = p->h;
	im.entry_point = p->e;
	im.device_tree = -1;

	return hv(FSL_HV_IOCTL_PARTITION_START, (void *) &im);
}

static int cmd_stop(struct parameters *p)
{
	struct fsl_hv_ioctl_stop im;

	if (!p->h_specified) {
		usage();
		return EINVAL;
	}

	im.partition = p->h;

	return hv(FSL_HV_IOCTL_PARTITION_STOP, (void *) &im);
}

static int cmd_restart(struct parameters *p)
{
	struct fsl_hv_ioctl_restart im;

	if (!p->h_specified) {
		usage();
		return EINVAL;
	}

	if (p->f_specified) {
		void *image;
		size_t filesize;
		unsigned long load_address;
		unsigned long entry;
		int ret = 0;

		image = load_image_file(p->f, &filesize, &load_address, &entry);
		if (!image)
			return EIO;

		if (!p->a_specified)
			p->a = load_address;

		if (!p->e_specified)
			p->e = entry;

		ret = copy_to_partition(p->h, image, p->a, filesize);

		free(image);
		if (ret)
			return ret;
	}

	im.partition = p->h;

	return hv(FSL_HV_IOCTL_PARTITION_RESTART, (void *) &im);
}

int main(int argc, char *argv[])
{
	enum command cmd = e_none;
	struct parameters p;
	int c;

	memset(&p, 0, sizeof(p));

	opterr = 0;

	while ((c = getopt(argc, argv, "slgxrh:f:a:e:")) != -1) {
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
		return cmd_status();
	case e_load:
		return cmd_load_image(&p);
	case e_start:
		return cmd_start(&p);
	case e_stop:
		return cmd_stop(&p);
	case e_restart:
		return cmd_restart(&p);
	default:
		usage();
		break;
	}

	return 0;
}

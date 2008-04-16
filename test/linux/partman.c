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

static void usage(void)
{
	printf("Usage:\n\n");

	printf("Show partition status:\n\tpartman -s\n\n");

	printf("Load image:\n\tpartman -l -h <handle> -f <file> [-a <address>]\n");
	printf("\t\t-a is optional for ELF images only\n\n");

	printf("Start partition:\n\tpartman -g -h <handle>\n\n");

	printf("Stop partition:\n\tpartman -x -h <handle>\n\n");

	printf("Restart partition:\n\tpartman -r -h <handle>\n");
}

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

static int zero_to_partition(unsigned int partition,
	unsigned long target, size_t count)
{
	struct fsl_hv_ioctl_memcpy im;
	void *buffer;
	int ret = 0;

	if (!count)
		return 0;

	buffer = calloc(count, 1);
	if (!buffer) {
		perror(__func__);
		return errno;
	}

	im.source = -1;
	im.target = partition;
	im.local_vaddr = (__u64) (unsigned long) buffer;
	im.remote_paddr = target;
	im.count = count;

	ret = hv(FSL_HV_IOCTL_MEMCPY, (void *) &im);

	free(buffer);

	return ret;
}

/**
 * Parse an ELF image and load the segments into guest memory
 *
 * @image: Pointer to the ELF image
 * @length: length of the ELF image, or -1 to ignore length checking
 * @address: guest physical address
 *
 */
static int write_elf(unsigned int partition, void *image, unsigned long length,
	unsigned long address)
{
	struct elf_header *hdr = (struct elf_header *) image;
	struct program_header *phdr = (struct program_header *) (image + hdr->phoff);
	unsigned long base = -1;
	unsigned int i;
	int ret;

	if (length < sizeof(struct elf_header)) {
		printf("Truncated ELF image\n");
		return EINVAL;
	}

	if (length < hdr->phoff + (hdr->phnum * sizeof(struct program_header))) {
		printf("Truncated ELF image\n");
		return EINVAL;
	}

	/* We only support 32-bit for now */
	if (hdr->ident[EI_CLASS] != ELFCLASS32) {
		printf("Only 32-bit ELF images are supported\n");
		return EINVAL;
	}

	/* ePAPR only supports big-endian ELF images */
	if (hdr->ident[EI_DATA] != ELFDATA2MSB) {
		printf("Only big-endian ELF images are supported\n");
		return EINVAL;
	}

	/*
	 * Test the program segment sizes and find the base address at the
	 * same time.  The base address is the smallest vaddr of all PT_LOAD
	 * segments.
	 */
	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].offset + phdr[i].filesz > length) {
			printf("Truncated ELF image\n");
			return EINVAL;
		}
		if (phdr[i].filesz > phdr[i].memsz) {
			printf("Invalid ELF program header file size\n");
			return EINVAL;
		}
		if ((phdr[i].type == PT_LOAD) && (phdr[i].paddr < base))
			base = phdr[i].paddr;
	}

	/* If the address wasn't specified on the command-line, then just use
	   the actual paddr in the ELF image. */
	if (address == -1)
		address = base;

	/* Copy each PT_LOAD segment to memory */

	for (i = 0; i < hdr->phnum; i++) {
		if (phdr[i].type == PT_LOAD) {
			ret = copy_to_partition(partition,
				image + phdr[i].offset,
				address + (phdr[i].paddr - base),
				phdr[i].filesz);
			if (ret)
				return ret;

			ret = zero_to_partition(partition,
				address + (phdr[i].paddr - base) + phdr[i].filesz,
				phdr[i].memsz - phdr[i].filesz);
			if (ret)
				return ret;
		}
	}

	printf("Entry point is 0x%lx\n", address + base - hdr->entry);

	return 0;
}

static void *read_file(const char *filename, size_t *size)
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
/**
 * Partition
 * handle              Status
 * ---------------------------------
 *    3                running
 *    5                stopped
 *    6                running
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
			compatible = read_file(filename, &size);
			if (!compatible)
				goto exit;
			if (!is_compatible(compatible, size, "fsl,hv-partition"))
				continue;
			free(compatible);

			sprintf(filename, "%s/reg", dp->d_name);
			reg = read_file(filename, &size);
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

/*
 *
 * Image loading
 * -------------
 * partman -l -h <handle> -f <file> [-a <phys address>]
 *
 * For ELF images, -a is optional.  If omitted, the base address specified
 * in the image is used.
 *
 * For ELF images, partman reports the entry point with a status message.
 *
 * Example
 * To load an image at physical 0x0:
 *   $ partman -l -h 5 -f vmlinux.elf -a 0x0
 *     Loaded vmlinux.elf...entry point is 0x1000.
 *
 * Example
 * To load an image at physical 0x01000000:
 *
 *   $ partman -l -h 5 -f rootfs.ext2.gz -a 0x01000000
 *     Loaded rootfs.ext2.gz.
*/
static int cmd_load_image(struct parameters *p)
{
	void *image;
	int f = 0;
	size_t filesize;
	int ret = 0;

	/* Check parameters */
	if (!p->h_specified || !p->f_specified) {
		usage();
		ret = EINVAL;
		goto exit;
	}

	image = read_file(p->f, &filesize);
	if (!image) {
		ret = EIO;
		goto exit;
	}
	if (filesize < 4) {
		printf("File %s too small\n", p->f);
		ret = EPERM;
		goto exit;
	}

	if (memcmp(image, "\177ELF", 4) == 0) {
		ret = write_elf(p->h, image, filesize, p->a_specified ? p->a : -1);
		goto exit;
	}

	/* Binary file, copy it directly to target */

	if (!p->a_specified) {
		usage();
		ret = EINVAL;
		goto exit;
	}

	ret = copy_to_partition(p->h, image, p->a, filesize);
exit:
	if (f)
		close(f);

	free(image);

	return ret;
}

static int cmd_start(struct parameters *p)
{
	struct fsl_hv_ioctl_start im;

	if (!p->h_specified || !p->e_specified) {
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
			cmd = e_load;
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

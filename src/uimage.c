/** @file
 * uImage parser
 *
 * This file contains functions that will parse and extract the executable
 * binary, generated using mkimage tool.
 */
/* Copyright (C) 2008 Freescale Semiconductor, Inc.
 * Author: Naveen Burmi <naveenburmi@freescale.com>
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

#include <libos/endian.h>
#include <percpu.h>
#include <errors.h>
#include <devtree.h>

#define UIMAGE_SIGNATURE 0x27051956

#define IMAGE_TYPE_INVALID      0
#define IMAGE_TYPE_STANDALONE   1
#define IMAGE_TYPE_KERNEL       2
#define IMAGE_TYPE_RAMDISK      3

/*
 * uimage header
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
};

/**
 * Add linux,initrd-start and linux,initrd-end properties into chosen
 * node of guest device tree.
 *
 * @initrd_start: guest physical start address of initrd image.
 * @initrd_end: guest physical end address of initrd image.
 */
static int add_initrd_start_end_to_guest_dt(guest_t *guest,
					    phys_addr_t initrd_start,
					    phys_addr_t initrd_end)
{
	dt_node_t *chosen;
	int ret = ERR_NOMEM;

	chosen = dt_get_subnode(guest->devtree, "chosen", 1);
	if (!chosen)
		goto err;

	ret = dt_set_prop(chosen, "linux,initrd-start",
	                  &initrd_start, sizeof(initrd_start));
	if (ret < 0)
		goto err;

	ret = dt_set_prop(chosen, "linux,initrd-end",
	                  &initrd_end, sizeof(initrd_end));
	if (ret < 0)
		goto err;

	return 0;

err:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: error %d\n", __func__, ret);
	return ret;
}

/**
 * Parse uImage, generated using mkimage and load it in guest memory
 *
 * @image: Pointer to the uimage, generated using mkimage
 * @length: length of the image, or -1 to ignore length checking
 * @target: guest physical address of the destination
 * @entry: guest phys address of entry point, if not NULL
 */
int load_uimage(guest_t *guest, phys_addr_t image_phys, unsigned long length,
                phys_addr_t target, register_t *entry)
{
	struct image_header hdr;
	uint32_t size;
	int ret;

	if (copy_from_phys(&hdr, image_phys, sizeof(hdr)) != sizeof(hdr))
		return ERR_UNHANDLED;

	if (cpu_from_be32(hdr.magic) != UIMAGE_SIGNATURE)
		return ERR_UNHANDLED;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "Loading uImage from flash\n");

	size = cpu_from_be32(hdr.size);
	if (length < size) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_uimage: image size exceeds target window\n");
		return ERR_BADIMAGE;
	}

	if (cpu_from_be32(hdr.type) == IMAGE_TYPE_RAMDISK) {
		ret = add_initrd_start_end_to_guest_dt(guest, target,
						       target + size);
		if (ret < 0)
			return ret;
	}

	ret = copy_phys_to_gphys(guest->gphys, target,
	                         image_phys + sizeof(hdr), size);
	if (ret != size) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_uimage: cannot copy\n");
		return ERR_BADADDR;
	}

	if (entry)
		*entry = target - cpu_from_be32(hdr.load) + cpu_from_be32(hdr.ep);

	return 0;
}

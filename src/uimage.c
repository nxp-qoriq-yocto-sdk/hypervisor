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

#include <percpu.h>
#include <errors.h>
#include <libfdt.h>
#include <libos/errors.h>
#include <libos/hcalls.h>

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
	uint8_t data[0];
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
	int chosen, ret;

	chosen = fdt_subnode_offset(guest->devtree, 0, "chosen");
	if (chosen < 0) {
		chosen = fdt_add_subnode(guest->devtree, 0, "chosen");
		if (chosen < 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				 "Couldn't create chosen node: %d\n", chosen);
			return chosen;
		}
	}

	ret = fdt_setprop(guest->devtree, chosen, "linux,initrd-start",
			  &initrd_start, sizeof(initrd_start));
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "Failed to set linux,initrd-start property in chosen"
			 " node: %d, error %d\n", chosen, ret);
		return ret;
	}

	ret = fdt_setprop(guest->devtree, chosen, "linux,initrd-end",
			  &initrd_end, sizeof(initrd_end));
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "Failed to set linux,initrd-end property in chosen "
			 "node: %d, error %d\n", chosen, ret);
		return ret;
	}

	return 0;
}

/**
 * Parse uImage, generated using mkimage and load it in guest memory
 *
 * @image: Pointer to the uimage, generated using mkimage
 * @length: length of the image, or -1 to ignore length checking
 * @target: guest physical address of the destination
 * @entry: guest phys address of entry point, if not NULL
 */
int load_uimage(guest_t *guest, void *image, unsigned long length,
		phys_addr_t target, register_t *entry)
{
	struct image_header *hdr;
	uint32_t size;
	int ret;

	hdr = (struct image_header *)image;

	if (be32_to_cpu(hdr->magic) != UIMAGE_SIGNATURE)
		return ERR_UNHANDLED;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
		 "guest %s: Loading uImage from flash\n", guest->name);

	if (length < sizeof(struct image_header)) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "guest %s: truncated image.\n", guest->name);
		return ERR_BADIMAGE;
	}

	size = be32_to_cpu(hdr->size);
	if (length < size) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "guest %s: image size specified in device tree is "
			 "less then size of image present in mkimage header.\n",
			 guest->name);
		return ERR_BADIMAGE;
	}

	if (be32_to_cpu(hdr->type) == IMAGE_TYPE_RAMDISK) {
		ret = add_initrd_start_end_to_guest_dt(guest, target,
						       target + size);
		if (ret < 0)
			return ret;
	}

	if (copy_to_gphys(guest->gphys, target, &hdr->data, size) != size)
		return ERR_BADADDR;

	if (entry)
		*entry = target - be32_to_cpu(hdr->load) + be32_to_cpu(hdr->ep);

	return 0;
}

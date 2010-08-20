/** @file
 * uImage parser
 *
 * This file contains functions that will parse and extract the executable
 * binary, generated using mkimage tool.
 */

/* Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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

#include <malloc.h>

#include <libos/endian.h>
#include <percpu.h>
#include <errors.h>
#include <devtree.h>
#include <uimage.h>
#include <zlib.h>

#define UIMAGE_SIGNATURE 0x27051956

#define IMAGE_TYPE_INVALID      0
#define IMAGE_TYPE_STANDALONE   1
#define IMAGE_TYPE_KERNEL       2
#define IMAGE_TYPE_RAMDISK      3

#ifdef CONFIG_ZLIB
#define GZIP_ID1       0x1f
#define GZIP_ID2       0x8b
#define GZIP_FHCRC     2
#define GZIP_FEXTRA    4
#define GZIP_FNAME     8
#define GZIP_FCOMMENT  16
#endif

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

#ifdef CONFIG_ZLIB
static void *inflate_malloc(void *opaque, unsigned int item, unsigned int size)
{
	return malloc(item * size);
}

static void inflate_free(void *opaque, void *address, unsigned int bytes)
{
	free(address);
}

static int parse_gzip_header(unsigned char *buffer)
{
	int compression_method;
	int gzip_flags;
	int gzip_xflags;
	int os;
	int index;
	int len;
	char c;

	if (buffer[0] != GZIP_ID1 || buffer[1] != GZIP_ID2) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"uImage is not compressed using gzip\n");
		return -1;
	}

	index = 2;
	compression_method = buffer[index++];

	gzip_flags = buffer[index++];
	index += 4;
	gzip_xflags = buffer[index++];

	os = buffer[index++];

	if (gzip_flags & GZIP_FEXTRA) {
		len = buffer[index++];
		len += buffer[index++] << 8;
		index += len;
	}

	if (gzip_flags & GZIP_FNAME)
		while ((c = buffer[index++]) != 0);

	if (gzip_flags & GZIP_FCOMMENT)
		while ((c = buffer[index++]) != 0);

	if (gzip_flags & GZIP_FHCRC)
		index += 2;

	return index;
}

static int do_inflate(pte_t *guest_gphys, phys_addr_t target,
		       phys_addr_t image_phys, uint32_t size)
{
	int err;
	z_stream d_stream;
	int index;
	unsigned char *compr, *uncompr;
	size_t uncomprlen;
	size_t comprlen = size >= PAGE_SIZE ? 1UL << ilog2_32(size) : size;

	comprlen = comprlen > 16*1024*1024 ? 16*1024*1024 : comprlen;
	uncompr = map_gphys(TEMPTLB1, guest_gphys, target, temp_mapping[0],
			    &uncomprlen, TLB_TSIZE_16M, TLB_MAS2_MEM, 1);
	compr = map_phys(TEMPTLB2, image_phys, temp_mapping[1], &comprlen,
	                 TLB_TSIZE_16M, TLB_MAS2_MEM, TLB_MAS3_KERN);
	size -= comprlen;
	index = parse_gzip_header(compr);
	if (index < 0)
		return ERR_BADIMAGE;
	compr += index;
	comprlen -= index;
	image_phys += index;

	d_stream.zalloc = inflate_malloc;
	d_stream.zfree = inflate_free;
	d_stream.opaque = NULL;
	d_stream.outcb = NULL;

	d_stream.next_in  = compr;
	d_stream.avail_in = comprlen;

	err = inflateInit2(&d_stream, -MAX_WBITS);
	if (err < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"load_uimage: inflateInit2 failed with zlib error "
			"code %d\n", err);
		return err;
	}

	d_stream.next_out = uncompr;
	d_stream.avail_out = uncomprlen;

	do {
		err = inflate(&d_stream, Z_NO_FLUSH);

		if (d_stream.avail_out == 0) {
			size_t chunk;
			target += uncomprlen;
			uncompr = map_gphys(TEMPTLB1, guest_gphys, target,
					    temp_mapping[0], &chunk,
					    TLB_TSIZE_16M, TLB_MAS2_MEM, 1);
			uncomprlen = chunk;
			d_stream.next_out = uncompr;
			d_stream.avail_out = uncomprlen;
		}

		if (d_stream.avail_in == 0) {
			size_t tsize;

			tsize = size >= PAGE_SIZE ? 1UL << ilog2_32(size) :
								size;
			tsize = tsize > 16*1024*1024 ? 16*1024*1024 : tsize;
			image_phys += comprlen;
			compr = map_phys(TEMPTLB2, image_phys, temp_mapping[1],
			                 &tsize, TLB_TSIZE_16M, TLB_MAS2_MEM,
			                 TLB_MAS3_KERN);
			size -= tsize;
			d_stream.next_in  = compr;
			d_stream.avail_in = tsize;
			comprlen = tsize;
		}

		if (err == Z_STREAM_END)
			break;

		if (err < 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"load_uimage: uImage decompression failed with "
				"zlib error code %d\n", err);
			return err;
		}

	} while (1);

	err = inflateEnd(&d_stream);
	if (err < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"load_uimage: inflateEnd failed with zlib error code "
			"%d\n", err);
		return err;
	}
	return 0;
}
#endif

/**
 * Parse uImage, generated using mkimage and load it in guest memory
 *
 * @image: Pointer to the uimage, generated using mkimage
 * @length: length of the image, or -1 to ignore length checking
 * @target: guest physical address of the destination
 * @entry: guest phys address of entry point, if not NULL
 */
int load_uimage(guest_t *guest, phys_addr_t image_phys, size_t *length,
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
	         "Loading uImage from %#llx to %#llx\n",
	         image_phys, target);

	size = cpu_from_be32(hdr.size);
	if (*length < size) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_uimage: image size exceeds target window\n");
		return ERR_BADIMAGE;
	}

	*length = size;

	image_phys += sizeof(hdr);

 	if (cpu_from_be32(hdr.type) == IMAGE_TYPE_KERNEL &&
 	    cpu_from_be32(hdr.comp) == 1) {
#ifdef CONFIG_ZLIB
		ret = do_inflate(guest->gphys, target, image_phys, size);
		if (ret < 0)
			return ERR_BADIMAGE;
#else
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_uimage: compressed uimages not supported\n");
		return ERR_BADIMAGE;
#endif
 	} else {
 		size_t ret = copy_phys_to_gphys(guest->gphys, target,
		                                image_phys, size, 1);
 		if (ret != size) {
 			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
 				"load_uimage: cannot copy\n");
 			return ERR_BADADDR;
		}
	}

	if (entry)
		*entry = target - cpu_from_be32(hdr.load) + cpu_from_be32(hdr.ep);

	return 0;
}

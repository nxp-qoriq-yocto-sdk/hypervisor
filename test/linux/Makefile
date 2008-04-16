#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: dtbs partman

include ../Makefile.inc

.PHONY: dtbs rootfs

dtbs: bin/hv-linux-1p.dtb bin/hv-linux-2p.dtb

bin/hv-linux-1p.dtb: bin/linux-p1.dtb hv-linux-1p.dts

bin/hv-linux-2p.dtb: bin/linux-p2.dtb bin/linux-p1.dtb hv-linux-2p.dts

partman: partman.c
	$(CC) -static -m32 -Wall -Wa,-me500 -std=gnu99 -I../../../linux/drivers/misc/ -o $@ $<
	powerpc-e500mc-linux-gnu-strip $@

rootfs: partman
	gunzip rootfs.ext2.gz
	mkdir -p rootfs
	sudo mount -t ext2 -o rw,loop rootfs.ext2 rootfs
	sudo cp partman rootfs/bin/
	sudo cp vmlinux rootfs/
	[ -e rootfs/dev/fsl-hv ] || sudo mknod rootfs/dev/fsl-hv c 253 0
	sudo umount -d rootfs
	rmdir rootfs
	gzip rootfs.ext2

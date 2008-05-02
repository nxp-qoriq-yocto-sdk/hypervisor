#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: dtbs bin/partman

include ../Makefile.inc

.PHONY: dtbs rootfs

dtbs: bin/hv-linux-1p.dtb bin/hv-linux-2p.dtb

bin/hv-linux-1p.dtb: bin/linux-p1.dtb hv-linux-1p.dts

bin/hv-linux-2p.dtb: bin/linux-p2.dtb bin/linux-p1.dtb hv-linux-2p.dts

bin/partman: partman.c
	$(CC) -static -m32 -Wall -Wa,-me500 -std=gnu99 -o $@ $<
	powerpc-e500mc-linux-gnu-strip $@

rootfs: bin/partman
	gunzip rootfs.ext2.gz
	mkdir -p bin/rootfs
	sudo mount -t ext2 -o rw,loop rootfs.ext2 bin/rootfs
	sudo cp -u bin/partman bin/rootfs/bin/
	sudo cp -u vmlinux bin/rootfs/
	[ -e bin/rootfs/dev/fsl-hv ] || sudo mknod bin/rootfs/dev/fsl-hv c 253 0
	sudo umount -d bin/rootfs
	rmdir bin/rootfs
	gzip rootfs.ext2

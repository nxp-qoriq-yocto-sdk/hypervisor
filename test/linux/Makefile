#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: dtbs bin/partman

include ../Makefile.inc

.PHONY: dtbs mount umount vmlinux

dtbs: bin/hv-linux-1p.dtb bin/hv-linux-2p.dtb

bin/hv-linux-1p.dtb: bin/linux-p1.dtb hv-linux-1p.dts

bin/hv-linux-2p.dtb: bin/linux-p2.dtb bin/linux-p1.dtb hv-linux-2p.dts

bin/partman: partman.c
	$(CC) -static -m32 -Wall -Wa,-me500 -std=gnu99 -o $@ $<
	powerpc-e500mc-linux-gnu-strip $@

# Mount rootfs.ext.gz into the bin directory
mount:
	gunzip rootfs.ext2.gz
	mkdir -p bin/rootfs
	sudo mount -t ext2 -o rw,loop rootfs.ext2 bin/rootfs

# Unmount the rootfs, saving all changes
umount:
	sudo umount -d bin/rootfs
	rmdir bin/rootfs
	gzip rootfs.ext2

# Add vmlinux and partman to the rootfs
vmlinux:
	make mount
	sudo cp -u bin/partman bin/rootfs/bin/
	sudo cp -u vmlinux bin/rootfs/
	make umount

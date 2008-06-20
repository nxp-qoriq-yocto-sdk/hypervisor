#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: dtbs rootfs

NO_LIBOS=y
include ../Makefile.inc

.PHONY: dtbs mount umount rootfs

dtbs: bin/hv-linux-1p.dtb bin/hv-linux-2p.dtb bin/hv-linux-intmap.dtb bin/hv-partman.dtb bin/hv-demo.dtb

bin/hv-linux-1p.dtb: bin/linux-p1.dtb hv-linux-1p.dts

bin/hv-linux-intmap.dtb: bin/linux-intmap.dtb hv-linux-intmap.dts

bin/hv-linux-2p.dtb: bin/linux-p2.dtb bin/linux-p1.dtb hv-linux-2p.dts

bin/hv-partman.dtb: bin/partman-p2.dtb bin/partman-p1.dtb hv-partman.dts

bin/hv-demo.dtb: bin/demo-p2.dtb bin/demo-p1.dtb hv-demo.dts

../../tools/partman/bin/partman:
	make -C ../../tools/partman

# Mount rootfs2.ext.gz into the bin directory
mount2:
	cp rootfs.ext2.gz bin/rootfs2.ext2.gz
	gunzip bin/rootfs2.ext2.gz
	mkdir -p bin/rootfs
	sudo mount -t ext2 -o rw,loop bin/rootfs2.ext2 bin/rootfs

# Unmount the rootfs, saving all changes
umount2:
	sudo umount -d bin/rootfs
	rmdir bin/rootfs
	gzip bin/rootfs2.ext2

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
rootfs: vmlinux ../../tools/partman/bin/partman
	make mount2
	sudo cp -u ../../tools/partman/bin/partman bin/rootfs/bin
	sudo cp -u vmlinux bin/rootfs/
	sudo cp -u rootfs.ext2.gz bin/rootfs/
	make umount2

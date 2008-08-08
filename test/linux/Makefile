
#
# Copyright (C) 2008 Freescale Semiconductor, Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
#  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
#  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
#  NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
#  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#


#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: dtbs

NO_LIBOS=y
include ../Makefile.inc

.PHONY: dtbs mount umount rootfs

dtbs: bin/hv-linux-1p.dtb bin/hv-linux-2p.dtb bin/hv-linux-intmap.dtb bin/hv-partman.dtb \
      bin/hv-demo.dtb bin/hv-hello.dtb

bin/hv-linux-1p.dtb: bin/linux-p1.dtb hv-linux-1p.dts

bin/hv-linux-intmap.dtb: bin/linux-intmap.dtb hv-linux-intmap.dts

bin/hv-linux-2p.dtb: bin/linux-p2.dtb bin/linux-p1.dtb hv-linux-2p.dts

bin/hv-partman.dtb: bin/partman-p2.dtb bin/partman-p1.dtb hv-partman.dts

bin/hv-demo.dtb: bin/demo-p2.dtb bin/demo-p1.dtb hv-demo.dts

bin/hv-hello.dtb: bin/hello-p2.dtb bin/partman-p1.dtb hv-hello.dts

../../tools/partman/bin/partman: ../../tools/partman/partman.c
	make -C ../../tools/partman

# Mount rootfs2.ext.gz into the bin directory
mount2:
	cp -f rootfs.ext2.gz bin/rootfs2.ext2.gz
	gunzip -f bin/rootfs2.ext2.gz
	mkdir -p bin/rootfs
	sudo mount -t ext2 -o rw,loop bin/rootfs2.ext2 bin/rootfs

# Unmount the rootfs, saving all changes
umount2:
	sudo umount -d bin/rootfs
	rmdir bin/rootfs
	gzip bin/rootfs2.ext2

# Mount rootfs3.ext.gz into the bin directory
mount3:
	cp -f rootfs.ext2.gz bin/rootfs3.ext2.gz
	gunzip -f bin/rootfs3.ext2.gz
	mkdir -p bin/rootfs
	sudo mount -t ext2 -o rw,loop bin/rootfs3.ext2 bin/rootfs

# Unmount the rootfs, saving all changes
umount3:
	sudo umount -d bin/rootfs
	rmdir bin/rootfs
	gzip bin/rootfs3.ext2

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

rootfs3: ../../tools/partman/bin/partman
	make mount3
	sudo cp -u ../../tools/partman/bin/partman bin/rootfs/bin
	sudo cp -u hello bin/rootfs/
	make umount3


#
# Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
# Copyright (C) 2008,2009 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

test := linux
dir := $(testdir)$(test)/

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-dtb := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dtb)

include $(src)tools/partman/Makefile $(src)tools/shmem/Makefile

.PHONY: $(test)
$(test): $($(test)-dtb)

$(test)-run-%: $(test)
	$(SIMICS) $(testdir)$</run-$*.simics

define mount
	$(call show,mount: bin/linux/$(1))
	$(V)mkdir -p bin/linux/$(1)
	$(V)gunzip < $(testdir)../../images/rootfs.ext2.gz > bin/linux/$(1).ext2
        $(V)/sbin/e2fsck bin/linux/$(1).ext2
        $(V)/sbin/resize2fs bin/linux/$(1).ext2 131072
	$(V)sudo mount -t ext2 -o rw,loop bin/linux/$(1).ext2 bin/linux/$(1)
endef

define unmount
	$(call show,unmount: $(1))
	$(V)sudo umount -d $(1)
	$(V)gzip -f $(1).ext2
endef

# Add vmlinux, partman, and shmem to the rootfs
bin/$(test)/rootfs-partman.ext2.gz: bin/linux/partman bin/linux/shmem $(testdir)../../images/rootfs.ext2.gz
# We want partman on the managed partition, so that we can send and listen
# for doorbells.  We want shmem on the managed partition, so that we can read
# and write the shared memory buffers.
	$(call mount,rootfs)
	sudo cp -af bin/linux/partman bin/linux/rootfs/bin/
	sudo cp -af bin/linux/shmem bin/linux/rootfs/bin/
	$(call unmount,bin/linux/rootfs)
	$(call mount,rootfs-partman)
	sudo cp -af bin/linux/partman bin/linux/rootfs-partman/bin/
	sudo cp -af bin/linux/shmem bin/linux/rootfs-partman/bin/
	sudo cp -af $(testdir)../../images/boot/vmlinux.stripped bin/linux/rootfs-partman/vmlinux
	sudo cp -af bin/linux/rootfs.ext2.gz bin/linux/rootfs-partman/
	sudo cp -af $(testdir)linux/partman-start.sh bin/linux/rootfs-partman/start.sh
	$(call unmount,bin/linux/rootfs-partman)

bin/$(test)/rootfs-hello.ext2.gz: bin/linux/partman $(testdir)../../images/rootfs.ext2.gz
	$(call mount,rootfs-hello)
	sudo cp -af bin/linux/partman bin/linux/rootfs-hello/bin/
	sudo cp -af bin/reset-status/reset-status bin/linux/rootfs-hello/
	sudo cp -af $(testdir)linux/hello-start.sh bin/linux/rootfs-hello/start.sh
	$(call unmount,bin/linux/rootfs-hello)

$(test)-run-partman: bin/$(test)/rootfs-partman.ext2.gz

$(test)-run-hello: $(test) bin/$(test)/rootfs-hello.ext2.gz
	setsid $(testdir)../tools/xtel.sh 9000 3
	$(SIMICS) $(testdir)linux/run-hello.simics

$(test)-run-2p: $(test)
	setsid $(testdir)../tools/xtel.sh 9000 3
	$(SIMICS) $(testdir)linux/run-2p.simics

$(test)-run-partman: $(test)
	setsid $(testdir)../tools/xtel.sh 9000 4
	$(SIMICS) $(testdir)linux/run-partman.simics

$(test)-run-1p: $(test)
	$(SIMICS) $(testdir)linux/run-1p.simics

$(test)-run-1core: $(test)
	setsid $(testdir)../tools/xtel.sh 9000 2
	$(SIMICS) $(testdir)linux/run-1core.simics

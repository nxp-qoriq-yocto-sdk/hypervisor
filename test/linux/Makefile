
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

test := linux
dir := $(testdir)$(test)/

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-bin-dts := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dts)
$(test)-hv-dts := $(filter bin/$(test)/hv-%.dts,$($(test)-bin-dts))
$(test)-part-dts := $(filter-out bin/$(test)/hv-%.dts,$($(test)-bin-dts))

$(test)-hv-dtb := $($(test)-hv-dts:%.dts=%.dtb)
$(test)-part-dtb := $($(test)-part-dts:%.dts=%.dtb)

$($(test)-hv-dts): $($(test)-part-dtb)

bin/$(test)/hv-linux-fman.dtb: bin/common/p4080master.dts

include $(src)tools/partman/Makefile

.PHONY: $(test)
$(test): $($(test)-hv-dtb)

$(test)-run-%: $(test)
	$(SIMICS) $(testdir)$</$*.simics

define mount
	$(call show,mount: bin/linux/$(1))
	$(V)mkdir -p bin/linux/$(1)
	$(V)gunzip < $(testdir)linux/$(1).ext2.gz > bin/linux/$(1).ext2
	$(V)sudo mount -t ext2 -o rw,loop bin/linux/$(1).ext2 bin/linux/$(1)
endef

define unmount
	$(call show,unmount: $(1))
	$(V)sudo umount -d $(1)
	$(V)gzip -f $(1).ext2
endef

# Add vmlinux and partman to the rootfs
bin/$(test)/rootfs-partman.ext2.gz: bin/linux/partman $(dir)rootfs-partman.ext2.gz
	$(call mount,rootfs-partman)
	sudo cp -af bin/linux/partman bin/linux/rootfs-partman/bin/
	sudo cp -af $(testdir)linux/vmlinux bin/linux/rootfs-partman/
	sudo cp -af $(testdir)linux/rootfs.ext2.gz bin/linux/rootfs-partman/
	$(call unmount,bin/linux/rootfs-partman)

bin/$(test)/rootfs-hello.ext2.gz: bin/linux/partman $(dir)rootfs-hello.ext2.gz
	$(call mount,rootfs-hello)
	sudo cp -af bin/linux/partman bin/linux/rootfs-hello/bin/
	sudo cp -af $(testdir)linux/hello bin/linux/rootfs-hello/
	$(call unmount,bin/linux/rootfs-hello)

$(test)-run-partman $(test)-run-demo: bin/$(test)/rootfs-partman.ext2.gz
$(test)-run-hello: bin/$(test)/rootfs-hello.ext2.gz

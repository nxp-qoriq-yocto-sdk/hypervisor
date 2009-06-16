#
# Copyright (C) 2009 Freescale Semiconductor, Inc.
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
# Copyright (C) 2009 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

test := msi
dir := $(testdir)$(test)/
LINUX_KERNEL_SRC_DIR ?= $(testdir)/../../linux-2.6-p4080
SIMICS_WKSPACE_DIR ?=
MKIMAGE ?= mkimage
IMAGES_DIR ?= $(testdir)../../images

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-dtb := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dtb)

.PHONY: $(test)
$(test): $($(test)-dtb)

$(test)-run: $(test)
	$(SIMICS) $(testdir)$</run-msi.simics

define mount-msi
	$(call show,mount: bin/msi/$(1))
	$(V)mkdir -p bin/msi/$(1)
	$(V)gunzip < $(testdir)../../images/rootfs.tiny.ext2.gz > bin/msi/$(1).ext2
        $(V)/sbin/e2fsck bin/msi/$(1).ext2
        $(V)/sbin/resize2fs bin/msi/$(1).ext2 16384
	$(V)sudo mount -t ext2 -o rw,loop bin/msi/$(1).ext2 bin/msi/$(1)
endef

define unmount-msi
	$(call show,unmount: $(1))
	$(V)sudo umount -d bin/msi/$(1)
	$(V)gzip -f bin/msi/$(1).ext2
# step below is only needed for bare-metal testing.
#	$(MKIMAGE) -A ppc -O Linux -T RAMDisk -C gzip -a 0 -e 0 -n "uboot ext2 ramdisk rootfs" -d $(IMAGES_DIR)/$(1).ext2.gz $(IMAGES_DIR)/$(1).ext2.gz.uboot
endef

# Add our kernel module to rootfs
bin/$(test)/rootfs.ext2.gz:
	$(call mount-msi,rootfs)
	sudo cp -af $(@D)/virt_pcie_msi.ko bin/msi/rootfs/lib
	$(call unmount-msi,rootfs)

bin/$(test)/virt_pcie_msi.ko: $(dir)virt_pcie_msi.c $(IMAGES_DIR)/pcie_msi.so
	mkdir -p $(SIMICS_WKSPACE_DIR)/amd64-linux/lib
	cp $(IMAGES_DIR)/pcie_msi.so $(SIMICS_WKSPACE_DIR)/amd64-linux/lib
	@$(MKDIR) $(@D)
	@cp $(testdir)/msi/virt_pcie_msi.c $(@D)
	@echo "obj-m += virt_pcie_msi.o" > $(@D)/Makefile
	make -C $(LINUX_KERNEL_SRC_DIR) M=`pwd`\/bin\/msi modules

$(test): bin/$(test)/virt_pcie_msi.ko bin/$(test)/rootfs.ext2.gz

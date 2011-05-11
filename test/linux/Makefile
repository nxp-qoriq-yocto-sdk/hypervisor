
#
# Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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
# Copyright (C) 2008-2011 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

# make test-*-run IMAGE_TYPE=initrd to use initrd images

test := linux
dir := $(testdir)$(test)/
IMAGE_TYPE := initramfs

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-dtb := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dtb)

$(test)-scr := $(wildcard $(dir)*.scr)
$(test)-ubs := $($(test)-scr:$(dir)%.scr=bin/$(test)/%.ubs)

include $(src)tools/partman/Makefile $(src)tools/shmem/Makefile

%.ubs : %.scr
	@$(MKDIR) $(@D)
	$(call show,build: $<)
	mkimage -A ppc -T script -a 0 -d $< $@

.PHONY: $(test)
$(test): $($(test)-dtb) $($(test)-ubs)

$(test)-run-%: $(test)
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run-$*.simics

define mount
	$(call show,mount: bin/linux/$(1))
	$(V)mkdir -p bin/linux/$(1)
	$(V)gunzip < $(testdir)../../images/rootfs.ext2.gz > bin/linux/$(1).ext2
        $(V)/sbin/e2fsck bin/linux/$(1).ext2
        $(V)/sbin/resize2fs bin/linux/$(1).ext2 $(2)
	$(V)sudo mount -t ext2 -o rw,loop bin/linux/$(1).ext2 bin/linux/$(1)
endef

define unmount
	$(call show,unmount: $(1))
	$(V)sudo umount -d $(1)
	$(V)gzip -f $(1).ext2
endef

# add files to existing initramfs
define add_files
	$(call show,add_files: bin/linux/$(1))
	$(V)gunzip < $(testdir)../../images/initramfs.cpio.gz > bin/linux/$(1).cpio
	$(V)cd bin/linux/$(1); find . | cpio -o -H newc -v -A -F ../$(1).cpio
	$(V)gzip -f9 bin/linux/$(1).cpio
	$(V)mkimage -n 'uboot initramfs rootfs' -A ppc -O linux -T ramdisk -C gzip -d bin/linux/$(1).cpio.gz bin/linux/$(1).cpio.gz.uboot
endef

# initramfs filesystem for partman
bin/$(test)/initramfs-partman.cpio.gz.uboot: bin/linux/partman bin/linux/shmem $(testdir)../../images/initramfs.cpio.gz.uboot
# We want partman on the managed partition, so that we can send and listen
# for doorbells.  We want shmem on the managed partition, so that we can read
# and write the shared memory buffers.
	mkdir -p bin/linux/initramfs-partman/bin
	cp -af bin/linux/partman bin/linux/initramfs-partman/bin/
	cp -af bin/linux/shmem bin/linux/initramfs-partman/bin/
	cp -af $(testdir)../../images/boot/vmlinux.stripped bin/linux/initramfs-partman/vmlinux
	cp -af $(testdir)../../images/initramfs.cpio.gz.uboot bin/linux/initramfs-partman/
	cp -af $(testdir)linux/partman-start_$(TARGET).sh bin/linux/initramfs-partman/start.sh
	$(call add_files,initramfs-partman)
	rm -rf bin/linux/initramfs-partman/

# initrd filesystem for partman
bin/$(test)/rootfs-partman.ext2.gz: bin/linux/partman bin/linux/shmem $(testdir)../../images/rootfs.ext2.gz

	$(call mount,rootfs,115000)
	sudo cp -af bin/linux/partman bin/linux/rootfs/bin/
	sudo cp -af bin/linux/shmem bin/linux/rootfs/bin/
	$(call unmount,bin/linux/rootfs)
	$(call mount,rootfs-partman,172032)
	sudo cp -af bin/linux/partman bin/linux/rootfs-partman/bin/
	sudo cp -af bin/linux/shmem bin/linux/rootfs-partman/bin/
	sudo cp -af $(testdir)../../images/boot/vmlinux.stripped bin/linux/rootfs-partman/vmlinux
	sudo cp -af bin/linux/rootfs.ext2.gz bin/linux/rootfs-partman/
	sudo cp -af $(testdir)linux/partman-start_$(TARGET).sh bin/linux/rootfs-partman/start.sh
	$(call unmount,bin/linux/rootfs-partman)


# initramfs filesystem for hello
bin/$(test)/initramfs-hello.cpio.gz.uboot: bin/linux/partman $(testdir)../../images/initramfs.cpio.gz.uboot
	mkdir -p bin/linux/initramfs-hello/bin
	cp -af bin/linux/partman bin/linux/initramfs-hello/bin/
	cp -af bin/reset-status/reset-status bin/linux/initramfs-hello/
	cp -af $(testdir)linux/hello-start.sh bin/linux/initramfs-hello/start.sh
	$(call add_files,initramfs-hello)
	rm -rf bin/linux/initramfs-hello/

# initrd filesystem for hello
bin/$(test)/rootfs-hello.ext2.gz: bin/linux/partman $(testdir)../../images/rootfs.ext2.gz
	$(call mount,rootfs-hello)
	sudo cp -af bin/linux/partman bin/linux/rootfs-hello/bin/
	sudo cp -af bin/reset-status/reset-status bin/linux/rootfs-hello/
	sudo cp -af $(testdir)linux/hello-start.sh bin/linux/rootfs-hello/start.sh
	$(call unmount,bin/linux/rootfs-hello)


ifeq ($(IMAGE_TYPE),initramfs)
IMAGE_NAME_1 := initramfs-hello.cpio.gz.uboot
IMAGE_NAME_2 := initramfs-partman.cpio.gz.uboot
else
IMAGE_NAME_1 := rootfs-hello.ext2.gz
IMAGE_NAME_2 := rootfs-partman.ext2.gz
endif

$(test)-run-hello: $(test) bin/$(test)/$(IMAGE_NAME_1)
	setsid $(testdir)../tools/xtel.sh 9000 3
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)linux/run-hello.simics

$(test)-run-2p: $(test)
	setsid $(testdir)../tools/xtel.sh 9000 3
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)linux/run-2p.simics

$(test)-run-partman: $(test) bin/$(test)/$(IMAGE_NAME_2)
	setsid $(testdir)../tools/xtel.sh 9000 4
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)linux/run-partman.simics

$(test)-run-1p: $(test)
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)linux/run-1p.simics

$(test)-run-1core: $(test)
	setsid $(testdir)../tools/xtel.sh 9000 2
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)linux/run-1core.simics

$(test)-run-e1000: $(test)
	setsid $(testdir)../tools/xtel.sh 9000 2
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)linux/run-e1000.simics

$(test)-run-scsi: $(test)
	setsid $(testdir)../tools/xtel.sh 9000 2
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)linux/run-scsi.simics

$(test)-pre-hello: bin/$(test)/$(IMAGE_NAME_1)

$(test)-pre-partman: bin/$(test)/$(IMAGE_NAME_2)



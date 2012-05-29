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

test := ipi
dir := $(testdir)$(test)/

# test source
ipi-src-y := $(dir)ipi.c

# define test and libos object files
ipi-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(ipi-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
ipi-obj := $(ipi-obj:%=bin/%.o)

# test source
ipi1-src-y := $(dir)ipi1.c

# define test and libos object files
ipi1-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(ipi1-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
ipi1-obj := $(ipi1-obj:%=bin/%.o)

#test source
ipi2-src-y := $(dir)ipi2.c

# define test and libos object files
ipi2-obj := $(basename $(libos-src-first-y:%=libos/%) \
                           $(libos-src-early-y:%=libos/%) \
                           $(libos-src-y:%=libos/%) \
                           $(ipi2-src-y:$(dir)%=$(test)/%) \
                           $(LIBFDT_SRCS:%=libfdt/%) \
                           common/init.o)
ipi2-obj := $(ipi2-obj:%=bin/%.o)

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-bin-dts := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dts)
$(test)-hv-dts := $(filter bin/$(test)/hv-%.dts,$($(test)-bin-dts))
$(test)-part-dts := $(filter-out bin/$(test)/hv-%.dts,$($(test)-bin-dts))

$(test)-hv-dtb := $($(test)-hv-dts:%.dts=%.dtb)
$(test)-part-dtb := $($(test)-part-dts:%.dts=%.dtb)

$($(test)-hv-dts): $($(test)-part-dtb)

$(test)-scr := $(wildcard $(dir)*.scr)
$(test)-ubs := $($(test)-scr:$(dir)%.scr=bin/$(test)/%.ubs)

bin/$(test)/ipi: $(ipi-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(LD_SCRIPT) -o $@ $^ -lgcc
	$(CROSS_COMPILE)objcopy -O binary $@ $@.bin
	gzip $@.bin
	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $@.bin.gz $@.uImage
	rm -f $@.bin.gz

bin/$(test)/ipi1: $(ipi1-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(LD_SCRIPT) -o $@ $^ -lgcc
	$(CROSS_COMPILE)objcopy -O binary $@ $@.bin
	gzip $@.bin
	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $@.bin.gz $@.uImage
	rm -f $@.bin.gz

bin/$(test)/ipi2: $(ipi2-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(LD_SCRIPT) -o $@ $^ -lgcc
	$(CROSS_COMPILE)objcopy -O binary $@ $@.bin
	gzip $@.bin
	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $@.bin.gz $@.uImage
	rm -f $@.bin.gz

%.ubs : %.scr
	@$(MKDIR) $(@D)
	$(call show,build: $<)
	mkimage -A ppc -T script -a 0 -d $< $@

.PHONY: $(test)
$(test): bin/$(test)/ipi bin/$(test)/ipi1  bin/$(test)/ipi2 $($(test)-hv-dtb) \
	$($(test)-ubs)

# $(call run-sim, sim-script-wo-extension, num-terminals)

define run-sim
	echo "here"
	@if [ $(SIM) == "sim_T4240" ]; then						\
		$(SIM) $(T4SIM_FLAGS) $1.py;					\
	else									\
		if [ $2 != 0 ]; then						\
			setsid $(testdir)../tools/xtel.sh 9000 $($<-num-terms);	\
		fi;								\
		$(SIM) $(SIMICS_FLAGS) $1.simics;				\
	fi;
endef

$(test)-run-A: $(test)
	$(call run-sim,$(testdir)$</run-A,2)

$(test)-run-B: $(test)
	$(call run-sim,$(testdir)$</run-B,3)

$(test)-run-C: $(test)
	$(call run-sim,$(testdir)$</run-C,3)

$(test)-run-D: $(test)
	$(call run-sim,$(testdir)$</run-D,2)

$(test)-check-A: $(test)-run-A-batch
	$(testdir)common/check-results 2

$(test)-check-B: $(test)-run-B-batch
	$(testdir)common/check-results 2 3

$(test)-check-C: $(test)-run-C-batch
	$(testdir)common/check-results 2 3

$(test)-check-D: $(test)-run-D-batch
	$(testdir)common/check-results 2

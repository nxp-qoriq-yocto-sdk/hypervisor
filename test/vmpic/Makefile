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

test := vmpic
dir := $(testdir)$(test)/

$(test)-num-terms := 2

# test source
$(test)-src-y := $(dir)$(test).c

# define test and libos object files
$(test)-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $($(test)-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
$(test)-obj := $($(test)-obj:%=bin/%.o)

# vmpic1 source
vmpic1-src-y := $(dir)vmpic1.c

# define test and libos object files
vmpic1-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(vmpic1-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
vmpic1-obj := $(vmpic1-obj:%=bin/%.o)

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-bin-dts := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dts)
$(test)-hv-dts := $(filter bin/$(test)/hv-%.dts,$($(test)-bin-dts))
$(test)-part-dts := $(filter-out bin/$(test)/hv-%.dts,$($(test)-bin-dts))

$(test)-hv-dtb := $($(test)-hv-dts:%.dts=%.dtb)
$(test)-part-dtb := $($(test)-part-dts:%.dts=%.dtb)

$($(test)-hv-dts): $($(test)-part-dtb)

$(test)-scr := $(wildcard $(dir)*.scr)
$(test)-ubs := $($(test)-scr:$(dir)%.scr=bin/$(test)/%.ubs)

bin/$(test)/$(test): $($(test)-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(LD_SCRIPT) -o $@ $^ -lgcc
	$(CROSS_COMPILE)objcopy -O binary $@ $@.bin
	gzip $@.bin
	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $@.bin.gz $@.uImage
	rm -f $@.bin.gz

bin/$(test)/vmpic1: $(vmpic1-obj)
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
$(test): bin/$(test)/$(test) bin/$(test)/vmpic1 $($(test)-hv-dtb) \
	$($(test)-ubs)

$(test)-run-%: $(test)
	@if [ $($<-num-terms) != 0 ]; then \
		setsid $(testdir)../tools/xtel.sh 9000 $($<-num-terms); \
	fi
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run-$*.simics



$(test)-check-C: $(test)-run-C-batch
	$(testdir)common/check-results 2

$(test)-check-B: $(test)-run-B-batch
	$(testdir)common/check-results 2

$(test)-check-A: $(test)-run-A-batch
	$(testdir)common/check-results 2

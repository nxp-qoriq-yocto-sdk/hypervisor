#
# Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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

test := elf
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

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-bin-dts := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dts)
$(test)-hv-dts := $(filter bin/$(test)/hv.dts,$($(test)-bin-dts))
$(test)-part-dts := $(filter-out bin/$(test)/hv.dts,$($(test)-bin-dts))

$(test)-hv-dtb := $($(test)-hv-dts:%.dts=%.dtb)
$(test)-part-dtb := $($(test)-part-dts:%.dts=%.dtb)

$($(test)-hv-dts): $($(test)-part-dtb)

$(test)-scr := $(wildcard $(dir)*.scr)
$(test)-ubs := $($(test)-scr:$(dir)%.scr=bin/$(test)/%.ubs)

ifeq ($(CONFIG_LIBOS_64BIT),y)
$(test)-LD_SCRIPT := $(testdir)elf/elf64.lds
else
$(test)-LD_SCRIPT := $(testdir)elf/elf.lds
endif

bin/$(test)/$(test): $($(test)-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$($(notdir $@)-LD_SCRIPT) -o $@ $^ -lgcc

%.ubs : %.scr
	@$(MKDIR) $(@D)
	$(call show,build: $<)
	mkimage -A ppc -T script -a 0 -d $< $@

.PHONY: $(test)
$(test): bin/$(test)/$(test) $($(test)-hv-dtb) $($(test)-ubs)

$(test)-run: $(test)
	@if [ $($<-num-terms) != 0 ]; then \
		setsid $(testdir)../tools/xtel.sh 9000 $($<-num-terms); \
	fi
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run.simics

$(test)-run-%: $(test)
	@if [ $($<-num-terms) != 0 ]; then \
		setsid $(testdir)../tools/xtel.sh 9000 $($<-num-terms); \
	fi
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run-$*.simics

#
# Copyright (C) 2008-2012 Freescale Semiconductor, Inc.
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

$(test)-src-y := $(dir)$(test).c
$(test)-dts-y := $(wildcard $(dir)*.dts)
$(test)-scr := $(wildcard $(dir)*.scr)

$(test)-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $($(test)-src-y:$(dir)%=$(test)/%) \
                          common/init.o $(LIBFDT_SRCS:%=libfdt/%))
$(test)-nocheck-obj := 

$(test)-obj := $($(test)-obj:%=bin/%.o)
$(test)-nocheck-obj := $($(test)-nocheck-obj:%=bin/nocheck/%.o)

$(test)-hv-dtb := $($(test)-dts-y:$(dir)%.dts=bin/$(test)/%.dtb)

$(test)-ubs := $($(test)-scr:$(dir)%.scr=bin/$(test)/%.ubs)

bin/$(test)/$(test): $($(test)-obj) $($(test)-nocheck-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(LD_SCRIPT) -o $@ $^ -lgcc
	$(CROSS_COMPILE)objcopy -O binary $@ $@.bin
	gzip $@.bin
	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $@.bin.gz $@.uImage
	rm -f $@.bin.gz

.PHONY: $(test)
$(test): bin/$(test)/$(test) $($(test)-hv-dtb) $($(test)-ubs)

$(test)-num-terms ?= 2

# $(call run-sim, sim-script-wo-extension, num-terminals)

define run-sim
	@if [ $(SIM) == "sim_T4240" ]; then						\
		$(SIM) $(T4SIM_FLAGS) $1.py;					\
	else									\
		if [ $2 != 0 ]; then						\
			setsid $(testdir)../tools/xtel.sh 9000 $($<-num-terms);	\
		fi;								\
		$(SIM) $(SIMICS_FLAGS) $1.simics;				\
	fi;
endef

# We cannot use $(dir) or $(test) in the actions because rule evaluation is
# deferred and we'd end up with the last test procesed, not the one actually
# being run.
$(test)-run: $(test)
	$(call run-sim,$(testdir)$</run,$($<-num-terms))

$(test)-run-%: $(test)
	$(call run-sim,$(testdir)$</run-$*,$($<-num-terms))

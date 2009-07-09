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

test := fast-doorbell
dir := $(testdir)$(test)/

# test source
fast-doorbell-src-y := $(dir)fast-doorbell.c

# define test and libos object files
fast-doorbell-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(fast-doorbell-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
fast-doorbell-obj := $(fast-doorbell-obj:%=bin/%.o)

# test source
fast-doorbell1-src-y := $(dir)fast-doorbell1.c

# define test and libos object files
fast-doorbell1-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(fast-doorbell1-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
fast-doorbell1-obj := $(fast-doorbell1-obj:%=bin/%.o)

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-bin-dts := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dts)
$(test)-hv-dts := $(filter bin/$(test)/hv-%.dts,$($(test)-bin-dts))
$(test)-part-dts := $(filter-out bin/$(test)/hv-%.dts,$($(test)-bin-dts))

$(test)-hv-dtb := $($(test)-hv-dts:%.dts=%.dtb)
$(test)-part-dtb := $($(test)-part-dts:%.dts=%.dtb)

$($(test)-hv-dts): $($(test)-part-dtb)

bin/$(test)/fast-doorbell: $(fast-doorbell-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(testdir)common/tests.lds -o $@ $^ -lgcc

bin/$(test)/fast-doorbell1: $(fast-doorbell1-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(testdir)common/tests.lds -o $@ $^ -lgcc

.PHONY: $(test)
$(test): bin/$(test)/fast-doorbell bin/$(test)/fast-doorbell1 $($(test)-hv-dtb)

$(test)-run-%: $(test)
	$(SIMICS) $(testdir)$</run-$*.simics

$(test)-check-A: $(test)-run-A-batch
	$(testdir)common/check-results 2

$(test)-check-B: $(test)-run-B-batch
	$(testdir)common/check-results 2 3

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

# default to 4080, can ovrride from command line
TARGET=4080

$(test)-src-y := $(dir)$(test).c
$(test)-dts-y := $(wildcard $(dir)*.dts)

$(test)-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $($(test)-src-y:$(dir)%=$(test)/%) \
                          common/init.o $(LIBFDT_SRCS:%=libfdt/%))
$(test)-nocheck-obj := 

$(test)-obj := $($(test)-obj:%=bin/%.o)
$(test)-nocheck-obj := $($(test)-nocheck-obj:%=bin/nocheck/%.o)

$(test)-hv-dtb := $($(test)-dts-y:$(dir)%.dts=bin/$(test)/%.dtb)

bin/$(test)/$(test): $($(test)-obj) $($(test)-nocheck-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(testdir)common/tests.lds -o $@ $^ -lgcc

.PHONY: $(test)
$(test): bin/$(test)/$(test) $($(test)-hv-dtb)

# We cannot use $(dir) here because rule evaluation is defered.
$(test)-run:: $(test)
	$(SIMICS) -e '$$target = $(TARGET)' $(testdir)$</run.simics

$(test)-run-%:: $(test)
	$(SIMICS) $(SIMICS_FLAGS) -e '$$target = $(TARGET)' $(testdir)$</run-$*.simics

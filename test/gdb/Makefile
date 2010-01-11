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

test := gdb
dir := $(testdir)$(test)/

# test source
gdb-src-y := $(dir)gdb.c

# define test and libos object files
gdb-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(gdb-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
gdb-obj := $(gdb-obj:%=bin/%.o)

# test source
gdb-hello-src-y := $(dir)gdb-hello.c

# define test and libos object files
gdb-hello-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(gdb-hello-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
gdb-hello-obj := $(gdb-hello-obj:%=bin/%.o)

# test source
gdb-whoami-src-y := $(dir)gdb-whoami.c

# define test and libos object files
gdb-whoami-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $(gdb-whoami-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)
gdb-whoami-obj := $(gdb-whoami-obj:%=bin/%.o)

$(test)-dts := $(wildcard $(dir)*.dts)
$(test)-bin-dts := $($(test)-dts:$(dir)%.dts=bin/$(test)/%.dts)
$(test)-hv-dts := $(filter bin/$(test)/hv-%.dts,$($(test)-bin-dts))
$(test)-part-dts := $(filter-out bin/$(test)/hv-%.dts,$($(test)-bin-dts))

$(test)-hv-dtb := $($(test)-hv-dts:%.dts=%.dtb)
$(test)-part-dtb := $($(test)-part-dts:%.dts=%.dtb)

$($(test)-hv-dts): $($(test)-part-dtb)

bin/$(test)/gdb: $(gdb-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(testdir)common/tests.lds -o $@ $^ -lgcc

bin/$(test)/gdb-hello: $(gdb-hello-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(testdir)common/tests.lds -o $@ $^ -lgcc

bin/$(test)/gdb-whoami: $(gdb-whoami-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(testdir)common/tests.lds -o $@ $^ -lgcc

.PHONY: $(test)
$(test): bin/$(test)/gdb bin/$(test)/gdb-hello bin/$(test)/gdb-whoami $($(test)-hv-dtb)

$(test)-run-%: $(test)
	../../tools/mux_server/mux_server localhost:9124 9000 9001 9002 9003 9004 &
	xterm -e "telnet localhost 9000" &
	$(SIMICS) $(testdir)$</run-$*.simics

$(test)-run-C: $(test)
	../../tools/mux_server/mux_server localhost:9124 9000 9001 9002 9003 9004 9005 &
	xterm -e "telnet localhost 9000" &
	xterm -e "telnet localhost 9001" &
	$(SIMICS) $(testdir)$</run-C.simics

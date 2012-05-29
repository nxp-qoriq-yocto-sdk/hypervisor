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

# default to p4080ds, can override from command line
export TARGET := p4080ds

# default T4 simulator, can be changed from command line
export SIM := sim_T4240

SIMICS_FLAGS += -e '$$target = $(TARGET)'
T4SIM_FLAGS += -s sim.num_cores=4 -s sim.num_threads_per_core=1 \
				-s sim.clock_dpa=false

testdir := $(src)test/

ifeq ($(wildcard include/config/auto.conf.cmd),)
goals := $(MAKECMDGOALS)

ifeq ($(goals),)
	goals := all
endif

$(goals): include/config/auto.conf

else

SUBMAKES = $(wildcard $(testdir)*/Makefile)
SUBDIRS = $(SUBMAKES:$(testdir)%/Makefile=%)

.PHONY: all $(SUBDIRS)
all: $(SUBDIRS)

include include/config/auto.conf

include $(LIBFDT_DIR)/Makefile.libfdt
include $(libos)lib/Makefile.libos

LIBOS_BIN := bin/libos
LIBOS_CLIENT_H := $(testdir)common/libos-client.h

ifeq ($(CONFIG_LIBOS_64BIT),y)
CC_OPTS = -m64
LD_OPTS = -m64
LD_SCRIPT = $(testdir)common/tests64.lds
else
CC_OPTS = -m32
LD_OPTS = -m32 -msoft-float
LD_SCRIPT = $(testdir)common/tests.lds
endif

CC_OPTS += -Wa,-me500 -nostdinc -I $(GCCINCDIR) -I $(GCCINCDIR)-fixed \
           -I$(src)include -Ibin/include -I$(libos)include -I$(LIBFDT_DIR) \
           -I$(LIBOS)/include-libc -std=gnu99  -include $(LIBOS_CLIENT_H) \
           -I$(testdir)common

LD_OPTS += -static -nostdlib

CC_OPTS_C = -fno-strict-aliasing -fno-common -O2 -g3 \
            -msoft-float -pipe -ffixed-r2 -mmultiple \
            -mno-altivec -funit-at-a-time \

include $(SUBMAKES)

#HDRS := $(shell cd $(testdir); find . -name '*.h')

bin/%.d : $(testdir)%.c
	$(call mkdep,REAL_CC,$(CC_OPTS) $(CC_OPTS_C))

bin/%.o : $(testdir)%.c bin/%.d
	$(call build,CHECKCC,$(CC_OPTS_NODEP) $(CC_OPTS) $(CC_OPTS_C))

bin/%.d : $(testdir)%.S bin/libos/assym.s
	$(call mkdep,REAL_CC,$(CC_OPTS) $(CC_OPTS_ASM))

bin/%.o : $(testdir)%.S bin/%.d
	$(call build,CHECKCC,$(CC_OPTS_NODEP) $(CC_OPTS) $(CC_OPTS_ASM))

bin/libfdt/%.d : $(LIBFDT_DIR)/%.c
	$(call mkdep,REAL_CC,$(CC_OPTS) $(CC_OPTS_C))

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c bin/libfdt/%.d
	$(call build,CHECKCC,$(CC_OPTS_NODEP) $(CC_OPTS) $(CC_OPTS_C))

bin/%.dtb : $(testdir)%.dts
	@$(MKDIR) $(@D)
	$(call show,build: $<)
	$(V)$(DTC) -p 1024 -O dtb $< -o $@

bin/%.ubs : $(testdir)%.scr
	@$(MKDIR) $(@D)
	$(call show,build: $<)
	mkimage -A ppc -T script -a 0 -d $< $@

ifneq ($(wildcard bin),)
-include $(shell find bin -path bin/libos -prune -o -name '*.d' -print) ''
endif
endif

include/config/auto.conf: $(testdir)common/config $(testdir)Kconfig $(LIBOS_DIR)/Kconfig
	@cp -f $(testdir)common/config .config
	@$(MAKE) -f $(src)kconfig/Makefile Kconfig=$(testdir)Kconfig silentoldconfig
	@$(MAKE) -f $(testdir)Makefile $(goals)

.SECONDARY:

#
# Copyright (C) 2008 Freescale Semiconductor, Inc.
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

testdir := $(src)test/

ifeq ($(wildcard include/config/auto.conf.cmd),)
goals := $(MAKECMDGOALS)

ifeq ($(goals),)
	goals := all
endif

.PHONY: config 
$(goals): config

config:
	@cp -f $(testdir)common/config .config
	@$(MAKE) -f $(src)kconfig/Makefile Kconfig=$(testdir)Kconfig silentoldconfig
	@$(MAKE) -f $(testdir)Makefile $(goals)
else

#SUBDIRS = $(wildcard $(src)*)
SUBDIRS = hello byte-chan

include include/config/auto.conf

include $(LIBFDT_DIR)/Makefile.libfdt
include $(libos)lib/Makefile.libos

.PHONY: all $(SUBDIRS)
all: $(SUBDIRS)

LIBOS_BIN := bin/libos
LIBOS_CLIENT_H := $(testdir)common/libos-client.h

CC_OPTS=-m32 -Wa,-me500 -nostdinc -I $(GCCINCDIR) -I $(GCCINCDIR)-fixed \
        -I$(src)include -Ibin/include -I$(libos)include -I$(LIBFDT_DIR) \
        -I$(LIBOS)/include-libc -std=gnu99  -include $(LIBOS_CLIENT_H)

include $(SUBDIRS:%=$(testdir)%/Makefile)

#HDRS := $(shell cd $(testdir); find . -name '*.h')

bin/%.d : $(testdir)%.c
	$(call mkdep,CC,$(CC_OPTS) $(CC_OPTS_C))

bin/%.o : $(testdir)%.c bin/%.d
	$(call build,CC,$(CC_OPTS_NODEP) $(CC_OPTS) $(CC_OPTS_C))

bin/%.d : $(testdir)%.S bin/libos/assym.s
	$(call mkdep,CC,$(CC_OPTS) $(CC_OPTS_ASM))

bin/%.o : $(testdir)%.S bin/%.d
	$(call build,CC,$(CC_OPTS_NODEP) $(CC_OPTS) $(CC_OPTS_ASM))

bin/libfdt/%.d : $(LIBFDT_DIR)/%.c
	$(call mkdep,CC,$(CC_OPTS) $(CC_OPTS_C))

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c bin/libfdt/%.d
	$(call build,CC,$(CC_OPTS_NODEP) $(CC_OPTS) $(CC_OPTS_C))

%.dtb : %.dts
	@$(MKDIR) $(@D)
	$(call show,build: $<)
	$(V)$(DTC) -p 1024 -O dtb $< -o $@

bin/%.dtb : $(testdir)%.dts
	@$(MKDIR) $(@D)
	$(call show,build: $<)
	$(V)$(DTC) -p 1024 -O dtb $< -o $@

# This is a hack to work around the lack of an include
# search path in DTC.
bin/%.dts : $(testdir)%.dts
	@$(MKDIR) $(@D)
	$(V)cp -f $< $@

endif

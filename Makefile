#
# Copyright (C) 2007-2011 Freescale Semiconductor, Inc.
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

export VERSION=1
export SUBVERSION=1
export EXTRAVERSION=-001
export LOCALVERSION := $(shell tools/setlocalversion)

export PROJECTVERSION=$(VERSION).$(SUBVERSION)$(EXTRAVERSION)$(LOCALVERSION)
export src := $(CURDIR)
override src := $(src)/
export O := output
override O := $(O)/

export MKDIR := mkdir -p
export LIBOS := $(src)libos
export LIBOS_DIR := $(LIBOS)/lib
export LIBOS_INC := $(LIBOS)/include

export OLD_PROJECTVERSION=$(strip $(shell cat $(O)/version))

.PHONY: all $(wildcard test/*)
all:

$(O).config $(O)include/config/auto.conf.cmd: ;

-include $(O)include/config/auto.conf.cmd

CONFIGS1 := config xconfig gconfig menuconfig oldconfig silentoldconfig randconfig defconfig allyesconfig allnoconfig
CONFIGS2 := %_defconfig
CONFIGS   = $(CONFIGS1) $(CONFIGS2)

$(O)include/config/auto.conf: $(O).config $(O)include/config/auto.conf.cmd Kconfig $(LIBOS_DIR)/Kconfig
	@$(MAKE) silentoldconfig

.PHONY: FORCE
help $(CONFIGS1): FORCE
	@$(MKDIR) $(O)bin/
	@$(MAKE) -C $(O) -f $(src)kconfig/Makefile $@
$(CONFIGS2): FORCE
	@$(MKDIR) $(O)bin/
	@$(MAKE) -C $(O) -f $(src)kconfig/Makefile $@

non-config := $(filter-out $(CONFIGS) setup clean distclean help, $(MAKECMDGOALS))
ifeq ($(MAKECMDGOALS),)
	non-config := all
endif

$(non-config): $(O)include/config/auto.conf FORCE
ifneq ("$(OLD_PROJECTVERSION)","$(PROJECTVERSION)")
	@echo $(PROJECTVERSION) > $(O)/version
	@$(MAKE) silentoldconfig
endif
	@$(MKDIR) $(O)bin/
	@$(MAKE) -C $(O) -f $(src)Makefile.build $@

clean:
	rm -rf $(O)bin/
	rm -rf $(O)test32/
	rm -rf $(O)test64/
	rm -rf $(O)include/config

setup:
	git submodule sync
	git submodule init
	git submodule update

.PHONY: distclean
distclean: clean
	rm -f tools/mux_server/mux_server
	rm -f $(O).config*

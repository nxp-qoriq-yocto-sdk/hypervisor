#
# Copyright (C) 2007-2009 Freescale Semiconductor, Inc.
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

export VERSION=0
export SUBVERSION=4
export EXTRAVERSION=-004
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

.PHONY: all $(wildcard test/*)
all:

$(O).config $(O)include/config/auto.conf.cmd: ;

-include $(O)include/config/auto.conf.cmd

CONFIGS := config xconfig gconfig menuconfig oldconfig silentoldconfig randconfig defconfig allyesconfig allnoconfig %_defconfig

$(O)include/config/auto.conf: $(O).config $(O)include/config/auto.conf.cmd Kconfig $(LIBOS_DIR)/Kconfig
	@$(MAKE) silentoldconfig

.PHONY: FORCE
help $(CONFIGS): FORCE
	@$(MKDIR) $(O)bin/
	@$(MAKE) -C $(O) -f $(src)kconfig/Makefile $@

non-config := $(filter-out $(CONFIGS) clean distclean help, $(MAKECMDGOALS))
ifeq ($(MAKECMDGOALS),)
	non-config := all
endif

$(non-config): $(O)include/config/auto.conf FORCE
	@$(MKDIR) $(O)bin/
	@$(MAKE) -C $(O) -f $(src)Makefile.build $@

package:
	git-archive --format=tar --prefix=hv-0.3/ v0.3-rc13 doc libos hv.lds include configs Kconfig Makefile Makefile.build patches README Release_Notes.txt src test tools/partman tools/tdgen tools/genassym.sh | gzip > ../hv-0.3.tgz
	git-archive --format=tar --prefix=hv-0.3/ v0.3-rc13 tools/mux_server | gzip > ../mux_server-0.3.tgz
	git-archive --format=tar --prefix=hv-0.3/ v0.3-rc13 tools/setlocalversion | gzip > ../setlocalversion-0.3.tgz
	git-archive --format=tar --prefix=hv-0.3/ v0.3-rc13 kconfig | gzip > ../kconfig-0.3.tgz

clean:
	rm -rf $(O)bin/
	rm -rf $(O)test/
	rm -rf $(O)include/config

.PHONY: distclean
distclean: clean
	rm -f tools/mux_server/mux_server
	rm -f $(O).config*

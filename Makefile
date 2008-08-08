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

export LIBOS_DIR := ../libos/lib
export LIBOS_INC := ../libos/include

$(shell ln -sfn $(LIBOS_DIR) libos)

export VERSION=0
export SUBVERSION=2
export EXTRAVERSION=-rc9
export LOCALVERSION := $(shell tools/setlocalversion)

export PROJECTVERSION=$(VERSION).$(SUBVERSION)$(EXTRAVERSION)$(LOCALVERSION)
export srctree=$(CURDIR)

.PHONY: all
all:

.config include/config/auto.conf.cmd: ;

-include include/config/auto.conf.cmd

include/config/auto.conf: .config include/config/auto.conf.cmd
	@$(MAKE) -f $(srctree)/Makefile silentoldconfig

.PHONY: FORCE
config %config: FORCE
	$(MAKE) -f kconfig/Makefile obj=bin srctree=$(CURDIR) src=kconfig $@

non-config := $(filter-out %config clean, $(MAKECMDGOALS))
non-config := $(filter-out %config distclean, $(MAKECMDGOALS))
ifeq ($(MAKECMDGOALS),)
	non-config := all
endif

$(non-config): include/config/auto.conf
	$(MAKE) -f Makefile.build $@

clean:
	find * -name bin | xargs rm -rf
	rm -rf include/config
	rm -f dts/*.dtb
	rm -f libos

.PHONY: distclean
distclean: clean
	rm -f tools/mux_server/mux_server
	rm -f .config*

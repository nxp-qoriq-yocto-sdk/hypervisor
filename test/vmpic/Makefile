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

all: vmpic_all

include ../Makefile.inc

VMPIC_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) vmpic.c
VMPIC_OBJS := $(basename $(VMPIC_SRCS))
VMPIC_OBJS := $(VMPIC_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
VMPIC_OBJS := $(VMPIC_OBJS:%=bin/%)

vmpic_all: bin/vmpic.bin bin/vmpic.map bin/hv-vmpic-1p.dtb bin/hv-vmpic-coreint.dtb

bin/vmpic.bin: bin/vmpic
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/vmpic: $(VMPIC_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(VMPIC_OBJS) -lgcc

bin/vmpic.map: bin/vmpic
	nm -n bin/vmpic > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(VMPIC_OBJS:.o=.d)
endif

bin/hv-vmpic-1p.dtb: hv-vmpic-1p.dts bin/vmpic-p1.dtb
bin/hv-vmpic-coreint.dtb: hv-vmpic-coreint.dts bin/vmpic-p1.dtb

bin/vmpic-p1.dtb: vmpic-p1.dts

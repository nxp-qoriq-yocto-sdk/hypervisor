
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


all: hcalls_all

include ../Makefile.inc

HCALLS_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) hcalls.c
HCALLS_OBJS := $(basename $(HCALLS_SRCS))
HCALLS_OBJS := $(HCALLS_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
HCALLS_OBJS := $(HCALLS_OBJS:%=bin/%)

hcalls_all: bin/hcalls.bin bin/hcalls.map bin/hv-hcalls-1p.dtb

bin/hcalls.bin: bin/hcalls
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hcalls: $(HCALLS_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(HCALLS_OBJS) -lgcc

bin/hcalls.map: bin/hcalls
	nm -n bin/hcalls > $@

-include $(HCALLS_OBJS:.o=.d)

bin/hv-hcalls-1p.dtb: hv-hcalls-1p.dts bin/hcalls-p1.dtb

bin/hcalls-p1.dtb: hcalls-p1.dts

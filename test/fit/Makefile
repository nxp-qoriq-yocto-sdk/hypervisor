
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


all: fit_all

include ../Makefile.inc

FIT_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) fit.c 
FIT_OBJS := $(basename $(FIT_SRCS))
FIT_OBJS := $(FIT_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
FIT_OBJS := $(FIT_OBJS:%=bin/%)

fit_all: bin/fit.bin bin/fit.map bin/hv-fit.dtb

bin/fit.bin: bin/fit
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/fit: $(FIT_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(FIT_OBJS) -lgcc

bin/fit.map: bin/fit
	nm -n bin/fit > $@

-include $(FIT_OBJS:.o=.d)

bin/hv-fit.dtb: hv-fit.dts bin/fit-p1.dtb

bin/fit-p1.dtb: fit-p1.dts

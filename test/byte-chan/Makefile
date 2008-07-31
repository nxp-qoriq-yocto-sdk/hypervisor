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

all: byte_chan_all

include ../Makefile.inc

BYTE_CHAN_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) byte_chan.c
BYTE_CHAN_OBJS := $(basename $(BYTE_CHAN_SRCS))
BYTE_CHAN_OBJS := $(BYTE_CHAN_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
BYTE_CHAN_OBJS := $(BYTE_CHAN_OBJS:%=bin/%)

byte_chan_all: bin/byte_chan.bin bin/byte_chan.map bin/hv-byte_chan-1p.dtb

bin/byte_chan.bin: bin/byte_chan
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/byte_chan: $(BYTE_CHAN_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(BYTE_CHAN_OBJS) -lgcc

bin/byte_chan.map: bin/byte_chan
	nm -n bin/byte_chan > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(BYTE_CHAN_OBJS:.o=.d)
endif

bin/hv-byte_chan-1p.dtb: hv-byte_chan-1p.dts bin/byte_chan-p1.dtb

bin/byte_chan-p1.dtb: byte_chan-p1.dts

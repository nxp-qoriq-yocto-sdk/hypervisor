
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


#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: msgsnd_all

include ../Makefile.inc

####################
# msgsnd

MSGSND_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) msgsnd.c
MSGSND_OBJS := $(basename $(MSGSND_SRCS))
MSGSND_OBJS := $(MSGSND_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
MSGSND_OBJS := $(MSGSND_OBJS:%=bin/%)

msgsnd_all: bin/msgsnd.bin bin/msgsnd.map bin/hv-msgsnd-1p.dtb

bin/msgsnd.bin: bin/msgsnd
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/msgsnd: $(MSGSND_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(MSGSND_OBJS) -lgcc

bin/msgsnd.map: bin/msgsnd
	nm -n bin/msgsnd > $@

-include $(MSGSND_OBJS:.o=.d)

bin/hv-msgsnd-1p.dtb: hv-msgsnd-1p.dts bin/msgsnd-p1.dtb

bin/msgsnd-p1.dtb: msgsnd-p1.dts

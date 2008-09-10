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

all: cache_all

include ../Makefile.inc

CACHE_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) cache.c
CACHE_OBJS := $(basename $(CACHE_SRCS))
CACHE_OBJS := $(CACHE_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
CACHE_OBJS := $(CACHE_OBJS:%=bin/%)

cache_all: bin/cache.bin bin/cache.map bin/hv-cachemode-on.dtb bin/hv-cachemode-off.dtb

bin/cache.bin: bin/cache
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/cache: $(CACHE_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(CACHE_OBJS) -lgcc

bin/cache.map: bin/cache
	nm -n bin/cache > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(CACHE_OBJS:.o=.d)
endif

bin/hv-cachemode-on.dtb: hv-cachemode-on.dts bin/cachemode-on.dtb

bin/cachemode-on.dtb: cachemode-on.dts

bin/hv-cachemode-off.dtb: hv-cachemode-off.dts bin/cachemode-off.dtb

bin/cachemode-off.dtb: cachemode-off.dts

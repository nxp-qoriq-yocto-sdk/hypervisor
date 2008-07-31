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

all: dma_all

include ../Makefile.inc

DMA_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) dma-test.c
DMA_OBJS := $(basename $(DMA_SRCS))
DMA_OBJS := $(DMA_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
DMA_OBJS := $(DMA_OBJS:%=bin/%)

dma_all: bin/dma.bin bin/dma.map bin/hv-dma-1p.dtb

bin/dma.bin: bin/dma
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/dma: $(DMA_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(DMA_OBJS) -lgcc

bin/dma.map: bin/dma
	nm -n bin/dma > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(DMA_OBJS:.o=.d)
endif

bin/hv-dma-1p.dtb: hv-dma-1p.dts bin/dma-test-p1.dtb

bin/dma-test-p1.dtb: dma-test-p1.dts

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

all: gdb_all

include ../Makefile.inc

GDB_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) gdb.c
GDB_OBJS := $(basename $(GDB_SRCS))
GDB_OBJS := $(GDB_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
GDB_OBJS := $(GDB_OBJS:%=bin/%)

gdb_all: bin/gdb.bin bin/gdb.map bin/hv-gdb.dtb

bin/gdb.bin: bin/gdb
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/gdb: $(GDB_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(GDB_OBJS) -lgcc

bin/gdb.map: bin/gdb
	nm -n bin/gdb > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(GDB_OBJS:.o=.d)
endif

bin/hv-gdb.dtb: hv-gdb.dts bin/gdb-p1.dtb

bin/gdb-p1.dtb: gdb-p1.dts

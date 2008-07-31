
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

GDB_HELLO_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) gdb-hello.c
GDB_HELLO_OBJS := $(basename $(GDB_HELLO_SRCS))
GDB_HELLO_OBJS := $(GDB_HELLO_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
GDB_HELLO_OBJS := $(GDB_HELLO_OBJS:%=bin/%)

gdb_all: bin/gdb.bin bin/gdb.map bin/hv-gdb.dtb bin/gdb-hello.bin bin/hv-gdb-4p.dtb

bin/gdb.bin: bin/gdb
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/gdb: $(GDB_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(GDB_OBJS) -lgcc

bin/gdb.map: bin/gdb
	nm -n bin/gdb > $@

ifneq ($(MAKECMDGOALS),clean)
bin/gdb-hello.bin: bin/gdb-hello
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/gdb-hello: $(GDB_HELLO_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(GDB_HELLO_OBJS) -lgcc

-include $(GDB_OBJS:.o=.d)
-include $(GDB_HELLO_OBJS:.o=.d)
endif

bin/hv-gdb.dtb: hv-gdb.dts bin/gdb-p1.dtb
bin/gdb-p1.dtb: gdb-p1.dts

bin/hv-gdb-4p.dtb: hv-gdb-4p.dts bin/gdb-p1.dtb bin/gdb-p2.dtb bin/gdb-p3.dtb bin/gdb-p4.dtb
bin/gdb-p1.dtb: gdb-p1.dts
bin/gdb-p2.dtb: gdb-p2.dts
bin/gdb-p3.dtb: gdb-p3.dts
bin/gdb-p4.dtb: gdb-p4.dts

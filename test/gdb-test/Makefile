#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
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

-include $(GDB_OBJS:.o=.d)

bin/hv-gdb.dtb: hv-gdb.dts bin/gdb-p1.dtb

bin/gdb-p1.dtb: gdb-p1.dts

#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
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

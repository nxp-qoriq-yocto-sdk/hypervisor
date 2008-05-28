#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: vmpic_all

include ../Makefile.inc

VMPIC_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) vmpic.c
VMPIC_OBJS := $(basename $(VMPIC_SRCS))
VMPIC_OBJS := $(VMPIC_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
VMPIC_OBJS := $(VMPIC_OBJS:%=bin/%)

vmpic_all: bin/vmpic.bin bin/vmpic.map bin/hv-vmpic-1p.dtb bin/hv-vmpic-coreint.dtb

bin/vmpic.bin: bin/vmpic
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/vmpic: $(VMPIC_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(VMPIC_OBJS) -lgcc

bin/vmpic.map: bin/vmpic
	nm -n bin/vmpic > $@

-include $(VMPIC_OBJS:.o=.d)

bin/hv-vmpic-1p.dtb: hv-vmpic-1p.dts bin/vmpic-p1.dtb
bin/hv-vmpic-coreint.dtb: hv-vmpic-coreint.dts bin/vmpic-p1.dtb

bin/vmpic-p1.dtb: vmpic-p1.dts

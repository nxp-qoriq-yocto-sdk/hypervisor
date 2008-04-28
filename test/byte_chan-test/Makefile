#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
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

-include $(BYTE_CHAN_OBJS:.o=.d)

bin/hv-byte_chan-1p.dtb: hv-byte_chan-1p.dts bin/byte_chan-p1.dtb

bin/byte_chan-p1.dtb: byte_chan-p1.dts

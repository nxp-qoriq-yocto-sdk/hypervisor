#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: whoami_all

include ../Makefile.inc

WHOAMI_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) whoami.c $(LIBOS_MP:%=libos/%)
WHOAMI_OBJS := $(basename $(WHOAMI_SRCS))
WHOAMI_OBJS := $(WHOAMI_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
WHOAMI_OBJS := $(WHOAMI_OBJS:%=bin/%)

whoami_all: bin/whoami.bin bin/whoami.map bin/hv-whoami-1p.dtb

bin/whoami.bin: bin/whoami
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/whoami: $(WHOAMI_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(WHOAMI_OBJS) -lgcc

bin/whoami.map: bin/whoami
	nm -n bin/whoami > $@

-include $(WHOAMI_OBJS:.o=.d)

bin/hv-whoami-1p.dtb: hv-whoami-1p.dts bin/whoami-p1.dtb

bin/whoami-p1.dtb: whoami-p1.dts

#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: fit_all

include ../Makefile.inc

FIT_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) fit.c 
FIT_OBJS := $(basename $(FIT_SRCS))
FIT_OBJS := $(FIT_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
FIT_OBJS := $(FIT_OBJS:%=bin/%)

fit_all: bin/fit.bin bin/fit.map bin/hv-fit.dtb

bin/fit.bin: bin/fit
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/fit: $(FIT_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(FIT_OBJS) -lgcc

bin/fit.map: bin/fit
	nm -n bin/fit > $@

-include $(FIT_OBJS:.o=.d)

bin/hv-fit.dtb: hv-fit.dts bin/fit-p1.dtb

bin/fit-p1.dtb: fit-p1.dts

#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: msgsnd_all

include ../Makefile.inc

####################
# msgsnd

MSGSND_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) msgsnd.c
MSGSND_OBJS := $(basename $(MSGSND_SRCS))
MSGSND_OBJS := $(MSGSND_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
MSGSND_OBJS := $(MSGSND_OBJS:%=bin/%)

msgsnd_all: bin/msgsnd.bin bin/msgsnd.map bin/hv-msgsnd-1p.dtb

bin/msgsnd.bin: bin/msgsnd
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/msgsnd: $(MSGSND_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(MSGSND_OBJS) -lgcc

bin/msgsnd.map: bin/msgsnd
	nm -n bin/msgsnd > $@

-include $(MSGSND_OBJS:.o=.d)

bin/hv-msgsnd-1p.dtb: hv-msgsnd-1p.dts bin/msgsnd-p1.dtb

bin/msgsnd-p1.dtb: msgsnd-p1.dts


all: pmr_all

include ../Makefile.inc

PMR_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) pmr.c 
PMR_OBJS := $(basename $(PMR_SRCS))
PMR_OBJS := $(PMR_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
PMR_OBJS := $(PMR_OBJS:%=bin/%)

pmr_all: bin/pmr.bin bin/pmr.map bin/hv-pmr.dtb

bin/pmr.bin: bin/pmr
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/pmr: $(PMR_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(PMR_OBJS) -lgcc

bin/pmr.map: bin/pmr
	nm -n bin/pmr > $@

-include $(PMR_OBJS:.o=.d)

bin/hv-pmr.dtb: hv-pmr.dts bin/pmr.dtb

bin/pmr.dtb: pmr.dts

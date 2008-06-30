all: ipi_all

include ../Makefile.inc

IPI_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) ipi.c
IPI_OBJS := $(basename $(IPI_SRCS))
IPI_OBJS := $(IPI_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
IPI_OBJS := $(IPI_OBJS:%=bin/%)
IPI1_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) ipi1.c
IPI1_OBJS := $(basename $(IPI1_SRCS))
IPI1_OBJS := $(IPI1_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
IPI1_OBJS := $(IPI1_OBJS:%=bin/%)

ipi_all: bin/ipi.bin bin/ipi.map bin/hv-ipi-1p.dtb bin/hv-ipi-2p.dtb \
         bin/ipi1.bin bin/ipi1.map bin/hv-ipi-coreint.dtb

bin/ipi.bin: bin/ipi
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/ipi1.bin: bin/ipi1
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/ipi: $(IPI_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(IPI_OBJS) -lgcc

bin/ipi1: $(IPI1_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(IPI1_OBJS) -lgcc

bin/ipi.map: bin/ipi
	nm -n bin/ipi > $@

bin/ipi1.map: bin/ipi1
	nm -n bin/ipi1 > $@

-include $(IPI_OBJS:.o=.d) $(IPI1_OBJS:.o=.d)

bin/hv-ipi-1p.dtb: hv-ipi-1p.dts bin/ipi-p1.dtb

bin/hv-ipi-2p.dtb: hv-ipi-2p.dts bin/ipi-p1.dtb bin/ipi-p2.dtb

bin/hv-ipi-coreint.dtb: hv-ipi-coreint.dts bin/ipi-p1.dtb bin/ipi-p2.dtb

bin/ipi-p1.dtb: ipi-p1.dts

bin/ipi-p2.dtb: ipi-p2.dts

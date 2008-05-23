all: mmu_all

include ../Makefile.inc

MMU_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) mmu.c 
MMU_OBJS := $(basename $(MMU_SRCS))
MMU_OBJS := $(MMU_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
MMU_OBJS := $(MMU_OBJS:%=bin/%)

mmu_all: bin/mmu.bin bin/hv-mmu.dtb

bin/mmu.bin: bin/mmu
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/mmu: $(MMU_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(MMU_OBJS) -lgcc

-include $(MMU_OBJS:.o=.d)

bin/hv-mmu.dtb: hv-mmu.dts bin/mmu.dtb

bin/mmu.dtb: mmu.dts

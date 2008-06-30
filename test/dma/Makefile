all: dma_all

include ../Makefile.inc

DMA_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) dma-test.c 
DMA_OBJS := $(basename $(DMA_SRCS))
DMA_OBJS := $(DMA_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
DMA_OBJS := $(DMA_OBJS:%=bin/%)

dma_all: bin/dma.bin bin/dma.map bin/hv-dma-1p.dtb

bin/dma.bin: bin/dma
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/dma: $(DMA_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(DMA_OBJS) -lgcc

bin/dma.map: bin/dma
	nm -n bin/dma > $@

-include $(DMA_OBJS:.o=.d)

bin/hv-dma-1p.dtb: hv-dma-1p.dts bin/dma-test-p1.dtb

bin/dma-test-p1.dtb: dma-test-p1.dts

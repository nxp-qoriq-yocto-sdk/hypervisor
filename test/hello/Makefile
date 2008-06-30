all: hello_all

include ../Makefile.inc

HELLO_SRCS := $(LIBOS_SRCS:%=libos/%) $(COMMON_SRCS) hello.c 
HELLO_OBJS := $(basename $(HELLO_SRCS))
HELLO_OBJS := $(HELLO_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
HELLO_OBJS := $(HELLO_OBJS:%=bin/%)

hello_all: bin/hello.bin bin/hello.map bin/hv-hello-1p.dtb

bin/hello.bin: bin/hello
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hello: $(HELLO_OBJS)
	$(CC) $(LD_OPTS) -Wl,-T../common/tests.lds -o $@ $(HELLO_OBJS) -lgcc

bin/hello.map: bin/hello
	nm -n bin/hello > $@

-include $(HELLO_OBJS:.o=.d)

bin/hv-hello-1p.dtb: hv-hello-1p.dts bin/hello-p1.dtb

bin/hello-p1.dtb: hello-p1.dts

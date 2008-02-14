#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

CROSS_COMPILE=powerpc-e500mc-linux-gnu-
DTC_DIR := ../../dtc
LIBFDT_DIR := $(DTC_DIR)/libfdt
LIBOS_DIR := ../../libos/lib
LIBOS_INC := ../../libos/include
LIBOS_BIN := bin/libos
LIBOS_CLIENT_H := libos-client.h

CC=$(CROSS_COMPILE)gcc
CC_OPTS=-m32 -Wa,-me500 -I. -I$(LIBFDT_DIR) -I$(LIBOS_INC) -g \
        -std=gnu99  -include $(LIBOS_CLIENT_H)

CC_OPTS_C= -Wall \
  -Wundef \
  -Wstrict-prototypes \
  -Wno-trigraphs \
  -fno-strict-aliasing \
  -fno-common \
  -O2 \
  -msoft-float \
  -pipe \
  -ffixed-r2 \
  -mmultiple \
  -mno-altivec \
  -funit-at-a-time \
  -mno-string \
  -fomit-frame-pointer \
  -Wno-unused \
  -Werror
CC_OPTS_ASM=-D_ASM -Ibin
LD=$(CROSS_COMPILE)ld
LD_OPTS=-Wl,-m -Wl,elf32ppc -Wl,-Bstatic -nostdlib -msoft-float
GENASSYM=$(LIBOS_DIR)/tools/genassym.sh
MKDIR=mkdir -p

all: hello_all hcall_test_all vmpic_all msgsnd_test_all

LIBFDT_objdir := src
include $(LIBFDT_DIR)/Makefile.libfdt
include $(LIBOS_DIR)/Makefile.libos

LIBOS_SRCS := $(LIBOS_STARTUP) $(LIBOS_FSL_BOOKE_TLB) $(LIBOS_EXCEPTION) \
	$(LIBOS_LIB) $(LIBOS_NS16550) $(LIBOS_CONSOLE) $(LIBOS_HCALL) \
	$(LIBOS_MPIC) $(LIBOS_QUEUE)


####################
# Hello

HELLO_SRCS := $(LIBOS_SRCS:%=libos/%) hello.c init.c
HELLO_OBJS := $(basename $(HELLO_SRCS))
HELLO_OBJS := $(HELLO_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
HELLO_OBJS := $(HELLO_OBJS:%=bin/%)

hello_all: bin/hello.bin bin/hello.map bin/mpc8578sim-hv-test.dtb

bin/hello.bin: bin/hello
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hello: $(HELLO_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(HELLO_OBJS) -lgcc

bin/hello.map: bin/hello
	nm -n bin/hello > $@

-include $(HELLO_OBJS:.o=.d)

####################
# vmpic_test

VMPIC_SRCS := $(LIBOS_SRCS:%=libos/%) vmpic_test.c init.c
VMPIC_OBJS := $(basename $(VMPIC_SRCS))
VMPIC_OBJS := $(VMPIC_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
VMPIC_OBJS := $(VMPIC_OBJS:%=bin/%)

vmpic_all: bin/vmpic_test.bin bin/vmpic_test.map bin/mpc8578sim-hv-test.dtb

bin/vmpic_test.bin: bin/vmpic_test
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/vmpic_test: $(VMPIC_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(VMPIC_OBJS) -lgcc

bin/vmpic_test.map: bin/vmpic_test
	nm -n $< > $@

-include $(VMPIC_OBJS:.o=.d)

####################
# hcall_test

HCALL_SRCS := $(LIBOS_SRCS:%=libos/%) hcall_test.c init.c
HCALL_OBJS := $(basename $(HCALL_SRCS))
HCALL_OBJS := $(HCALL_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
HCALL_OBJS := $(HCALL_OBJS:%=bin/%)

hcall_test_all: bin/hcall_test.bin bin/hcall_test.map bin/mpc8578sim-hv-test.dtb

bin/hcall_test.bin: bin/hcall_test
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hcall_test: $(HCALL_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(HCALL_OBJS) -lgcc

bin/hcall_test.map: bin/hcall_test
	nm -n bin/hcall_test > $@


-include $(HCALL_OBJS:.o=.d)

####################
# msgsnd_test

MSGSND_SRCS := $(LIBOS_SRCS:%=libos/%) msgsnd_test.c init.c
MSGSND_OBJS := $(basename $(MSGSND_SRCS))
MSGSND_OBJS := $(MSGSND_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
MSGSND_OBJS := $(MSGSND_OBJS:%=bin/%)

msgsnd_test_all: bin/msgsnd_test.bin bin/msgsnd_test.map bin/mpc8578sim-hv-test.dtb

bin/msgsnd_test.bin: bin/msgsnd_test
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/msgsnd_test: $(MSGSND_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(MSGSND_OBJS) -lgcc

bin/msgsnd_test.map: bin/msgsnd_test
	nm -n bin/msgsnd_test > $@

-include $(MSGSND_OBJS:.o=.d)

####################

# compile and gen dependecy file
bin/%.o : %.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) \
	-include libfdt_env.h -c -o $@ $<

bin/mpc8578sim-hv-test.dtb: dts/mpc8578sim-hv-test.dts dts/mpc8578sim-test-part1.dtb

bin/%.dtb : dts/%.dts
	$(DTC_DIR)/dtc -p 1024 -O dtb $< -o $@

dts/%.dtb : dts/%.dts
	$(DTC_DIR)/dtc -O dtb $< -o $@

# include the dependecy files
-include bin/genassym.d

bin/example.map: bin/example
	nm -n bin/example > $@

clean:
	rm -rf bin
	rm -f dts/*.dtb

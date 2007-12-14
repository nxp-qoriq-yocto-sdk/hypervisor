#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

CROSS_COMPILE=powerpc-e500mc-linux-gnu-
LIBFDT_DIR := ../../dtc/libfdt
LIBOS_DIR := ../../libos/lib
LIBOS_INC := ../../libos/include
LIBOS_BIN := bin/libos
LIBOS_CLIENT_H := libos-client.h

CC=$(CROSS_COMPILE)gcc
CC_OPTS=-m32 -Wa,-me500 -Iinclude -I$(LIBFDT_DIR) -I$(LIBOS_INC) -g \
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
LD_OPTS=-Wl,-m -Wl,elf32ppc -Wl,-Bstatic -nostdlib
GENASSYM=$(LIBOS_DIR)/tools/genassym.sh
MKDIR=mkdir -p

HELLO_TARGETS := bin/hello.bin bin/hello bin/hello.map
HCALL_TARGETS := bin/hcall_test.bin bin/hcall_test bin/hcall_test.map

all: $(HELLO_TARGETS) $(HCALL_TARGETS)

LIBFDT_objdir := src
include $(LIBFDT_DIR)/Makefile.libfdt
include $(LIBOS_DIR)/Makefile.libos

LIBOS_SRCS := $(LIBOS_STARTUP) $(LIBOS_FSL_BOOKE_TLB) $(LIBOS_EXCEPTION) \
              $(LIBOS_LIB) $(LIBOS_NS16550) $(LIBOS_CONSOLE) $(LIBOS_HCALL)

####################
# Hello

HELLO_SRCS := $(LIBOS_SRCS:%=libos/%) hello.c init.c 
HELLO_OBJS := $(basename $(HELLO_SRCS))
HELLO_OBJS := $(HELLO_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
HELLO_OBJS := $(HELLO_OBJS:%=bin/%)

bin/hello.bin: bin/hello
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hello: $(HELLO_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(HELLO_OBJS) -lgcc

bin/hello.map: bin/hello
	nm -n bin/hello > $@	

-include $(HELLO_OBJS:.o=.d)

####################
# hcall_test

HCALL_SRCS := $(LIBOS_SRCS:%=libos/%) hcall_test.c init.c 
HCALL_OBJS := $(basename $(HCALL_SRCS))
HCALL_OBJS := $(HCALL_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
HCALL_OBJS := $(HCALL_OBJS:%=bin/%)

bin/hcall_test.bin: bin/hcall_test
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hcall_test: $(HCALL_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(HCALL_OBJS) -lgcc

bin/hcall_test.map: bin/hcall_test
	nm -n bin/hcall_test > $@	

-include $(HCALL_OBJS:.o=.d)


####################

# compile and gen dependecy file
bin/%.o : %.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) \
	-include libfdt_env.h -c -o $@ $<

# include the dependecy files
-include bin/genassym.d

bin/example.map: bin/example
	nm -n bin/example > $@	
 
clean:
	rm -rf bin

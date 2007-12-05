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

CC=$(CROSS_COMPILE)gcc
#CC_OPTS=-m32 -nostdinc -Wa,-me500
CC_OPTS=-m32 -Wa,-me500 -Iinclude -I$(LIBFDT_DIR) -I$(LIBOS_DIR) -g -std=gnu99
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
GENASSYM=tools/genassym.sh
MKDIR=mkdir -p

all: bin/hello.bin bin/hcall_test.bin

include $(LIBFDT_DIR)/Makefile.libfdt
include $(LIBOS_DIR)/Makefile.libos

####################
# Hello

HELLO_SRCS_C := hello.c init.c
HELLO_OBJS :=  $(HELLO_SRCS_C:.c=.o) $(LIBFDT_OBJS:%=libfdt/%)
HELLO_OBJS := $(LIBOS_ALL_OBJS:%=$(LIBOS_DIR)/%) $(HELLO_OBJS:%=bin/%) 

bin/hello.bin: bin/hello
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hello: $(HELLO_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(HELLO_OBJS) -lgcc

####################
# hcall_test

HCALL_SRCS_C := hcall_test.c init.c
HCALL_OBJS := $(HCALL_SRCS_C:.c=.o) $(LIBFDT_OBJS:%=libfdt/%)
HCALL_OBJS := $(LIBOS_ALL_OBJS:%=$(LIBOS_DIR)/%) $(HCALL_OBJS:%=bin/%) 

bin/hcall_test.bin: bin/hcall_test
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hcall_test: $(HCALL_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(HCALL_OBJS) -lgcc

####################

# compile and gen dependecy file
bin/%.o : %.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

# assemble and gen dependecy file
bin/%.o : %.S bin/assym.s
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_ASM) -c -o $@ $<

bin/genassym.o: genassym.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) -c -o $@ $<

bin/assym.s: bin/genassym.o
	@$(MKDIR) `dirname $@`
	$(GENASSYM) -o $@ $<

# include the dependecy files
-include bin/genassym.d
-include $(OBJS:.o=.d)

bin/hello.map: bin/hello
	nm -n bin/hello > $@	
 
clean:
	rm -rf bin

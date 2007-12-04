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

all: bin/hello.bin

include $(LIBFDT_DIR)/Makefile.libfdt
include $(LIBOS_DIR)/Makefile.libos

SRCS_C := hello.c init.c

OBJS := $(SRCS_S:.S=.o) $(SRCS_C:.c=.o) $(LIBFDT_OBJS:%=libfdt/%)
OBJS := $(LIBOS_ALL_OBJS:%=$(LIBOS_DIR)/%) $(OBJS:%=bin/%) 

bin/hello.bin: bin/hello
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/hello: $(OBJS)
	$(CC) $(LD_OPTS) -Wl,-Ttests.lds -o $@ $(OBJS) -lgcc

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

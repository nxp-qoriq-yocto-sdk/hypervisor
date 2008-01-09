#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

CROSS_COMPILE=powerpc-e500mc-linux-gnu-
LIBFDT_DIR := ../dtc/libfdt
LIBOS_DIR := ../libos/lib
LIBOS_INC := ../libos/include
LIBOS_BIN := bin/libos
LIBOS_CLIENT_H := include/libos-client.h

CC=$(CROSS_COMPILE)gcc
#CC_OPTS=-m32 -nostdinc -Wa,-me500
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
CC_OPTS_ASM=-D_ASM -Ibin/src
LD=$(CROSS_COMPILE)ld
LD_OPTS=-Wl,-m -Wl,elf32ppc -Wl,-Bstatic -nostdlib
GENASSYM=$(LIBOS_DIR)/tools/genassym.sh
MKDIR=mkdir -p

all: bin/uv.uImage bin/uv.map

LIBFDT_objdir := src
include $(LIBFDT_DIR)/Makefile.libfdt
include $(LIBOS_DIR)/Makefile.libos

LIBOS_SRCS := $(LIBOS_STARTUP) $(LIBOS_FSL_BOOKE_TLB) $(LIBOS_EXCEPTION) \
              $(LIBOS_LIB) $(LIBOS_NS16550) $(LIBOS_CONSOLE)
SRCS := $(LIBOS_SRCS:%=libos/%) src/interrupts.c src/trap.c \
       src/init.c src/guest.c src/tlb.c src/emulate.c src/timers.c \
       src/paging.c src/hcalls.c src/mpic.c

OBJS := $(basename $(SRCS))
OBJS := $(OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
OBJS := $(OBJS:%=bin/%)

bin/uv.uImage: bin/uv.bin
	mkimage -A ppc -O linux -T kernel -C none -a 00000000 -e 00000000 -d $< $@

#	mkimage -A ppc -O linux -T standalone -C gzip -a 00000000 -e 00000000 -d $< $@
#	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $< $@
#	mkimage -A ppc -O linux -C none -T standalone -a 00000000 -e 00000000 -d $< $@


bin/uv.bin.gz: bin/uv.bin
	gzip -f $<

bin/uv.bin: bin/uv
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/uv: $(OBJS)
	$(CC) $(LD_OPTS) -Wl,-Tuv.lds -o $@ $(OBJS) -lgcc

# compile and gen dependecy file
bin/src/%.o : src/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) \
	-include include/libfdt_env.h -c -o $@ $<

# include the dependecy files
-include bin/src/genassym.d
-include $(OBJS:.o=.d)

bin/uv.map: bin/uv
	nm -n bin/uv > $@	
 
clean:
	rm -rf bin

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
CC_OPTS_ASM=-D_ASM -Ibin/src
LD=$(CROSS_COMPILE)ld
LD_OPTS=-Wl,-m -Wl,elf32ppc -Wl,-Bstatic -nostdlib
GENASSYM=tools/genassym.sh
MKDIR=mkdir -p

all: bin/uv.uImage bin/uv.map

LIBFDT_objdir := src
include $(LIBFDT_DIR)/Makefile.libfdt
include $(LIBOS_DIR)/Makefile.libos

SRCS_C := src/interrupts.c src/trap.c src/init.c src/start.c src/tlb.c \
       src/emulate.c src/timers.c \
       src/paging.c src/alloc.c src/hcalls.c
SRCS_S := src/exceptions.S 

OBJS := $(SRCS_S:.S=.o) $(SRCS_C:.c=.o) $(LIBFDT_OBJS:%=libfdt/%)
OBJS := $(OBJS:%=bin/%)
LIBOS_OBJS := $(LIBOS_STARTUP_OBJS:%=$(LIBOS_DIR)/%) \
	$(LIBOS_CONSOLE_OBJS:%=$(LIBOS_DIR)/%) \
	$(LIBOS_LIB_OBJS:%=$(LIBOS_DIR)/%)
OBJS := $(LIBOS_OBJS) $(OBJS)

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
	echo foo $(LIBOS_CONSOLE_OBJS) bar
	$(CC) $(LD_OPTS) -Wl,-Tuv.lds -o $@ $(OBJS) -lgcc
#	$(LD) $(LD_OPTS) -o $@ $(OBJS)

# compile and gen dependecy file
bin/src/%.o : src/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -include include/libfdt_env.h \
	-c -o $@ $<

# assemble and gen dependecy file
bin/src/%.o : src/%.S bin/src/assym.s
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_ASM) -c -o $@ $<

bin/src/genassym.o: src/genassym.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) -c -o $@ $<

bin/src/assym.s: bin/src/genassym.o
	@$(MKDIR) `dirname $@`
	$(GENASSYM) -o $@ $<

# include the dependecy files
-include bin/src/genassym.d
-include $(OBJS:.o=.d)

bin/uv.map: bin/uv
	nm -n bin/uv > $@	
 
clean:
	rm -rf bin

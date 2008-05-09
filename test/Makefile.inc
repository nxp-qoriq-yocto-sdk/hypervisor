#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

CROSS_COMPILE=powerpc-e500mc-linux-gnu-
DTC_DIR := ../../../dtc
LIBFDT_DIR := $(DTC_DIR)/libfdt
LIBOS_DIR := ../../../libos/lib
LIBOS_INC := ../../../libos/include
LIBOS_BIN := bin/libos
LIBOS_CLIENT_H := libos-client.h
COMMON_DIR := ../common
COMMON_SRCS := common/init.c

CC=$(CROSS_COMPILE)gcc
GCCINCDIR := $(shell $(CC) -print-file-name=include)
CC_OPTS=-m32 -Wa,-me500 -nostdinc -I $(GCCINCDIR) -I $(GCCINCDIR)-fixed \
        -I. -I../common -Iinclude -I$(LIBFDT_DIR) -I$(LIBOS_INC) -g \
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

include $(LIBFDT_DIR)/Makefile.libfdt

CONFIG_LIBOS_QUEUE=y
CONFIG_LIBOS_NS16550=y
include $(LIBOS_DIR)/Makefile.libos

LIBOS_SRCS := $(LIBOS_STARTUP) $(LIBOS_FSL_BOOKE_TLB) $(LIBOS_EXCEPTION) \
	$(LIBOS_LIB) $(LIBOS_CONSOLE) $(LIBOS_HCALL) \
	$(LIBOS_MPIC) $(libos-src-y)

####################

# compile and gen dependecy file
bin/%.o : %.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

bin/libfdt/%.o : $(LIBFDT_DIR)/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) \
	-include libfdt_env.h -c -o $@ $<

bin/common/%.o : $(COMMON_DIR)/%.c
	@$(MKDIR) `dirname $@`
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<

bin/%.dtb : %.dts
	@$(MKDIR) `dirname $@`
	$(DTC_DIR)/dtc -p 1024 -O dtb $< -o $@

# include the dependecy files
-include bin/genassym.d

bin/example.map: bin/example
	nm -n bin/example > $@

clean:
	rm -rf bin
	rm -f *.dtb

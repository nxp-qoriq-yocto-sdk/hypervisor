
#
# Copyright (C) 2008 Freescale Semiconductor, Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
#  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
#  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
#  NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
#  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
LD_OPTS=-Wl,-m -Wl,elf32ppc -Wl,-Bstatic -nostdlib -msoft-float
GENASSYM=$(LIBOS_DIR)/tools/genassym.sh
MKDIR=mkdir -p

GSTESTER_TARGETS := bin/gs-tester.bin bin/gs-tester bin/gs-tester.map

all: $(HELLO_TARGETS) $(HCALL_TARGETS) $(GSTESTER_TARGETS)

LIBFDT_objdir := src
include $(LIBFDT_DIR)/Makefile.libfdt
include $(LIBOS_DIR)/Makefile.libos

LIBOS_SRCS := $(LIBOS_STARTUP) $(LIBOS_FSL_BOOKE_TLB) $(LIBOS_EXCEPTION) \
              $(LIBOS_LIB) $(LIBOS_NS16550) $(LIBOS_CONSOLE) $(LIBOS_HCALL)

####################
# gs-tester


GSTESTER_SRCS := $(LIBOS_SRCS:%=libos/%) gs-tester.c trap.c
GSTESTER_OBJS := $(basename $(GSTESTER_SRCS))
GSTESTER_OBJS := $(GSTESTER_OBJS:%=%.o) $(LIBFDT_OBJS:%=libfdt/%)
GSTESTER_OBJS := $(GSTESTER_OBJS:%=bin/%)

bin/gs-tester.bin: bin/gs-tester
	$(CROSS_COMPILE)objcopy -O binary $< $@

bin/gs-tester: $(GSTESTER_OBJS)
	$(CC) $(LD_OPTS) -Wl,-Tgs-tester.lds -o $@ $(GSTESTER_OBJS) -lgcc

bin/gs-tester.map: bin/gs-tester
	nm -n bin/gs-tester > $@

-include $(GSTESTER_OBJS:.o=.d)

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

#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

CROSS_COMPILE=powerpc-e500mc-linux-gnu-

CC=$(CROSS_COMPILE)gcc
#CC_OPTS=-m32 -nostdinc -Wa,-me500
CC_OPTS=-m32 -Wa,-me500 -I../include -std=gnu99
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
CC_OPTS_ASM=-D_ASM
LD=$(CROSS_COMPILE)ld
LD_OPTS=-Wl,-m -Wl,elf32ppc -Wl,-Bstatic -nostdlib
GENASSYM=../tools/genassym.sh

OBJS = head.o exceptions.o interrupts.o trap.o init.o gs-tester.o tlb.o \
	uart.o console.o sprintf.o string.o

SRCS_C = ../src/genassym.c ../src/interrupts.c trap.c ../src/init.c ../src/tbl.c ../src/uart.c ../src/console.c \
          gs-tester.c ../src/sprintf.c ../src/string.c
SRCS_S = ../src/head.S ../src/exceptions.S

all: gs-tester.uImage gs-tester.map

gs-tester.uImage: gs-tester.bin
	mkimage -A ppc -O linux -T kernel -C none -a 00000000 -e 00000000 -d $< $@

#	mkimage -A ppc -O linux -T standalone -C gzip -a 00000000 -e 00000000 -d $< $@
#	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $< $@
#	mkimage -A ppc -O linux -C none -T standalone -a 00000000 -e 00000000 -d $< $@


gs-tester.bin.gz: gs-tester.bin
	gzip -f $<

gs-tester.bin: gs-tester
	$(CROSS_COMPILE)objcopy -O binary $< $@

gs-tester: $(OBJS)
	$(CC) $(LD_OPTS) -Wl,-Tgs-tester.lds -o $@ $(OBJS) -lgcc
#	$(LD) $(LD_OPTS) -o $@ $(OBJS)

# compile and gen dependecy file
%.o : %.c
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

# compile and gen dependecy file
%.o : ../src/%.c
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

# assemble and gen dependecy file
%.o : ../src/%.S assym.s
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_ASM) -c -o $@ $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

genassym.o: ../src/genassym.c
	$(CC) -MD $(CC_OPTS) -c -o $@ $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

assym.s: genassym.o
	$(GENASSYM) -o $@ $<

# include the dependecy files
-include $(SRCS_C:.c=.P)
-include $(SRCS_S:.S=.P)

gs-tester.map: gs-tester
	nm -n gs-tester > $@	
 
clean:
	rm -f gs-tester *.o *.P *.d *.bin *.uImage *.gz assym.s *.map

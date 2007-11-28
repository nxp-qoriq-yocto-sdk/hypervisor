#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

CROSS_COMPILE=powerpc-e500mc-linux-gnu-

CC=$(CROSS_COMPILE)gcc
CC_OPTS=-m32 -Wa,-me500 -Iinclude -g -std=gnu99
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
#CC_OPTS=-m32 -nostdinc -Wa,-me500
CC_OPTS_ASM=-D_ASM
LD=$(CROSS_COMPILE)ld
LD_OPTS=-Wl,-m -Wl,elf32ppc -Wl,-Bstatic -nostdlib
GENASSYM=tools/genassym.sh

OBJS = src/head.o src/exceptions.o src/interrupts.o src/trap.o src/init.o src/guest.o src/tlb.o \
	src/uart.o src/sprintf.o src/console.o src/string.o

SRCS_C = src/genassym.c src/interrupts.c src/trap.c src/init.c src/guest.c src/tbl.c src/uart.c src/sprintf.c src/console.c \
	src/string.c

SRCS_S = src/head.S src/exceptions.S

all: sg.uImage sg.map


sg.uImage: sg.bin
	mkimage -A ppc -O linux -T kernel -C none -a 00000000 -e 00000000 -d $< $@

#	mkimage -A ppc -O linux -T standalone -C gzip -a 00000000 -e 00000000 -d $< $@
#	mkimage -A ppc -O linux -T kernel -C gzip -a 00000000 -e 00000000 -d $< $@
#	mkimage -A ppc -O linux -C none -T standalone -a 00000000 -e 00000000 -d $< $@


sg.bin.gz: sg.bin
	gzip -f $<

sg.bin: sg
	$(CROSS_COMPILE)objcopy -O binary $< $@

sg: $(OBJS)
	$(CC) $(LD_OPTS) -T sg.lds -o $@ $(OBJS) -lgcc
#	$(LD) $(LD_OPTS) -o $@ $(OBJS)

# compile and gen dependecy file
%.o : %.c
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_C) -c -o $@ $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

# assemble and gen dependecy file
%.o : %.S src/assym.s
	$(CC) -MD $(CC_OPTS) $(CC_OPTS_ASM) -c -o $@ $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

src/genassym.o: src/genassym.c
	$(CC) -MD $(CC_OPTS) -c -o $@ $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

src/assym.s: src/genassym.o
	$(GENASSYM) -o $@ $<

# include the dependecy files
-include $(SRCS_C:.c=.P)
-include $(SRCS_S:.S=.P)

sg.map: sg
	nm -n sg > $@	
 
clean:
	rm -f sg src/*.o src/*.P src/*.d *.bin *.uImage *.gz src/assym.s *.map

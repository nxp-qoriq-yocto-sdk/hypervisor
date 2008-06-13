#
# Copyright © 2007 Freescale Semiconductor, Inc
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

CC=$(CROSS_COMPILE)gcc

all: bin/partman

bin/partman: partman.c
	@mkdir -p `dirname $@`
	$(CC) -static -m32 -Wall -Wa,-me500 -std=gnu99 -o $@ $<
	$(CROSS_COMPILE)strip $@

clean:
	rm -rf bin

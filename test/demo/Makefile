#
# Copyright © 2007 Freescale Semiconductor, Inc
# Copyright © 1999 Paul D. Smith
#
#
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-
#

all: dtbs

include ../Makefile.inc

.PHONY: dtbs

dtbs: bin/hv-demo.dtb

bin/hv-demo.dtb: bin/linux-p3.dtb bin/linux-p2.dtb bin/linux-p1.dtb hv-demo.dts


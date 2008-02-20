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

dtbs: bin/hv-linux-1p.dtb bin/hv-linux-2p.dtb

bin/hv-linux-1p.dtb: bin/linux-p1.dtb hv-linux-1p.dts

bin/hv-linux-2p.dtb: bin/linux-p2.dtb bin/linux-p1.dtb hv-linux-2p.dts


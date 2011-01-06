#!/bin/bash
# Copyright (C) 2010 Freescale Semiconductor, Inc.
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

if [ $# -ne 2 ]; then
	echo $0 p1 p2
	echo "    p1:" size in hex of primary window
	echo "    p2:" number of subwindows power of two
	exit 1
fi

# If it is not power of two, generate the list to test the HV error detection
# This could be configurable but the primary window has 1GB
size=`echo "ibase=16; $1" | bc`
size_subw=`expr $size / $2`
# Gap is the amount in bytes that will be left unmapped at the end of a subwindow.
# Note that the mapped size must be power of two.
gap=`expr $size_subw / 2` # This could be a parameter
size_subw=`expr $size_subw - $gap`
size_subw_hex=`echo "obase=16; $size_subw" | bc`
start_subw=0	# This could be a parameter
win_file=subwindows_$2.inc
map_file=dma_map_$2.inc

echo Generate $2 subwindows of 0x$size_subw_hex bytes each, with gaps 
path_to_file=`dirname $0`

echo "size = <0x0 0x$1>;" > $path_to_file/$win_file
echo "subwindow-count = <$2>;" >> $path_to_file/$win_file

echo -n 'prepend-stringlist = "compatible", "subwindow", "dma-map", "' > $path_to_file/$map_file

for (( i=0 ; i<$2 ; i++ )); do
	echo "sub-window@$i {" >> $path_to_file/$win_file
	echo 'compatible = "dma-subwindow";' >> $path_to_file/$win_file
	echo "guest-addr = <0x0 0x`echo "obase=16; $start_subw" | bc`>;" >> $path_to_file/$win_file
	echo "size = <0x0 0x$size_subw_hex>;" >> $path_to_file/$win_file
	echo '};' >> $path_to_file/$win_file
	
	# FIXME: supports only 32-bit address space
	echo 0x`echo "obase=16; $start_subw" | bc` 0x$size_subw_hex " " >> $path_to_file/$map_file
	
	start_subw=`expr $start_subw + $size_subw + $gap`
done

echo '";' >> $path_to_file/$map_file

# Force DTS compilation
touch $path_to_file/hv-B.dts $path_to_file/hv-C.dts

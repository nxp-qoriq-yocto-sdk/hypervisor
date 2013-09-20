#!/bin/bash
#
# Copyright 2013 Freescale Semiconductor, Inc.
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

NUM_GUESTS=23
GUEST_MEM=256

show_help ()
{
	echo "Usage: hv-np [OPTIONS]
Generates a simple hypervisor config device tree with the
given number of guests each having the specified amount of memory.
  -n		number of guests (default $NUM_GUESTS)
  -m		amount of memory in MB to allocate to each guest (default $GUEST_MEM)
  -h		this help message"
}

while getopts "h?n:m:" opt; do
	case "$opt" in
		h|\?)
			show_help
			exit 0
			;;
		n)
			NUM_GUESTS=$OPTARG
			;;
		m)
			GUEST_MEM=$OPTARG
			;;
	esac
done

echo "/dts-v1/;

/ {
	compatible = \"fsl,hv-config\";"


let pma_addr=0

for (( i=0; i<$NUM_GUESTS; i++ ))
do
	echo "	pma$i:pma$i {
		compatible = \"phys-mem-area\";"

	if (( ( pma_addr > 0xffffffff ) ))
	then
		printf "\t\taddr = <0x%x 0x%08x>;\n" $(($pma_addr / 0xffffffff)) $(($pma_addr % (0xffffffff + 1)))
	else
		printf "\t\taddr = <0x0 0x%08x>;\n" $pma_addr
	fi

	printf "\t\tsize = <0x0 0x%08x>;\n" $(($GUEST_MEM*1024*1024))
	let pma_addr=$pma_addr+$GUEST_MEM*1024*1024

	echo "	};"
done

echo "
	pma_hv:pma_hv {
		compatible = \"phys-mem-area\";"

if (( ( pma_addr > 0xffffffff ) ))
then
	printf "\t\taddr = <0x%x 0x%08x>;\n" $(($pma_addr / 0xffffffff)) $(($pma_addr % (0xffffffff + 1)))
else
	printf "\t\taddr = <0x0 0x%08x>;\n" $pma_addr
fi

echo "		size = <0x0 0x01000000>;
	};

	dma-windows {"

let pma_addr=0

for (( i=0; i<$NUM_GUESTS; i++ ))
do
	echo "		window$i: window$i {
			compatible = \"dma-window\";
			guest-addr = <0x0 0x0>;"

	printf "\t\t\tsize = <0x0 0x%08x>;\n" $(($GUEST_MEM*1024*1024))
	let pma_addr=$pma_addr+$GUEST_MEM*1024*1024

	echo "		};"
done

echo "	};

	hv:hypervisor-config {
		compatible = \"hv-config\";
		stdout = <&hvbc>;

		hvbc: byte-channel {
			compatible = \"byte-channel\";
			endpoint = <&uartmux>;
			mux-channel = <0>;
		};

		hv-memory {
			compatible = \"hv-memory\";
			phys-mem = <&pma_hv>;
		};

		uart0:serial0 {
			device = \"serial0\";
		};

		serial1:serial1 {
			device = \"serial1\";
		};

		mpic {
			device = \"/soc/pic\";
		};

		cpc {
			device = \"/soc/l3-cache-controller\";
		};

		corenet-law {
			device = \"/soc/corenet-law\";
		};

		corenet-cf {
			device = \"/soc/corenet-cf\";
		};

		guts {
			device = \"/soc/global-utilities@e0000\";
		};
	};

	uartmux: uartmux {
		compatible = \"byte-channel-mux\";
		endpoint = <&uart0>;
	};"

for (( i=0; i<$NUM_GUESTS; i++ ))
do

echo "
	part$i {
		compatible = \"partition\";
		label = \"p$i-linux\";
		cpus = <$i 1>;
		guest-image = <0xf 0xe8020000 0 0 0 0x00e00000>;
		linux-rootfs = <0xf 0xe9300000 0 0x01300000 0 0x02800000>;
		dtb-window = <0 0x1000000 0 0x10000>;
		no-dma-disable;
		direct-guest-tlb-management;
		direct-guest-tlb-miss;

		gpma$i {
			compatible = \"guest-phys-mem-area\";
			phys-mem = <&pma$i>;
			guest-addr = <0 0>;
		};

		p$(($i))bc: byte-channel {
			compatible = \"byte-channel\";
			endpoint = <&uartmux>;
			mux-channel = <$(($i+1))>;
		};

		aliases {
			stdout = <&p$(($i))bc>;
		};
	};"

done

echo "};"


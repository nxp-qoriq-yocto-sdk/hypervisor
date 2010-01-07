#!/bin/bash
# Creates multiple xterm windows

# Copyright (c) 2008 - 2010, Freescale Semiconductor, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#     * Neither the name of Freescale Semiconductor nor the
#     names of its contributors may be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
#
# ALTERNATIVELY, this software may be distributed under the terms of the
# GNU General Public License ("GPL") as published by the Free Software
# Foundation, either version 2 of that License or (at your option) any
# later version.
#
# THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# TOP_DIR needs to point to the root folder (containing lwe) before calling this script

if [ "$#" -lt "2" -o "$#" -gt "6" ]
then
	echo "$0: required arguments missing"
	echo "Usage: xtel <start-port-id> <#byte-channel-sessions> [-p mux_server_port] [-i mux_server_host_ip_addr]"
	exit 1
fi

MUX_SERVER_PORT=9124
HOST_IP_ADDRESS=localhost
launch_mux_server=1

if [ $# -ge 3 ]
then
	case "$3" in
		-p) MUX_SERVER_PORT=$4
		;;
		-i) HOST_IP_ADDRESS=$4
		;;
		*) echo "$0: unrecognized option $3"; exit 2
		;;
	esac
fi

if [ $# -ge 5 ]
then
	case "$5" in
		-p) MUX_SERVER_PORT=$6
		;;
		-i) HOST_IP_ADDRESS=$6
		;;
		*) echo "$0: unrecognized option $5"; exit 3
		;;
	esac
fi

if [ $HOST_IP_ADDRESS != "localhost" ]
then
	launch_mux_server=0
fi

# add a path to mux_server
export PATH=$PATH:/opt/freescale/ltib/usr/bin

if [ $launch_mux_server -eq 1 ]
then
	mux_server $HOST_IP_ADDRESS:$MUX_SERVER_PORT $(($1 + 0)) $(($1 + 1)) $(($1 + 2)) $(($1 + 3)) $(($1 + 4)) $(($1 + 5)) $(($1 + 6)) $(($1 + 7)) $(($1 + 8)) $(($1 + 9)) $(($1 + 10)) $(($1 + 11)) $(($1 + 12)) $(($1 + 13)) $(($1 + 14)) $(($1 + 15)) $(($1 + 16)) $(($1 + 17)) $(($1 + 18)) $(($1 + 19))  $(($1 + 20)) $(($1 + 21)) $(($1 + 22)) $(($1 + 23)) $(($1 + 24)) $(($1 + 25)) $(($1 + 26)) $(($1 + 27)) $(($1 + 28)) $(($1 + 29)) $(($1 + 30)) $(($1 + 31)) &
fi

for ((i=0; i<$2; i+=1))
do
	xterm  -sl 5000 -title console$(($i + 1)) \
	-e socat -,raw,echo=0 tcp:$HOST_IP_ADDRESS:$(($1 + $i + 0))&
done

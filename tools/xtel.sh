#!/bin/bash
# Creates multiple xterm windows

# Copyright (c) 2008 - 2010, 2012 Freescale Semiconductor, Inc.
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
#set -xv

if [ "$#" -lt "2" -o "$#" -gt "8" ]
then
	echo "$0: required arguments missing"
	echo "Usage: xtel <start-port-id> <#byte-channel-sessions> [-p mux_server_port] [-i mux_server_host_ip_addr][-e mux_server_exec_cmd]"
	exit 1
fi

MUX_SERVER_PORT=9124
HOST_IP_ADDRESS=localhost
launch_mux_server=1
MUX_SERVER_EXEC=""

port=$1
sessions=$2

shift 2

while getopts p:i:e: option
do
	case $option in
		p) MUX_SERVER_PORT="$OPTARG"
		;;
		i) HOST_IP_ADDRESS="$OPTARG"
		;;
		e) MUX_SERVER_EXEC="$OPTARG"
		;;
		?) echo "$0: unrecognized option"; exit 2
		;;
	esac
done

if [ $HOST_IP_ADDRESS != "localhost" ]
then
	launch_mux_server=0
fi

# add a path to mux_server
export PATH=$PATH:./mux_server

if [ $launch_mux_server -eq 1 ]
then
	if [ -z "$MUX_SERVER_EXEC" ]
	then
		mux_server $HOST_IP_ADDRESS:$MUX_SERVER_PORT $(($port + 0)) $(($port + 1)) $(($port + 2)) $(($port + 3)) $(($port + 4)) $(($port + 5)) $(($port + 6)) $(($port + 7)) $(($port + 8)) $(($port + 9)) $(($port + 10)) $(($port + 11)) $(($port + 12)) $(($port + 13)) $(($port + 14)) $(($port + 15)) $(($port + 16)) $(($port + 17)) $(($port + 18)) $(($port + 19))  $(($port + 20)) $(($port + 21)) $(($port + 22)) $(($port + 23)) $(($port + 24)) $(($port + 25)) $(($port + 26)) $(($port + 27)) $(($port + 28)) $(($port + 29)) $(($port + 30)) $(($port + 31)) &
	else
		mux_server -exec "$MUX_SERVER_EXEC" $(($port + 0)) $(($port + 1)) $(($port + 2)) $(($port + 3)) $(($port + 4)) $(($port + 5)) $(($port + 6)) $(($port + 7)) $(($port + 8)) $(($port + 9)) $(($port + 10)) $(($port + 11)) $(($port + 12)) $(($port + 13)) $(($port + 14)) $(($port + 15)) $(($port + 16)) $(($port + 17)) $(($port + 18)) $(($port + 19))  $(($port + 20)) $(($port + 21)) $(($port + 22)) $(($port + 23)) $(($port + 24)) $(($port + 25)) $(($port + 26)) $(($port + 27)) $(($port + 28)) $(($port + 29)) $(($port + 30)) $(($port + 31)) &
	fi
fi

for ((i=0; i<$sessions; i+=1))
do
	xterm  -sl 5000 -title console$(($i + 1)) \
	-e socat -,raw,echo=0 tcp:$HOST_IP_ADDRESS:$(($port + $i + 0))&
done

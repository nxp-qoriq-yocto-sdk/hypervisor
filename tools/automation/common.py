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

#!/usr/bin/env python

#constants used by automation scripts
RMT_SERVER = "ofnir.am.freescale.net" 
RMT_USER = "arailea1"
RMT_PASSWORD = "freescale"
RMT_TESTDIR = "arailea1/" #dir on server used by tftp
RMT_BOARD = "grinch"
LOG_PATH = "../../output/test32/log/" #console log location
BIN_PATH = "../../output/test32/"     #test binaries location


START_PORT = 23400
LINUX_TESTS = ["linux","simtst"]      #identify linux tests

#for test linux-e1000
LINUX_ETH0_IP = "192.168.170.2"       #ip addr for eth0
LINUX_ETH0_PING = "192.168.170.1"     #dest ip addr for ping test

#for test simtst
LINUX_FM1GB1_IP = "192.168.20.2"      #ip addr for fm1gb1
LINUX_FM1GB1_PING = "192.168.20.1"    #dest ip addr for ping test

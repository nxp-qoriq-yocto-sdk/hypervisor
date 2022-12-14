#
# Copyright (C) 2012 Freescale Semiconductor, Inc.
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

HV_FILE     = '../bin/hv.uImage'
IMAGES_PATH = '../../../images/boot/'
UBOOT_FILE  = IMAGES_PATH + 'u-boot.bin'
DTB_FILE    = IMAGES_PATH + 't4240ds.dtb'
GUEST_FILE  = ['', '', '']
GUEST_ADDR  = [0x0a00000, 0x0b00000, 0]
RCW_FILE    = IMAGES_PATH + 'T4240_RCW.bin'
SPD_FILE    = IMAGES_PATH + 'spd_2133.bin'
#mux server base port
BASE_PORT   = 40001
#number of consoles to be opened
TOTAL_PORTS = 2
#serial used
SERIAL = 'duart1_0'


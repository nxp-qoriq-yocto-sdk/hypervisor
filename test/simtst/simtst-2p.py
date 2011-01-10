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

import pexpect
import common
import time

def test(obj):
	obj.sendline("ifconfig fm1-gb1 %s" % common.LINUX_FM1GB1_IP)
	time.sleep(4)
	obj.sendline("ping -c 4 %s" % common.LINUX_FM1GB1_PING)
	obj.expect("4 packets transmitted",timeout=60)
	obj.expect("4 packets received",timeout=10)
	obj.sendline("partman status")
	obj.expect("Partition Name",timeout=10)
	obj.expect_exact("-lwe",timeout=10)
	current_line = obj.readline()
	handle,state = current_line.split()
	obj.sendline("partman start -h %d -f //opt//lwe_apps//hello//hello.elf" % int(handle))
#	obj.expect_exact("Byte Channel Name",timeout=10)
	
	return 0

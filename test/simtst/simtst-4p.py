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
import time

def test(obj):
	obj.sendline("partman status")
	obj.expect("Partition Name",timeout=10)
	#get 1st lwe partition
	idx = obj.expect_exact(["-lwe","Byte Channel Name"],timeout=5)
	if idx == 1:
		#partition is not a manager
		return 0
	current_line = obj.readline()
	handle,state = current_line.split()
	obj.sendline("partman start -h %d -f //opt//lwe_apps//hello//hello.elf" % int(handle))
	time.sleep(1)
	#get 2nd lwe partition
	obj.expect_exact("-lwe",timeout=10)
	current_line = obj.readline()
	handle,state = current_line.split()
	obj.sendline("partman start -h %d -f //opt//lwe_apps//hello//hello.elf" % int(handle))
	return 0

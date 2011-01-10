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
	obj.expect("[$#] ",timeout=10)
	obj.sendline("partman status")
	obj.expect("Partition Name",timeout=10)
	obj.expect_exact("reset-status",timeout=10)
	current_line = obj.readline()
	handle,state = current_line.split()
	obj.sendline("partman restart -h %d" % int(handle))
	obj.expect("[$#] ",timeout=10)
	obj.sendline("cd /;./start.sh")
	obj.expect("[$#] ",timeout=10)
	time.sleep(15)
	obj.sendline("./start.sh")
	obj.expect("[$#] ",timeout=10)
	return 0


def test_hv(obj):
	obj.expect("HV>",timeout=10)
	time.sleep(5)
	obj.send("restart 1\r")
	#time.sleep(5)
	obj.expect_exact("branching to guest reset-status",timeout=60)
	return 0

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

import os
import sys
import pexpect
import time
import imp

#dummy class instead of missing include
class empty:
	def test(self,obj):
		return 0


cmd = sys.argv[1]
filename = sys.argv[2]
pymodule = sys.argv[3] #specific test function module

fileout =  file(filename,'w')

if os.path.exists(sys.argv[3]):
	try:
		special = imp.load_source('special',sys.argv[3])
		try:		
			func = getattr(special, "test")
		except AttributeError:
			special = None #clear import
			special = empty()
	except NameError:
		special = None #clear import
		special = empty()
else:
	special = empty()

obj = pexpect.spawn(cmd,logfile=fileout)

try:
	#detect if console is linux or lwe
	idx = obj.expect_exact(["login:","Hello World! My LWE-id","Reset status test"],timeout=360)
	# for tests with LWE part running hello
	if idx == 1:
		obj.close()
		sys.exit(0)
	# for tests with part running reset status
	elif idx == 2:
		obj.expect_exact("stopped by property:  manager", timeout=30)
		obj.expect("Test Complete", timeout=3)
		obj.expect_exact("stopped by property:  shell", timeout=30)
		obj.expect("Test Complete", timeout=3)
		obj.close()
		sys.exit(0)
	obj.sendline("root")
	#obj.expect("Password:",timeout=60)
	#obj.sendline("root")
	obj.expect("root@.*:~# *",timeout=60)
	obj.sendline("uname")
	obj.expect("Linux",timeout=60)
	#here special linux commands are issued
	special.test(obj)
	time.sleep(1)
	obj.close()
	sys.exit(0)

except pexpect.EOF:
	#print "error partition"
	obj.close()
	sys.exit(-1)
except pexpect.TIMEOUT:
	#print "timeout partition"
	#fileout.write(str(obj))
	obj.close()
	fileout.write("\nTIMEOUT\n")	
	fileout.flush()
	fileout.close()
	sys.exit(-1)

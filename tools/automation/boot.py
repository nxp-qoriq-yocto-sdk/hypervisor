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

import sys
import pexpect
import time
import imp
import os
import common

#dummy class instead of missing include
class empty:
	def test_hv(self,obj):
		return 0

#function to be used on hardware boot, after hw reset
def hw_boot(obj,testname_s, testname_l):
	obj.send("\r")
	obj.expect_exact("=> ", timeout = 10)
	obj.send("pixis_reset altbank\r")
	time.sleep(2)
	#obj.expect_exact("Hit any key to stop autoboot", timeout = 10)
	obj.send("\r")
	obj.expect_exact("=> ", timeout = 20)
	obj.send("setenv unittestdir %s\r" % (common.RMT_TESTDIR+testname_s))	
	obj.expect_exact("=> ", timeout = 10)
	obj.send("tftp 100000 $unittestdir/%s.ubs\r" % testname_l)
	obj.expect_exact("=> ", timeout = 20)
	obj.send("source 100000\r")	


cmd = sys.argv[1] # start command
filename = sys.argv[2] #log file
testnameshort = sys.argv[3]
testnamelong = sys.argv[4]
target_type = sys.argv[5]

#check if special behavior py script exists
if os.path.exists("../../test/%s/%s.py" % (testnameshort,testnamelong)):
	try:
		special = imp.load_source('special',"../../test/%s/%s.py" % (testnameshort,testnamelong))
		try:
			func = getattr(special, "test_hv")
		except AttributeError: #bad file
			special = None #clear import
			special = empty()
	except NameError: #bad file
		special = None #clear import
		special = empty()
else:
	special = empty() #no file

fileout =  file(filename,'w')

obj = pexpect.spawn(cmd,logfile=fileout)
try:
	if target_type == 'hw':
		hw_boot(obj,testnameshort,testnamelong)		
	obj.expect('Freescale Hypervisor', timeout=120)
	while 1:
		index = obj.expect_exact(['HV>','Error','error','Warning', 'warning','branching to guest reset-status'], timeout=300)
		#display warnings is not consistent, case removed
		#if index != 0:
			#print "HV encountered error or warning:\n",obj.before[-30:],obj.after,"\n"
		if index == 5: #hv interaction - used only for reset-status
			special.test_hv(obj)
		#time.sleep(1)

except pexpect.EOF:
	#print "error: HV console closed"
	obj.close()
	sys.exit(-1)
except pexpect.TIMEOUT:
	#print "timeout: HV console"
	obj.close()
	fileout.write("\nTIMEOUT\n")
	fileout.flush()	
	fileout.close()
	sys.exit(-1)	

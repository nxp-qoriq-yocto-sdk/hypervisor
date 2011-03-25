# Copyright (C) 2011 Freescale Semiconductor, Inc.
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

import pexpect
import os, sys
import time
import common

#remote ssh login and commands
def ssh_command (user, host, password, command):

	ssh_newkey = 'Are you sure you want to continue connecting'
	child = pexpect.spawn('ssh -x -l %s %s'%(user, host),logfile=sys.stdout)
	try:
		i = child.expect([ssh_newkey, 'password: ', '[$#]'])
		if i == 0: # SSH does not have the public key. Just accept it.
			child.sendline ('yes')
			child.expect('password: ')   
			child.send(password+"\r")
			child.expect ('[$#]', timeout = 30)
		if i == 1:
			child.send(password+"\r")
			child.expect ('[$#]', timeout = 30)
		for com in command:
			child.send(com+"\r")
			idx = child.expect(['password:',pexpect.TIMEOUT],timeout=3)
			if idx == 0:
				child.send(password+"\r")
			time.sleep(4) #4 works better if boot delay is short
	except pexpect.TIMEOUT:
		print 'ERROR!'
		print 'SSH could not login. Here is what SSH said:'
		print child.before, child.after
		return None    
    	
	return child


host = common.RMT_SERVER
user = common.RMT_USER
password = common.RMT_PASSWORD
cmd_mux = sys.argv[1] #command used to start mux remotely
child = ssh_command (user, host, password, ['skermit -reset '+common.RMT_BOARD,cmd_mux])

try:
	child.expect(pexpect.EOF,timeout=900)
except pexpect.TIMEOUT:
	print "TIMEOUT"
except KeyboardInterrupt: #cleanup mux connection
	child.send("logout\r")
	child.close()
#print child.before



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
		i = child.expect([ssh_newkey, 'password: '])
		if i == 0: # SSH does not have the public key. Just accept it.
			child.sendline ('yes')
			child.expect('password: ')   
		child.send(password+"\r")
		child.expect ('[$#]', timeout = 30)
		for com in command:
			child.send(com+"\r")
			idx = child.expect(['password:',pexpect.TIMEOUT],timeout=3)
			if idx == 0:
				child.send(password+"\r")
			time.sleep(10)
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
	child.expect(pexpect.EOF,timeout=330)
except pexpect.TIMEOUT:
	print "TIMEOUT"
except KeyboardInterrupt: #cleanup mux connection
	child.send("logout\r")
	child.close()
#print child.before



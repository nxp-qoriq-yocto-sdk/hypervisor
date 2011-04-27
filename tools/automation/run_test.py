#!/usr/bin/env python
# Copyright (C) 2010-2011 Freescale Semiconductor, Inc.
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
import time
import signal
import pexpect
import subprocess
import common


start_port = common.START_PORT
linux_tests = common.LINUX_TESTS

cmd_sc = []


if len(sys.argv) < 7:
	print "Usage"
	sys.exit(1)


testname = sys.argv[1]

if sys.argv[2] != 'none':
	subtestname = '-' + sys.argv[2]
else:
	subtestname = ''
	
consoles = int(sys.argv[3]) #total number of consoles
required_consoles = int(sys.argv[4]) #number of consoles required to finish
target = sys.argv[5]
results = sys.argv[6] #results file

#prepare console commands
if 'hw' in target:
	console_server = common.RMT_SERVER
else:
	console_server = "localhost"

cmd_console = "socat -,raw,echo=0 tcp:%s:" % console_server

cmd_sc = []
for i in range(consoles):
	cmd_sc.append(cmd_console+str(start_port+i))

port_list =""
for i in range(32):
	port_list = port_list + " " + str(start_port + i)

#prepare mux and startup commands
if 'hw' in target:
	cmd_mux_only = "mux_server -exec \\\"skermit -con %s\\\" " % (common.RMT_BOARD) +port_list
	cmd_mux = "python remote_hw.py \"%s\" " % (cmd_mux_only)
	target_type = "hw"
else:
	cmd_mux = "../../tools/mux_server/mux_server localhost:9124"+port_list
	cmd_sim = "simics -e '$target=%s' ../../test/%s/run%s.simics -e '$console.con->raw = TRUE' -e c" % (target,testname,subtestname)
	target_type = "sim"
	
#print cmd_mux
#print cmd_sc

#TODO: use os.path
log_path = common.LOG_PATH+"%s/" % target
logfiles = []
log_mux = log_path+testname+subtestname+'-mux.log'
log_sim = log_path+testname+subtestname+'-sim.log'
for i in range(consoles):
	logfiles.append(log_path+testname+subtestname+'-con'+str(i)+'.log')


#start mux server
log_mux_p = file(log_mux,'w')
child_mux = subprocess.Popen(cmd_mux, shell=True, stdout=log_mux_p, stderr=log_mux_p)

#start simics or wait for hardware to startup
if 'hw' not in target:
	log_sim_p = file(log_sim,'w')
	#print cmd_sim
	child_sim = subprocess.Popen (cmd_sim,shell=True,stdin=subprocess.PIPE, stdout=log_sim_p , stderr=log_sim_p , cwd=common.BIN_PATH)
else:
	#allow remote reset and mux start on hardware
	time.sleep(20)


child = [None]
specials = "../../test/%s/%s.py" %(testname,testname+subtestname) #special functions

#start console 0 (boot and HV)
tmp_call_cmd = "python boot.py \"%s\" %s %s %s %s" % (cmd_sc[0],logfiles[0],testname,testname+subtestname,target)
#print tmp_call_cmd
child[0] = subprocess.Popen(tmp_call_cmd, shell=True, stdout=subprocess.PIPE,stderr=subprocess.PIPE)


#start other consoles
for i in range(1,consoles):
	#different behavior for linux tests
	if testname in linux_tests:
		tmp_call_cmd = "python console_linux.py \"%s\" %s %s" % (cmd_sc[i],logfiles[i],specials)
		#print tmp_call_cmd
	else:
		tmp_call_cmd = "python console.py \"%s\" %s" % (cmd_sc[i],logfiles[i])
	child.append(subprocess.Popen(tmp_call_cmd, shell=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE))


#main test loop - checks all consoles
stay_in_loop = True
while stay_in_loop:
	consoles_finished = 0
	consoles_req_finished = 0
	#print consoles_finished
	for i in range(0,consoles):
		retcode = child[i].poll()
		if retcode != None:
			consoles_finished += 1
			#print "console",i
			if retcode == 0:
				#consoles that finish properly return 0
				consoles_req_finished += 1
	#test exit condition
	if consoles_req_finished == required_consoles or \
	   consoles_finished == consoles:
		stay_in_loop = False
		#print consoles_req_finished
		#print consoles_finished
	time.sleep(1)

#cleanup consoles
for i in range(0,consoles):
	if child[i].poll() == None:
		os.kill(child[i].pid,signal.SIGTERM)

#extract coverage data
if common.ENABLE_COVERAGE:
	print "Extracting coverage data\n"
	if "hw" in target:
		gcov_cmd = "../gcov-extract/gcov-extract --host-libgcov-version=401p -v %s:%d" % (common.RMT_SERVER, start_port + 31)
	else:
		gcov_cmd = "../gcov-extract/gcov-extract --host-libgcov-version=401p -v localhost:%d" % (start_port + 31)

	#print gcov_cmd
	lcov_reset_cmd = "lcov --zerocounters -d ../../output/bin --gcov-tool powerpc-linux-gnu-gcov"
	lcov_extract_cmd = "lcov -c -d ../../output/bin/ --gcov-tool powerpc-linux-gnu-gcov -o %s_%s.info -t %s_%s" % (testname+subtestname, target, testname+subtestname, target)
	os.system(lcov_reset_cmd)
	os.system(gcov_cmd)
	os.system(lcov_extract_cmd)

#cleanup simics
if "hw" not in target:
	#quit simics
	os.kill(child_sim.pid,signal.SIGINT)
	time.sleep(5)
	child_sim.stdin.write("quit\r") #this will clear all tmp files
	time.sleep(8)
	#make sure simics is killed
	if child_sim.poll() == None:
		os.kill(child_sim.pid,signal.SIGTERM)

#cleanup mux server local or remote
os.kill(child_mux.pid,signal.SIGINT)

#result parser and reporter
def parseResults(loglist):
	passed = 0
	failed = 0
	timeout = 0
	result = 'PASSED'
	warn = False
	for logfile in loglist:
		tmp_file = file(logfile,'r')
		text = tmp_file.read()
		failed +=text.count('FAILED')
		passed +=text.count('PASSED')
		timeout += text.count('TIMEOUT')
		if text.count('Connection refused'):
			warn = True
		tmp_file.close()
	if failed != 0:
		result = 'FAILED'
	elif timeout != 0 or warn == True:
		result = 'WARNING'
	return (result,passed,failed,timeout)
	
resultsfile = file(results,'a')
res,passed,failed,timeout = parseResults(logfiles)
resultsfile.write("%s,%s,%d,%d,%d\n" % (testname+subtestname,res,passed,failed,timeout))
print "Result = ",res
resultsfile.flush()

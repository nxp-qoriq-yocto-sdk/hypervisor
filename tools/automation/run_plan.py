#!/usr/bin/env python
import os
import sys
import subprocess
import time
import shutil
import glob
import common
import re
import imp
import pexpect
import signal

if len(sys.argv) != 3:
	print "Usage: %s <test_plan_file> <target>\n" % sys.argv[0]
	print "(valid targets: p4080ds, p3041ds, p5020ds, p4080ds_hw)\n"
	sys.exit(1)

test_plan = sys.argv[1]
target = sys.argv[2]
plan = file(test_plan,'r')


#create log dir
log_dir = common.LOG_PATH+"%s/" % target
if os.path.exists(log_dir):
	shutil.rmtree(log_dir)
os.makedirs(log_dir)




results = target+"_results.txt"
resultsp = file(results,'w')
resultsp.write("Test Name,Result,Passes,Fails,Timeouts\n")
resultsp.close()

#start timer
time_start = time.time()


testn = 0
for line in plan:
	#format line
	line = line.rstrip()
	test_list = line.split(',')
	if len(test_list) != 5:
		print "Error in test plan!"
		continue
	testn += 1
	print "\n===Test %d===\n" % testn
	print test_list[0],"-",test_list[1],":",test_list[4]
	if test_list[4] == 'enabled':
		cmd = "python run_test.py %s %s %s %s %s %s" % \
		(test_list[0],test_list[1], test_list[2], test_list[3], target, results)
		print cmd
		part_time_start = time.time()
		child = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		child.wait()
		print "Test duration:",time.strftime("%H:%M:%S", time.gmtime(time.time()-part_time_start))
		time.sleep(2)
	else:
		resultsp = file(results,'a')
		#format testname
		subtest = ""
		if test_list[1] != "none":
			subtest = "-"+test_list[1] 
		resultsp.write("%s%s,NOT_EXECUTED,,,\n" % (test_list[0],subtest))
		resultsp.flush()
		#resultsp.close()

#stop timer
time_stop = time.time()
print "\n\n======================================"
print "Total duration:",time.strftime("%H:%M:%S", time.gmtime(time_stop-time_start))

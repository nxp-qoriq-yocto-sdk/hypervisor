import sys
import pexpect
import time

cmd = sys.argv[1]
filename = sys.argv[2]

fileout =  file(filename,'w')

obj = pexpect.spawn(cmd,logfile=fileout)

try:
	index = obj.expect("Test Complete",timeout=300)
	#print "Test Complete"
	time.sleep(1)
	obj.close()
	sys.exit(0)

except pexpect.EOF:
	#print "error partition"
	obj.close()
	sys.exit(-1)
except pexpect.TIMEOUT:
	#print "timeout partition"
	obj.close()
	fileout.write("\nTIMEOUT\n")
	fileout.flush()
	fileout.close()
	sys.exit(-1)

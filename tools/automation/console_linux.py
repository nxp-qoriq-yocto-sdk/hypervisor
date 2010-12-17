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
	obj.expect("Password:",timeout=60)
	obj.sendline("root")
	obj.expect("[$#] ",timeout=60)
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

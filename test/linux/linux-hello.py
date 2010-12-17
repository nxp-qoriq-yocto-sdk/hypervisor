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

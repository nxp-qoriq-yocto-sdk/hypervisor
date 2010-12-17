import pexpect
import time

def test(obj):
	obj.sendline("partman status")
	obj.expect("Partition Name",timeout=10)
	#get 1st lwe partition
	idx = obj.expect_exact(["-lwe","Byte Channel Name"],timeout=5)
	if idx == 1:
		#partition is not a manager
		return 0
	current_line = obj.readline()
	handle,state = current_line.split()
	obj.sendline("partman start -h %d -f //opt//lwe_apps//hello//hello.elf" % int(handle))
	time.sleep(1)
	#get 2nd lwe partition
	obj.expect_exact("-lwe",timeout=10)
	current_line = obj.readline()
	handle,state = current_line.split()
	obj.sendline("partman start -h %d -f //opt//lwe_apps//hello//hello.elf" % int(handle))
	return 0

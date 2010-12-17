import pexpect
import common
import time

def test(obj):
	obj.sendline("ifconfig fm1-gb1 %s" % common.LINUX_FM1GB1_IP)
	time.sleep(4)
	obj.sendline("ping -c 4 %s" % common.LINUX_FM1GB1_PING)
	obj.expect("4 packets transmitted",timeout=60)
	obj.expect("4 packets received",timeout=10)
	obj.sendline("partman status")
	obj.expect("Partition Name",timeout=10)
	obj.expect_exact("-lwe",timeout=10)
	current_line = obj.readline()
	handle,state = current_line.split()
	obj.sendline("partman start -h %d -f //opt//lwe_apps//hello//hello.elf" % int(handle))
#	obj.expect_exact("Byte Channel Name",timeout=10)
	
	return 0

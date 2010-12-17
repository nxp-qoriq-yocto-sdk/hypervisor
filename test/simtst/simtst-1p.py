import pexpect
import common
import time

def test(obj):
	obj.sendline("ifconfig fm1-gb1 %s" % common.LINUX_FM1GB1_IP)
	time.sleep(4)
	obj.sendline("ping -c 4 %s" % common.LINUX_FM1GB1_PING)
	obj.expect("4 packets transmitted",timeout=60)
	obj.expect("4 packets received",timeout=10)
	return 0

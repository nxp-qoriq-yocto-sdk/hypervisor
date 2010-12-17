import pexpect
import common
import time

def test(obj):
	obj.sendline("ifconfig eth0 %s up" % common.LINUX_ETH0_IP)
	time.sleep(3)
	obj.sendline("ping -c 4 %s" % common.LINUX_ETH0_PING)
	obj.expect("4 packets transmitted",timeout=60)
	obj.expect("4 packets received",timeout=10)
	return 0

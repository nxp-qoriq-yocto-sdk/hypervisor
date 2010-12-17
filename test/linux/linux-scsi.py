import pexpect

def test(obj):
	obj.sendline("fdisk /dev/sda")
	obj.expect_exact("Command (m for help):",timeout=20)
	obj.sendline("p")
	obj.expect("heads",timeout=10)
	obj.expect_exact("Command (m for help):",timeout=10)
	return 0

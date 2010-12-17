import pexpect

def test(obj):
	obj.sendline("cd /;./start.sh")
	obj.expect("[$#]",timeout=30)
	return 0

#
# Copyright (C) 2012 Freescale Semiconductor, Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
#  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
#  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
#  NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
#  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

def bootprep():
	bm.default_processor = top.cluster0.cpu0.thread0

	# Load the binary
	assert True == top.ifc.exts.loadImageToBank(RCW_FILE, 0, 0)
	assert True == top.ifc.exts.loadImageToBank(UBOOT_FILE, 0, 0x7F80000)
	assert True == top.ifc.exts.loadImageToBank(DTB_FILE,   0, 0x0800000)
	assert True == top.ifc.exts.loadImageToBank(HV_FILE,    0, 0x0700000)
	assert True == top.ifc.exts.loadImageToBank(HV_DTB,     0, 0x0900000)

	for (guest,addr) in zip(GUEST_FILE,GUEST_ADDR):
		if guest:
			print "Loading guest " + guest + " to address " + str(hex(addr))
			assert True == top.ifc.exts.loadImageToBank(guest, 0, addr)

	# Boot release
	top.configunit.regs.BRRL.write(0x1)

	# Ready to run...
	set_global_log_level(log_levels.NONE)

	#read the RCW
	top.pbl.exts.doPreBootLoad()

def found_uboot_prompt(the_match):
	print "Found."
	print "Booting Hypervisor ..."
	top.duart1.uart0.exts.puts('\n')
	top.duart1.uart0.exts.puts('setenv bootargs config-addr=0xfe8900000; bootm e8700000 - e8800000\n')

def hv_autoboot():
	print "Waiting for u-boot prompt ..."
	top.duart1.uart0.events.string_match.data.match.observe.on_occurrence(found_uboot_prompt, "Hit any key to stop autoboot")


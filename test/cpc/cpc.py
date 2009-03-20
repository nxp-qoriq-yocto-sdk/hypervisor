#
# Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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

cycle_count = 0
def markpoint_hap_callback(callback_data, trigger_obj, id, vaddr, cyc, seq_num, cyc_last, cyc_last_this):
	global cycle_count

	if ((trigger_obj.name == "cpu0") and (id == 3)):
		run_command("stop")
		run_command("quit")
	if ((trigger_obj.name == "cpu0") and (id == 1)):
		print "cycle since markpooint 0 =%d\n" %(cyc_last)
		cycle_count = cyc_last
	if ((trigger_obj.name == "cpu0") and (id == 2)):
		print "cycle since markpooint 1 =%d\n" %(cyc_last)
		if (cyc_last > cycle_count):
			print "CPC Test PASSED\n"
		else:
			print "CPC Test FAILED\n"

SIM_hap_add_callback("Markpoint_Encountered", markpoint_hap_callback, None)

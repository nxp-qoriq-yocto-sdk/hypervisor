#
# Copyright (C) 2009 Freescale Semiconductor, Inc.
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

mark_pt1 = 0
mark_pt2 = 0
def markpoint_hap_callback(callback_data, trigger_obj, id, vaddr, cyc, seq_num, cyc_last, cyc_last_this):
	global mark_pt1
	global mark_pt2
	if ((trigger_obj.name == "cpu0") and (id == 31)):
		mark_pt1 += 1
	if ((trigger_obj.name == "cpu4") and (id == 31)):
		mark_pt2 += 1
	if ((mark_pt1 != 0) and (mark_pt2 != 0)):
		run_command("stop")
		os.system('../../test/csd/check_ccm_log.sh')
		run_command("quit")

SIM_hap_add_callback("Markpoint_Encountered", markpoint_hap_callback, None)

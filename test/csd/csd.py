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

exc_stat = {}
mode_stat = {}
prev_inst_cnt = {}

def mode_tracker(callback_data, cpu, old_mode, new_mode):
    global mode_stat
    if not cpu in mode_stat:
        mode_stat[cpu] = {}
        mode_stat[cpu][Sim_CPU_Mode_User] = 0
        mode_stat[cpu][Sim_CPU_Mode_Supervisor] = 0
        mode_stat[cpu][Sim_CPU_Mode_Hypervisor] = 0
        prev_inst_cnt[cpu] = cpu.steps
    else:
        mode_stat[cpu][old_mode] += (cpu.steps - prev_inst_cnt[cpu])
        prev_inst_cnt[cpu] = cpu.steps
        
def clear_stats():
    global mode_stat
    global exc_stat;
    cpus = exc_stat.keys()
    for cpu in cpus:
        for exc in exc_stat[cpu]:
            exc_stat[cpu][exc] = 0
        mode_stat[cpu][Sim_CPU_Mode_User] = 0
        mode_stat[cpu][Sim_CPU_Mode_Supervisor] = 0
        mode_stat[cpu][Sim_CPU_Mode_Hypervisor] = 0

def exc_tracker(callback_data, cpu, exc_no):
    global exc_stat;
    if not cpu in exc_stat:
        exc_stat[cpu] = {}
    if not exc_no in exc_stat[cpu]:
        exc_stat[cpu][exc_no] = 0
    exc_stat[cpu][exc_no] += 1

def print_stat():
    global mode_stat
    cpus = exc_stat.keys()
    cpus.sort()
    for cpu in cpus:
        print "%s" % SIM_object_name(cpu)
        for exc in exc_stat[cpu]:
            count = exc_stat[cpu][exc]
            print "\t%30s: %d" % (cpu.iface.exception.get_name(exc), count)
        print "\t%30s: %d" % ("User mode inst cnt", mode_stat[cpu][Sim_CPU_Mode_User])
        print "\t%30s: %d" % ("Supervisor mode inst cnt", mode_stat[cpu][Sim_CPU_Mode_Supervisor])
        print "\t%30s: %d" % ("Hypervisor mode inst cnt", mode_stat[cpu][Sim_CPU_Mode_Hypervisor])

SIM_hap_add_callback("Core_Exception", exc_tracker, None)

SIM_hap_add_callback("Core_Mode_Change", mode_tracker, None)

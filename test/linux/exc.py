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

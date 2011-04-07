/** @file
 * Command line shell
 */

/* Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <limits.h>

#include <libos/readline.h>
#include <libos/pamu.h>
#include <libos/alloc.h>

#include <errors.h>
#include <devtree.h>
#include <shell.h>
#include <percpu.h>
#include <guts.h>
#include <benchmark.h>
#include <error_mgmt.h>

extern command_t *shellcmd_begin, *shellcmd_end;

static command_t *find_command(const char *str)
{
	command_t **i, *cmd;

	for (i = &shellcmd_begin; i < &shellcmd_end; i++) {
		cmd = *i;

		if (!strcmp(str, cmd->name))
			return cmd;

		if (cmd->aliases) {
			const char **alias;

			for (alias = cmd->aliases; *alias; alias++)
				if (!strcmp(str, *alias))
					return cmd;
		}
	}

	return NULL;
}

static int get_partition_num(shell_t *shell, char *numstr)
{
	uint32_t num;

	num = get_number32(shell->out, numstr);
	if (cpu->errno)
		return -1;

	if (num >= num_guests) {
		qprintf(shell->out, 1, "Partition %u does not exist.\n", num);
		return -1;
	}

	return num;
}

static int shell_action(void *user_ctx, char *buf)
{
	shell_t *shell = user_ctx;
	char *cmdname;
	command_t *cmd;

	cmdname = nextword(&buf);
	if (!cmdname)
		return 0;

	cmd = find_command(cmdname);
	if (cmd)
		cmd->action(shell, buf);
	else
		qprintf(shell->out, 1,"Unknown command '%s'.\n", cmdname);

	return 0;
}

static void shell_thread(trapframe_t *regs, void *arg)
{
	shell_t *shell = arg;
	int ret;

	ret = readline_init(stdin, stdout, "HV> ",
	                    shell_action, shell, 1);
	if (ret < 0)
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: readline_init returned %d\n", __func__, ret);

	while (1) {
		prepare_to_block();
		block();
	}
}

void shell_init(void)
{
	open_stdin();

	if (stdin && stdout) {
		thread_t *thread;
		shell_t *shell = alloc_type(shell_t);
		if (!shell)
			return;

		shell->out = stdout;

		thread = new_thread(shell_thread, shell, 1);
		if (thread)
			unblock(thread);
		else
			printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			         "%s: failed to create shell thread\n", __func__);
	}
}

static void print_aliases(shell_t *shell, command_t *cmd)
{
	if (cmd->aliases) {
		const char **a = cmd->aliases;

		qprintf(shell->out, 1, " (");
		while (*a) {
			qprintf(shell->out, 1, "%s", *a);
			a++;
			if (*a)
				qprintf(shell->out, 1, ",");
		}
		qprintf(shell->out, 1, ")");
	}
}

static void help_fn(shell_t *shell, char *args)
{
	command_t *cmd;
	const char *cmdname = nextword(&args);

	if (!cmdname) {
		command_t **i;

		qprintf(shell->out, 1, "Commands:\n");

		for (i = &shellcmd_begin; i < &shellcmd_end; i++) {
			cmd = *i;
			qprintf(shell->out, 1, "  %s", cmd->name);
			print_aliases(shell, cmd);
			qprintf(shell->out, 1, " - %s\n",cmd->shorthelp);
		}
		qprintf(shell->out, 1, "\n");

		return;
	}

	cmd = find_command(cmdname);
	if (!cmd) {
		qprintf(shell->out, 1, "help: unknown command '%s'.\n", cmdname);
		return;
	}

	qprintf(shell->out, 1, "%s", cmd->name);
	print_aliases(shell, cmd);
	qprintf(shell->out, 1, " - %s\n",cmd->shorthelp);

	if (cmd->longhelp)
		qprintf(shell->out, 1, "\n%s\n", cmd->longhelp);

	qprintf(shell->out, 1, "\n");
}

static command_t help = {
	.name = "help",
	.action = help_fn,
	.aliases = (const char *[]){ "?", NULL },
	.shorthelp = "Print command usage information",
};
shell_cmd(help);

static void version_fn(shell_t *shell, char *args)
{
#ifdef CONFIG_LIBOS_64BIT
	qprintf(shell->out, 1, "Freescale Embedded Hypervisor version %s ppc64\n", CONFIG_HV_VERSION);
#else
	qprintf(shell->out, 1, "Freescale Embedded Hypervisor version %s\n", CONFIG_HV_VERSION);
#endif
}

static command_t version = {
	.name = "version",
	.action = version_fn,
	.shorthelp = "Print the hypervisor version",
};
shell_cmd(version);

static void guest_state_str(guest_t *guest, const char **str)
{
	switch (guest->state) {
	case guest_stopped:
		*str = "stopped";
		break;
	case guest_running:
		*str = "running";
		break;
	case guest_starting:
		*str = "starting";
		break;
	case guest_stopping:
		*str = "stopping";
		break;
	case guest_pausing:
		*str = "pausing";
		break;
	case guest_paused:
		*str = "paused";
		break;
	case guest_resuming:
		*str = "resuming";
		break;
	case guest_stopping_percpu:
		*str = "stopping (percpu)";
		break;
	default:
		*str = "unknown";
		break;
	}

	return;
}

static void info_fn(shell_t *shell, char *args)
{
	unsigned int i;
	const char *state_str = NULL;

	qprintf(shell->out, 1, "Partition   Name        State         Vcpus\n");
	qprintf(shell->out, 1, "-------------------------------------------\n");

	for (i = 0; i < num_guests; i++) {
		guest_state_str(&guests[i], &state_str);
		qprintf(shell->out, 1, "%-11d %s", i, guests[i].name);
		if (state_str)
			qprintf(shell->out, 1, "\t%s", state_str);
		qprintf(shell->out, 1, "%10d", guests[i].cpucnt);
		qprintf(shell->out, 1, "\n");
	}
}

static command_t info = {
	.name = "info",
	.action = info_fn,
	.shorthelp = "List partitions and show their status",
};
shell_cmd(info);

static void reset_fn(shell_t *shell, char *args)
{
	system_reset();
}

static command_t reset = {
	.name = "reset",
	.action = reset_fn,
	.shorthelp = "Perform a system reset",
};
shell_cmd(reset);

static void gdt_fn(shell_t *shell, char *args)
{
	char *numstr, *cmdstr;
	int num;
	guest_t *guest;

	args = stripspace(args);
	cmdstr = nextword(&args);
	numstr = nextword(&args);

	if (!numstr || !cmdstr) {
		qprintf(shell->out, 1, "Usage: gdt <cmd> <partition-number>\n");
		return;
	}

	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	qprintf(shell->out, 1, "Partition %u: %s\n", num, guest->name);

	if(!strcmp(cmdstr, "print"))
		dt_print_tree(guest->devtree, shell->out);
}

static command_t gdt = {
	.name = "gdt",
	.action = gdt_fn,
	.shorthelp = "Guest device tree operation",
	.longhelp = "  Usage: gdt <cmd> <partition number>\n\n"
	            "  currently only 'print' command is supported.",
};
shell_cmd(gdt);

static void hdt_fn(shell_t *shell, char *args)
{
	dt_print_tree(hw_devtree, shell->out);
}

static command_t hdt = {
	.name = "hdt",
	.action = hdt_fn,
	.shorthelp = "Display hardware device tree",
	.longhelp = "  Displays the hardware device passed to the hypervisor at boot time.",
};

shell_cmd(hdt);

static void cdt_fn(shell_t *shell, char *args)
{
	dt_print_tree(config_tree, shell->out);
}

static command_t cdt = {
	.name = "cdt",
	.action = cdt_fn,
	.shorthelp = "Display hypervisor configuration tree",
	.longhelp = "  Displays the hypervisor configuration tree.",
};

shell_cmd(cdt);

#ifdef CONFIG_PAMU
#define BUFF_SIZE 64
#define BIT_SHIFT_1P 50
#define BIT_SHIFT_1T 40
#define BIT_SHIFT_1G 30
#define BIT_SHIFT_1M 20
#define BIT_SHIFT_1K 10

static void decode_wse(unsigned int wse, char *str)
{
	unsigned int i = wse - 9;

	if (i >= BIT_SHIFT_1P)
		sprintf(str, "%3d EiB", 1 << (i - BIT_SHIFT_1P));
	else if (i >= BIT_SHIFT_1T)
		sprintf(str, "%3d PiB", 1 << (i - BIT_SHIFT_1T));
	else if (i >= BIT_SHIFT_1G)
		sprintf(str, "%3d TiB", 1 << (i - BIT_SHIFT_1G));
	else if (i >= BIT_SHIFT_1M)
		sprintf(str, "%3d GiB", 1 << (i - BIT_SHIFT_1M));
	else if (i >= BIT_SHIFT_1K)
		sprintf(str, "%3d MiB", 1 << (i - BIT_SHIFT_1K));
	else
		sprintf(str, "%3d KiB", 1 << i);
}

static void spaact_dump_fn(shell_t *shell, paace_t *entry)
{
	int wcount = 0;
	paace_t *sentry;
	int wce = 1 << (get_bf(entry->impl_attr, PAACE_IA_WCE) + 1);
	char str[BUFF_SIZE];
	phys_addr_t subwin_base =
		((phys_addr_t)entry->wbah << 32) | 
		 (get_bf(entry->addr_bitfields, PPAACE_AF_WBAL) << 
		 PAGE_SHIFT);
	phys_addr_t subwin_len =
		(1ULL << (get_bf(entry->addr_bitfields, PPAACE_AF_WSE) + 1)) >> 
		(get_bf(entry->impl_attr, PAACE_IA_WCE) + 1);

	/* The first subwindow is embedded in the primary paace entry.
	 * This would be easier if we used the same struct for both.
	 */
	if (get_bf(entry->addr_bitfields, PAACE_AF_AP) != 
	    PAACE_AP_PERMS_DENIED) {
		decode_wse(get_bf(entry->win_bitfields, PAACE_WIN_SWSE), str);
		qprintf(shell->out, 1, "           %2d   ", 0);
		qprintf(shell->out, 1, "%09llx  ",
		        (unsigned long long)subwin_base);
		qprintf(shell->out, 1, "%01x%08x  ",
			entry->twbah,
			get_bf(entry->win_bitfields, PAACE_WIN_TWBAL) <<
				PAGE_SHIFT);
		qprintf(shell->out, 1, "%s    -      %2d      %d\n",
		        str, get_bf(entry->impl_attr, PAACE_IA_CID),
			get_bf(entry->impl_attr, PAACE_IA_OTM));
	}

	for (int i = 1; i < wce; i++) {
		subwin_base += subwin_len;

		sentry = pamu_get_spaace(entry->fspi, i - 1);
		if (get_bf(sentry->addr_bitfields, PAACE_AF_V)) {
			decode_wse(get_bf(sentry->win_bitfields, PAACE_WIN_SWSE), str);
			qprintf(shell->out, 1, "           %2d   ", i);
			qprintf(shell->out, 1, "%09llx  ",
			        (unsigned long long)subwin_base);
			qprintf(shell->out, 1, "%01x%08x  ",
				sentry->twbah,
				get_bf(sentry->win_bitfields, PAACE_WIN_TWBAL) 
					<< PAGE_SHIFT);
			qprintf(shell->out, 1, "%s    -      %2d      %d\n",
			        str, get_bf(sentry->impl_attr, PAACE_IA_CID),
				get_bf(sentry->impl_attr, PAACE_IA_OTM));
		}
	}
}

static void paact_dump_fn(shell_t *shell, char *args)
{
	int liodn;
	paace_t *entry;
	char str[BUFF_SIZE];
	int wce;
	int is_table_empty = 1;

	/*
	 * Currently all PAMUs in the platform share the same PAACT(s), hence,
	 * this command does not support a PAMU#
	 */
	qprintf(shell->out, 1, "          sub                                  sub   stash  oper.\n");
	qprintf(shell->out, 1, "      val win   base       xlate               win   cache  trans\n");
	qprintf(shell->out, 1, "liodn bit  #    addr       addr       size     cnt   id     mode\n");
	qprintf(shell->out, 1, "-----------------------------------------------------------------\n");

	for (liodn = 0; liodn < PAACE_NUMBER_ENTRIES; liodn++) {
		entry = pamu_get_ppaace(liodn);
		if (entry && get_bf(entry->addr_bitfields, PPAACE_AF_WSE)) {
			is_table_empty = 0;
			memset(str, 0, BUFF_SIZE);
			decode_wse(get_bf(entry->addr_bitfields, PPAACE_AF_WSE), str);

			qprintf(shell->out, 1, "%5d ", liodn);
			qprintf(shell->out, 1, "  %1d", 
				get_bf(entry->addr_bitfields, PAACE_AF_V));

			qprintf(shell->out, 1, "   -   %01x%08x  ",
				entry->wbah,
				get_bf(entry->addr_bitfields, PPAACE_AF_WBAL) 
					<< PAGE_SHIFT);

			if (get_bf(entry->addr_bitfields, PPAACE_AF_MW))
				qprintf(shell->out, 1, "        -  ");
			else
				qprintf(shell->out, 1, "%01x%08x  ",
					entry->twbah,
					get_bf(entry->win_bitfields, PAACE_WIN_TWBAL)
						<< PAGE_SHIFT);

			qprintf(shell->out, 1, "%s   ", str);

			if (get_bf(entry->addr_bitfields, PPAACE_AF_MW)) {
				wce = 1 << (get_bf(entry->impl_attr, PAACE_IA_WCE) + 1);
				qprintf(shell->out, 1, "%2d       -      -\n",
				        wce);
				spaact_dump_fn(shell, entry);
			} else {
				qprintf(shell->out, 1, " 0      %2d      %d\n",
				        get_bf(entry->impl_attr, PAACE_IA_CID),
					get_bf(entry->impl_attr, PAACE_IA_OTM));
			}
		}
	}
	if (is_table_empty)
		qprintf(shell->out, 1, "No entry found\n");
}

static command_t paact = {
	.name = "paact",
	.action = paact_dump_fn,
	.shorthelp = "Display PAMU's PAACT table entries",
};
shell_cmd(paact);
#endif

static void start_fn(shell_t *shell, char *args)
{
	char *str;
	int num, load = 0;
	guest_t *guest;

	args = stripspace(args);
	str = nextword(&args);

	if (str && !strcmp(str, "load")) {
		load = 1;
		str = nextword(&args);
	}

	if (!str) {
		qprintf(shell->out, 1, "Usage: start [load] <partition-number>\n");
		return;
	}

	num = get_partition_num(shell, str);
	if (num == -1)
		return;

	guest = &guests[num];
	if (start_guest(guest, load))
		qprintf(shell->out, 1, "Couldn't start partition.\n");
}

static command_t startcmd = {
	.name = "start",
	.action = start_fn,
	.shorthelp = "Start a stopped partition",
	.longhelp = "  Usage: start [load] <partition-number>\n\n"
	            "  The optional 'load' argument specifies that any images defined\n"
	            "  by the partition are to be loaded.\n\n"
	            "  The partition number can be obtained with the 'info' command.",
};
shell_cmd(startcmd);

static void restart_fn(shell_t *shell, char *args)
{
	char *numstr;
	int num;
	guest_t *guest;

	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, 1, "Usage: restart <partition-number>\n");
		return;
	}

	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];

	if (restart_guest(guest, "restart", "shell"))
		qprintf(shell->out, 1, "Couldn't restart partition.\n");
}

static command_t restart = {
	.name = "restart",
	.action = restart_fn,
	.shorthelp = "Re-start a running partition",
	.longhelp = "  Usage: restart <partition-number>\n\n"
	            "  The partition number can be obtained with the 'info' command.",
};
shell_cmd(restart);

static void stop_fn(shell_t *shell, char *args)
{
	char *numstr;
	int num;
	guest_t *guest;

	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, 1, "Usage: stop <partition-number>\n");
		return;
	}

	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];

	if (stop_guest(guest, "stop", "shell"))
		qprintf(shell->out, 1, "Couldn't stop partition.\n");
}


static command_t stop = {
	.name = "stop",
	.action = stop_fn,
	.shorthelp = "Stop a partition",
	.longhelp = "  Usage: stop <partition-number>\n\n"
	            "  The partition number can be obtained with the 'info' command.",
};
shell_cmd(stop);

static void pause_fn(shell_t *shell, char *args)
{
	char *numstr;
	int num;
	guest_t *guest;

	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, 1, "Usage: pause <partition-number>\n");
		return;
	}

	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (pause_guest(guest))
		qprintf(shell->out, 1, "Couldn't pause partition.\n");
}


static command_t pause = {
	.name = "pause",
	.action = pause_fn,
	.shorthelp = "Pause a running partition",
	.longhelp = "  Usage: pause <partition-number>\n\n"
	            "  Instruction execution is suspended on all CPUs of a paused partition.\n\n"
	            "  The partition number can be obtained with the 'info' command.",
};
shell_cmd(pause);

static void resume_fn(shell_t *shell, char *args)
{
	char *numstr;
	int num;
	guest_t *guest;

	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, 1, "Usage: resume <partition-number>\n");
		return;
	}

	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (resume_guest(guest))
		qprintf(shell->out, 1, "Couldn't resume partition.\n");
}


static command_t resume = {
	.name = "resume",
	.action = resume_fn,
	.shorthelp = "Resume a paused partition",
	.longhelp = "  Usage: resume <partition-number>\n\n"
	            "  The partition number can be obtained with the 'info' command.",
};
shell_cmd(resume);

#ifdef CONFIG_TLB_CACHE

#ifndef CONFIG_LIBOS_64BIT
#define VADDR_WIDTH	"8"
#define PADDR_WIDTH	"9"
#else
#define VADDR_WIDTH	"16"
#define PADDR_WIDTH	"16"
#endif

enum mas3_perm {supervisor = 0, user};
enum mas_regs {mas2_reg = 0, mas3_reg};

static void extract_mas_flags(uint32_t val, const char *str, char *buf, int reg, int perm)
{
	int len, j;

	len = strlen(str);
	for (j = 0; j < len; j++) {
		int shift = (reg == mas2_reg ? j : j * 2 + perm);
		if (val & (1 << shift))
			buf[(len - 1) - j] = str[(len - 1) - j];
		else
			buf[(len - 1) - j] = ' ';
	}
	buf[j] = 0;
}

static void dump_tlb(shell_t *shell, gcpu_t *gcpu)
{
	tlb_entry_t gmas;
	uint32_t flags;
	int rc, tlb_num;
	phys_addr_t paddr;
	unsigned long vaddr, size;
	char buf[16];

	for (tlb_num = 0; tlb_num < 2; tlb_num++) {
		memset(&gmas, 0, sizeof(tlb_entry_t));
		flags = TLB_READ_FIRST;
		qprintf(shell->out, 1, "TLB %d \n", tlb_num);
		qprintf(shell->out, 1, "					             ");
		qprintf(shell->out, 1, " T             SSS UUU I V\n");
		qprintf(shell->out, 1, "          Effective                Physical           S  TID");
		qprintf(shell->out, 1, "  WIMGE XWR XWR P F\n");
		qprintf(shell->out, 1, "    ----------------------- ------------------------- - -----");
		qprintf(shell->out, 1, " ----- --- --- - -\n");

		gmas.mas0 = MAS0_TLBSEL(tlb_num);
		while (1) {
			rc = guest_tlb_read_vcpu(&gmas, &flags, gcpu);
			if (rc < 0)
				break;
			if (!(gmas.mas1 & MAS1_VALID))
				continue;

			paddr = ((uint64_t) gmas.mas7 << 32) |
				(gmas.mas3 & ~(PAGE_SIZE - 1));
			vaddr = gmas.mas2 & ~((register_t)PAGE_SIZE - 1);
			size = (tsize_to_pages(MAS1_GETTSIZE(gmas.mas1)) << PAGE_SHIFT) - 1;

			qprintf(shell->out, 1, "%02u  ",
				(tlb_num ? (int) MAS0_GET_TLB1ESEL(gmas.mas0) :
							(int) MAS0_GET_TLB0ESEL(gmas.mas0)));

			qprintf(shell->out, 1,
				"0x%0" VADDR_WIDTH "lx - 0x%0" VADDR_WIDTH "lx ",
				vaddr, vaddr + size);

			qprintf(shell->out, 1,
				"0x%0" PADDR_WIDTH "llx - 0x%0" PADDR_WIDTH "llx",
				paddr, paddr + size);
			qprintf(shell->out, 1, " %1d", (int)((gmas.mas1 & MAS1_TS) >> MAS1_TS_SHIFT));
			qprintf(shell->out, 1, "%6d", (int)((gmas.mas1 & MAS1_TID_MASK) >> MAS1_TID_SHIFT));
			extract_mas_flags(gmas.mas2, "WIMGE", buf, mas2_reg, 0);
			qprintf(shell->out, 1, " %s", buf);
			extract_mas_flags(gmas.mas3, "XWR", buf, mas3_reg, supervisor);
			qprintf(shell->out, 1, " %s", buf);
			extract_mas_flags(gmas.mas3, "XWR", buf, mas3_reg, user);
			qprintf(shell->out, 1, " %s", buf);
			qprintf(shell->out, 1, " %1d", (int)((gmas.mas1 & MAS1_IPROT) >> MAS1_IPROT_SHIFT));
			qprintf(shell->out, 1, " %1d\n", (int)((gmas.mas8 & MAS8_VF) >> MAS8_VF_SHIFT));
		}

		qprintf(shell->out, 1, "\n\n");
	}
}

static void gtlb_fn(shell_t *shell, char *args)
{
	int guest_num, vcpu_num;
	char *gueststr, *vcpustr;
	cpu_t *old_cpu = NULL;

	args = stripspace(args);
	gueststr = nextword(&args);
	vcpustr = nextword(&args);

	if (!gueststr || !vcpustr) {
		qprintf(shell->out, 1, "Usage: gtlb <guest#> <vcpu#>\n");
		return;
	}

	guest_num = get_partition_num(shell, gueststr);
	if (guest_num == -1 ) {
		qprintf(shell->out, 1, "Invalid guest number\n");
		return;
	}

	if (guests[guest_num].state != guest_paused) {
		qprintf(shell->out, 1, "Can't display tlb, guest not paused\n");
		return;
	}

	vcpu_num = get_number32(shell->out, vcpustr);

	if (vcpu_num >= guests[guest_num].cpucnt) {
		qprintf(shell->out, 1, "Invalid vcpu number\n");
		return;
	}

	dump_tlb(shell, guests[guest_num].gcpus[vcpu_num]);
}

static command_t gtlb = {
	.name = "gtlb",
	.action = gtlb_fn,
	.shorthelp = "Display guest tlb entries",
	.longhelp = "  Usage: gtlb <partition-number> <vcpu-number>\n\n"
	            "  The partition number and number of vcpus in a partition\n"
	            "  can be obtained with the 'info' command.",
};
shell_cmd(gtlb);

#endif

#ifdef CONFIG_STATISTICS
#define MICRO_BENCHMARK_START bm_tlb0_inv_pid
extern const char *benchmark_names[];

static unsigned long tb_to_nsec(uint64_t freq, unsigned long ticks)
{
	return ticks * 1000000000ULL / freq;
}

static void print_stats(shell_t *shell, gcpu_t *gcpu, int start, int end, int total_flag)
{
	char *tmp;
	int len, cplen;
	unsigned long total = 0;
	uint64_t freq = dt_get_timebase_freq();

	qprintf(shell->out, 1, "Event                      Total(ns)    Avg(ns)    Min(ns)    Max(ns)    Count\n");
	qprintf(shell->out, 1, "-------------------------------------------------------------------------------\n");

	for (benchmark_num_t i = start; i < end; i++) {
		benchmark_t *bm = &gcpu->benchmarks[i];
		qprintf(shell->out, 1, "%-24s %10lu %10lu %10lu %10lu %10lu\n",
			benchmark_names[i],
			tb_to_nsec(freq, bm->accum),
			bm->num ? tb_to_nsec(freq, bm->accum / bm->num) : 0,
			tb_to_nsec(freq, bm->min),
			tb_to_nsec(freq, bm->max), bm->num);
		total += tb_to_nsec(freq, bm->accum);
	}

	if (total_flag) {
		qprintf(shell->out, 1, "-------------------------------------------------------------------------------\n");
		qprintf(shell->out, 1, "TOTAL                    %10lu\n", total);
	}
}

static void dump_stats(shell_t *shell, int num)
{
	guest_t *guest;
	int i;

	if (num_benchmarks == 0) {
		qprintf(shell->out, 1, "No benchmarks defined.\n");
		return;
	}

	guest = &guests[num];
	qprintf(shell->out, 1, "Guest: %s\n", guest->name);
	for (i = 0; i < guest->cpucnt; i++) {
		gcpu_t *gcpu = guest->gcpus[i];
		qprintf(shell->out, 1, "guest gcpu: %d\n", i);
		print_stats(shell, gcpu, 0, MICRO_BENCHMARK_START, 1);
	#ifdef CONFIG_BENCHMARKS
		qprintf(shell->out, 1, "\nMicro Benchmarks:\n");
		print_stats(shell, gcpu, MICRO_BENCHMARK_START, num_benchmarks, 0);
	#endif
	}
	qprintf(shell->out, 1, "\n");
}

static void clear_stats(int num)
{
	guest_t *guest;
	int i;

	guest = &guests[num];
	for (i = 0; i < guest->cpucnt; i++) {
		gcpu_t *gcpu = guest->gcpus[i];
		for (benchmark_num_t i = 0; i < num_benchmarks; i++) {
			benchmark_t *bm = &gcpu->benchmarks[i];
			memset(bm, 0, sizeof(benchmark_t));
		}
	}
}

static void stats_fn(shell_t *shell, char *args)
{
	char *numstr, *cmdstr;
	int num;

	args = stripspace(args);
	cmdstr = nextword(&args);
	numstr = nextword(&args);

	if (!numstr || !cmdstr) {
		qprintf(shell->out, 1, "Usage: stats <command> <partition-number>\n");
		return;
	}

	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	if (!strcmp(cmdstr, "print"))
		dump_stats(shell, num);
	else if (!strcmp(cmdstr, "clear"))
		clear_stats(num);
}

static command_t stats = {
	.name = "stats",
	.action = stats_fn,
	.shorthelp = "Print statistics/microbenchmark information",
	.longhelp = "  Usage: stats <cmd> <partition number>\n\n"
	            "  'print' & 'clear' commands are supported.",
};
shell_cmd(stats);
#endif

static void guestmem_fn(shell_t *shell, char *args)
{
	int guest;
	char *gueststr, *addrstr, *lenstr;
	char buf[16];
	phys_addr_t address, length;

	args = stripspace(args);
	gueststr = nextword(&args);
	addrstr = nextword(&args);
	lenstr = nextword(&args);

	if (!gueststr || !addrstr) {
		qprintf(shell->out, 1, "Usage: guestmem <partition> <address> [<length>]\n");
		return;
	}

	guest = get_partition_num(shell, gueststr);
	if (guest < 0) {
		qprintf(shell->out, 1, "Invalid guest number\n");
		return;
	}

	address = get_number64(shell->out, addrstr);

	if (lenstr) {
		length = get_number64(shell->out, lenstr);
		if (length > 1024*1024) {
			qprintf(shell->out, 1, "Invalid length\n");
			return;
		}
	} else {
		length = 256;
	}

	while (length > 0) {
		int chunk = 16;
		int good;
	
		if (address & 15)
			chunk -= address & 15;
		if (chunk > length)
			chunk = length;
	
		good = copy_from_gphys(guests[guest].gphys, buf + (address & 15),
		                       address, chunk);
		if (good == 0)
			goto bad;

		qprintf(shell->out, 1, "%08llx: ", address & ~15ULL);
		
		for (int i = 0; i < 16; i++) {
			if (i < (address & 15) || i >= (address & 15) + good)
				qprintf(shell->out, 1, "   ");
			else
				qprintf(shell->out, 1, "%02x ", buf[i]);
		}
		
		qprintf(shell->out, 1, " |  ");

		for (int i = 0; i < 16; i++) {
			if (i < (address & 15) || i >= (address & 15) + good ||
			    buf[i] < 0x20 || buf[i] > 0x7f)
				qprintf(shell->out, 1, ".");
			else
				qprintf(shell->out, 1, "%c", buf[i]);
		}
		
		qprintf(shell->out, 1, "\n");
		
bad:
		address = (address + 16) & ~15ULL;
		length -= chunk;
	}
}

static command_t guestmem = {
	.name = "guestmem",
	.action = guestmem_fn,
	.aliases = (const char *[]){ "gm", NULL },
	.shorthelp = "Dump guest memory",
	.longhelp = "  Usage: guestmem <partition-number> <address> [<length>]\n\n",
};
shell_cmd(guestmem);


static void error_policy_dump_fn(shell_t *shell, char *args)
{
	int i, j;
	error_policy_t *error_p;

	qprintf(shell->out, 1, "error\n");
	qprintf(shell->out, 1, "domain            error               policy\n");
	qprintf(shell->out, 1, "---------------------------------------------\n");

	/* get pointer to errors for this domain */
	for (i = 0; i < ERROR_DOMAIN_COUNT; i++) {

		error_p = error_domains[i].errors;

		for (j = 0; j < error_domains[i].error_count; j++) {
			qprintf(shell->out, 1, "%5s %30s %8s\n",
			        error_domains[i].domain,
				error_p[j].error,
				error_p[j].policy);
		}
	}
}

static command_t error_policy = {
	.name = "error_policy",
	.action = error_policy_dump_fn,
	.shorthelp = "Display error policies",
};
shell_cmd(error_policy);

#ifdef CONFIG_BUILD_CONFIG
extern const unsigned char build_config_data[];

static void build_config_dump_fn(shell_t *shell, char *args)
{
	const unsigned char *p = build_config_data;

	while (*p) {
		if (*p == '\n')
			queue_writechar_blocking(shell->out, '\r');
		queue_writechar_blocking(shell->out, (*p++));
	}
}

static command_t build_config = {
	.name = "buildconfig",
	.action = build_config_dump_fn,
	.aliases = (const char *[]){ "bc", NULL },
	.shorthelp = "Display build time configuration",
};
shell_cmd(build_config);
#endif

#ifdef CONFIG_HV_WATCHDOG
static void crash_fn(shell_t *shell, char *args)
{
	*(volatile unsigned int *)0 = 1;   /* null pointer dereference */
}

static command_t crash = {
	.name = "crash",
	.action = crash_fn,
	.shorthelp = "Crash the hypervisor.  Used for testing.",
};
shell_cmd(crash);
#endif

typedef struct log_type {
	const char *namestr;
	int id;
} log_type_t;

static struct log_type groups[] =
{
	{"misc", LOGTYPE_MISC},
	{"mmu", LOGTYPE_MMU},
	{"irq", LOGTYPE_IRQ},
	{"mp", LOGTYPE_MP},
	{"malloc", LOGTYPE_MALLOC},
	{"dev", LOGTYPE_DEV},
	{"guest-mmu", LOGTYPE_GUEST_MMU},
	{"emu", LOGTYPE_EMU},
	{"partition", LOGTYPE_PARTITION},
	{"debug-stub", LOGTYPE_DEBUG_STUB},
	{"byte-chan", LOGTYPE_BYTE_CHAN},
	{"doorbell", LOGTYPE_DOORBELL},
	{"bcmux", LOGTYPE_BCMUX},
	{"devtree", LOGTYPE_DEVTREE},
	{"pamu", LOGTYPE_PAMU},
	{"ccm", LOGTYPE_CCM},
	{"cpc", LOGTYPE_CPC},
	{"guts", LOGTYPE_GUTS},
	{"pm", LOGTYPE_PM},
	{"ddr", LOGTYPE_DDR},
	{"errorq", LOGTYPE_ERRORQ}
};
#define GROUPS_COUNT (sizeof(groups)/sizeof(struct log_type))

static int str_to_loggroup(const char *groupstr)
{
	int index;

	for (index = 0; index < GROUPS_COUNT; index++)
		if (!strcmp(groups[index].namestr, groupstr))
			return groups[index].id;

	return -1;
}

static void set_loglevel(shell_t *shell, char *args)
{
	char *groupstr, *levelstr;
	int group, index;
	uint32_t level;

	args = stripspace(args);
	groupstr = nextword(&args);
	levelstr = nextword(&args);

	if (!groupstr) {
		qprintf(shell->out, 1, "Group             Loglevel\n");
		qprintf(shell->out, 1, "---------------------------------\n");
		for (index = 0; index < GROUPS_COUNT; index++)
			qprintf(shell->out, 1, "%-20s %d\n", groups[index].namestr, loglevels[groups[index].id]);
		return;
	}
	group = str_to_loggroup(groupstr);
	if (group < 0) {
		qprintf(shell->out, 1, "Invalid log group\n");
		return;
	}
	if (!levelstr) {
		qprintf(shell->out, 1, "Level for group %s: %d\n", groupstr, loglevels[group]);
		return;
	}

	level = get_number32(shell->out, levelstr);
	if ((cpu->errno) || (level > CONFIG_LIBOS_MAX_BUILD_LOGLEVEL)) {
		qprintf(shell->out, 1, "Invalid log level (valid values: 0-%d)\n", CONFIG_LIBOS_MAX_BUILD_LOGLEVEL);
		return;
	}
	loglevels[group] = level;
}

static command_t loglevel = {
	.name = "loglevel",
	.action = set_loglevel,
	.aliases = (const char *[]){"ll", NULL },
	.shorthelp = "Control the log level for a specific log type",
	.longhelp = "Usage: loglevel [<group>] [<level>] \n"
	            "Setting higher levels may be dangerous (e.g enabling "
	            "debug on bcmux when HV console is on mux)"
};
shell_cmd(loglevel);

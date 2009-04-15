/** @file
 * Command line shell
 */
/* Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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

#include <libos/readline.h>
#include <libos/pamu.h>

#include <errors.h>
#include <devtree.h>
#include <shell.h>
#include <percpu.h>

#include <limits.h>

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

	if (num >= last_lpid) {
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
			qprintf(shell->out, 1, " %s", cmd->name);
			print_aliases(shell, cmd);
			qprintf(shell->out, 1, " - %s\n",cmd->shorthelp);
		}
		
		return;
	}
	
	cmd = find_command(cmdname);
	if (!cmd) {
		qprintf(shell->out, 1, "help: unknown command '%s'.\n", cmdname);
		return;
	}
	
	qprintf(shell->out, 1, " %s", cmd->name);
	print_aliases(shell, cmd);
	qprintf(shell->out, 1, " - %s\n",cmd->shorthelp);

	if (cmd->longhelp)
		qprintf(shell->out, 1, "\n%s\n", cmd->longhelp);
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
	printf("Freescale Embedded Hypervisor version %s\n", CONFIG_HV_VERSION);
}

static command_t version = {
	.name = "version",
	.action = version_fn,
	.shorthelp = "Print the hypervisor version",
};
shell_cmd(version);

static void lp_fn(shell_t *shell, char *args)
{
	unsigned int i;
	
	qprintf(shell->out, 1, "Partition   Name\n");
	
	for (i = 0; i < last_lpid; i++)
		qprintf(shell->out, 1, "%-11d %s\n", i, guests[i].name);
}

static command_t lp = {
	.name = "list-partitions",
	.aliases = (const char *[]){ "lp", NULL },
	.action = lp_fn,
	.shorthelp = "List partitions",
};
shell_cmd(lp);


#ifdef CONFIG_STATISTICS
static void print_stat(shell_t *shell, guest_t *guest,
                       int stat, const char *str)
{
	unsigned int i;
	unsigned int total = 0;
	
	for (i = 0; i < guest->cpucnt; i++) {
		gcpu_t *gcpu = guest->gcpus[i];
		
		if (gcpu)
			total += guest->gcpus[i]->stats[stat];
	}

	qprintf(shell->out, 1, "%s %-10u\n", str, total);
}

static void pi_fn(shell_t *shell, char *args)
{
	char *numstr;
	int num;
	guest_t *guest;
	
	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, 1, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	qprintf(shell->out, 1, "Partition %u: %s\n", num, guest->name);
	print_stat(shell, guest, stat_emu_total,
	           "Total emulated instructions:          ");
	print_stat(shell, guest, stat_emu_tlbwe,
	           "Emulated TLB writes:                  ");
	print_stat(shell, guest, stat_emu_spr,
	           "Emulated SPR accesses:                ");
	print_stat(shell, guest, stat_decr,
	           "Decrementer interrupts:               ");
	print_stat(shell, guest, stat_emu_tlbivax,
	           "Emulated TLB invalidates:             ");
	print_stat(shell, guest, stat_emu_msgsnd,
	           "Emulated msgsnd instructions:         ");
	print_stat(shell, guest, stat_emu_msgclr,
	           "Emulated msgclr instructions:         ");
	print_stat(shell, guest, stat_emu_tlbilx,
	           "Emulated tlbilx instructions:         ");
	print_stat(shell, guest, stat_emu_tlbre,
	           "Emulated TLB reads:                   ");
	print_stat(shell, guest, stat_emu_tlbsx,
	           "Emulated TLB searches:                ");
	print_stat(shell, guest, stat_emu_tlbsync,
	           "Emulated TLB syncs:                   ");
	print_stat(shell, guest, stat_emu_tlbivax_tlb0_all,
	           "Emulated TLB invalidate all for tlb0: ");
	print_stat(shell, guest, stat_emu_tlbivax_tlb0,
	           "Emulated TLB invalidate for tlb0:     ");
	print_stat(shell, guest, stat_emu_tlbivax_tlb1_all,
	           "Emulated TLB invalidate all for tlb1: ");
	print_stat(shell, guest, stat_emu_tlbivax_tlb1,
	           "Emulated TLB invalidate for tlb1:     ");
	print_stat(shell, guest, stat_emu_tlbwe_tlb0,
	           "Emulated TLB writes for tlb0:         ");
	print_stat(shell, guest, stat_emu_tlbwe_tlb1,
	           "Emulated TLB writes for tlb1:         ");
#ifdef CONFIG_TLB_CACHE
	print_stat(shell, guest, stat_tlb_miss_count,
	           "Total TLB miss interrupts handled:    ");
	print_stat(shell, guest, stat_tlb_miss_reflect,
	           "TLB miss interrupts reflected:        ");
#endif
}

static command_t pi = {
	.name = "partition-info",
	.aliases = (const char *[]){ "pi", NULL },
	.action = pi_fn,
	.shorthelp = "Display information about a partition",
	.longhelp = "  Usage: partition-info <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
};
shell_cmd(pi);
#endif

static void gdt_fn(shell_t *shell, char *args)
{
	char *numstr, *cmdstr;
	int num;
	guest_t *guest;

	args = stripspace(args);
	cmdstr = nextword(&args);
	numstr = nextword(&args);

	if (!numstr || !cmdstr) {
		qprintf(shell->out, 1, "Usage: guest-device-tree <cmd> <number>\n");
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
	.name = "guest-device-tree",
	.aliases = (const char *[]){ "gdt", NULL },
	.action = gdt_fn,
	.shorthelp = "Guest device tree operation",
	.longhelp = "  Usage: guest-device-tree <cmd> <partition number>\n\n"
	            "  currently only print command is supported.",
};
shell_cmd(gdt);

static void mdt_fn(shell_t *shell, char *args)
{
	dt_print_tree(hw_devtree, shell->out);
}

static command_t mdt = {
	.name = "master-device-tree",
	.aliases = (const char *[]){ "mdt", NULL },
	.action = mdt_fn,
	.shorthelp = "Display master device tree",
};

shell_cmd(mdt);

#ifdef CONFIG_PAMU
#define BUFF_SIZE 64
#define BIT_SHIFT_1P 50
#define BIT_SHIFT_1T 40
#define BIT_SHIFT_1G 30
#define BIT_SHIFT_1M 20
#define BIT_SHIFT_1K 10

static void decode_wse(unsigned int wse, char *str)
{
	unsigned int i = 0;

	i = wse - 10;

	if (i >= BIT_SHIFT_1P)
		sprintf(str, "%d EB", 1<<(i-BIT_SHIFT_1P+1));
	else if (i >= BIT_SHIFT_1T)
		sprintf(str, "%d PB", 1<<(i-BIT_SHIFT_1T+1));
	else if (i >= BIT_SHIFT_1G)
		sprintf(str, "%d TB", 1<<(i-BIT_SHIFT_1G+1));
	else if (i >= BIT_SHIFT_1M)
		sprintf(str, "%d GB", 1<<(i-BIT_SHIFT_1M+1));
	else if (i >= BIT_SHIFT_1K)
		sprintf(str, "%d MB", 1<<(i-BIT_SHIFT_1K+1));
	else
		sprintf(str, "%d KB", 1<<(i+1));
}

static void ppaace_entry_dump_fn(shell_t *shell, ppaace_t *entry)
{
	qprintf(shell->out, 1, "\n\tStash Cache id: %d",
		entry->impl_attr.cid);
	qprintf(shell->out, 1, "\n\tOperation Translation Mode:");
	if (!entry->otm)
		qprintf(shell->out, 1, "\n\t\t0:"
			"No operation Translation\n");
	else {
		if (entry->otm == 1)
			qprintf(shell->out, 1, "\n\t\t1:"
				"Immediate Operation Mode\n");

		if (entry->otm == 2) {
			qprintf(shell->out, 1, "\n\t\t2:"
				"Indexed Translation Mode");
			qprintf(shell->out, 1, "\n\t\t\tOMI: %d\n",
				entry->op_encode.index_ot.omi);
		}
	}
}

static void spaace_entry_dump_fn(shell_t *shell, spaace_t *sentry)
{
	char str[BUFF_SIZE];

	memset(str, 0, BUFF_SIZE);
	decode_wse(sentry->swse, str);
	qprintf(shell->out, 1, "\tTranslated Window Base Address %x"
		"\n\t\tsize: %s",
		sentry->twbal << PAGE_SHIFT, str);
	qprintf(shell->out, 1, "\n\t\tStash Cache id: %d",
		sentry->impl_attr.cid);
	qprintf(shell->out, 1, "\n\t\tOperation Translation Mode:");
	if (!sentry->otm)
		qprintf(shell->out, 1, "\n\t\t\t0:"
			"No operation Translation\n");
	else {
		if (sentry->otm == 1)
			qprintf(shell->out, 1, "\n\t\t1:"
				"Immediate Operation Mode\n");

		if (sentry->otm == 2) {
			qprintf(shell->out, 1, "\n\t\t2:"
				"Indexed Translation Mode");
			qprintf(shell->out, 1, "\n\t\t\tOMI: %d\n",
				sentry->op_encode.index_ot.omi);
		}
	}
}

static void spaact_dump_fn(shell_t *shell, ppaace_t *entry)
{
	int wcount = 0;
	spaace_t *sentry;
	int wce;

	qprintf(shell->out, 1, "\n\tSubwindows:");
	if (entry->swse) {
		char str[BUFF_SIZE];

		memset(str, 0, BUFF_SIZE);
		decode_wse(entry->swse, str);
		qprintf(shell->out, 1, "\n\t%d\tsize: %s",
			wcount, str);
	}

	wce = 2 * (1 << entry->wce);
	for (int i = 0; i < wce; i++) {
		sentry = pamu_get_spaace(entry->fspi, i);
		if (sentry->v) {
			wcount++;
			qprintf(shell->out, 1, "\n\t%d", wcount);
			spaace_entry_dump_fn(shell, sentry);
		}
	}
}

static void paact_dump_fn(shell_t *shell, char *args)
{
	int liodn;
	ppaace_t *entry;
	char str[BUFF_SIZE];

	/*
	 * Currently all PAMUs in the platform share the same PAACT(s), hence,
	 * this command does not support a PAMU#
	 */

	for (liodn = 0; liodn < PAACE_NUMBER_ENTRIES; liodn++) {
		entry = pamu_get_ppaace(liodn);
		if (entry && entry->v) {
			memset(str, 0, BUFF_SIZE);
			decode_wse(entry->wse, str);
			qprintf(shell->out, 1, "liodn#: %d(0x%x)"
				"\n\tWindow Base Address: 0x%x"
				"\n\tTranslated Window Base Address: 0x%x"
				"\n\tsize: %s",
				liodn, liodn, (entry->wbal << PAGE_SHIFT),
				(entry->twbal << PAGE_SHIFT), str);
			ppaace_entry_dump_fn(shell, entry);
			if (entry->mw)
				spaact_dump_fn(shell, entry);
		}
	}
}

static command_t paact = {
	.name = "paact",
	.action = paact_dump_fn,
	.shorthelp = "Dump PAMU's PAACT table entries",
};
shell_cmd(paact);
#endif

static void start_fn(shell_t *shell, char *args)
{
	char *numstr;
	int num;
	guest_t *guest;
	
	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, 1, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (start_guest(guest))
		qprintf(shell->out, 1, "Couldn't start partition.\n");
}

static command_t startcmd = {
	.name = "start",
	.action = start_fn,
	.shorthelp = "Start a partition",
	.longhelp = "  Usage: start <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
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
		qprintf(shell->out, 1, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (restart_guest(guest))
		qprintf(shell->out, 1, "Couldn't restart partition.\n");
}

static command_t restart = {
	.name = "restart",
	.action = restart_fn,
	.shorthelp = "Start a partition",
	.longhelp = "  Usage: restart <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
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
		qprintf(shell->out, 1, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (stop_guest(guest))
		qprintf(shell->out, 1, "Couldn't stop partition.\n");
}


static command_t stop = {
	.name = "stop",
	.action = stop_fn,
	.shorthelp = "Stop a partition",
	.longhelp = "  Usage: stop <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
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
		qprintf(shell->out, 1, "Usage: partition-info <number>\n");
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
	.shorthelp = "Stop a partition",
	.longhelp = "  Usage: pause <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
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
		qprintf(shell->out, 1, "Usage: partition-info <number>\n");
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
	.shorthelp = "Stop a partition",
	.longhelp = "  Usage: resume <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
};
shell_cmd(resume);

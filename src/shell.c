/** @file
 * Command line shell
 */
/* Copyright (C) 2008 Freescale Semiconductor, Inc.
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
	int num;

	num = get_number32(shell->out, numstr);
	if (cpu->errno)
		return -1;

	if (num >= last_lpid) {
		qprintf(shell->out, "Partition %u does not exist.\n", num);
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
		qprintf(shell->out, "Unknown command '%s'.\n", cmdname);

	return 0;
}

void shell_init(void)
{
	open_stdin();
	
	if (stdin && stdout) {
		shell_t *shell = alloc_type(shell_t);
		if (!shell)
			return;
		
		shell->rl = readline_init(stdin, stdout, "HV> ", shell_action, shell);
		shell->out = stdout;
		rl_console = shell->rl;
		readline_resume(shell->rl);
	}
}

static void print_aliases(shell_t *shell, command_t *cmd)
{
	if (cmd->aliases) {
		const char **a = cmd->aliases;
		
		qprintf(shell->out, " (");
		while (*a) {
			qprintf(shell->out, "%s", *a);
			a++;
			if (*a)
				qprintf(shell->out, ",");
		}
		qprintf(shell->out, ")");
	}
}

static void help_fn(shell_t *shell, char *args)
{
	command_t *cmd;
	const char *cmdname = nextword(&args);

	if (!cmdname) {
		command_t **i, *cmd;

		qprintf(shell->out, "Commands:\n");

		for (i = &shellcmd_begin; i < &shellcmd_end; i++) {
			cmd = *i;
			qprintf(shell->out, " %s", cmd->name);
			print_aliases(shell, cmd);
			qprintf(shell->out, " - %s\n",cmd->shorthelp);
		}
		
		return;
	}
	
	cmd = find_command(cmdname);

	if (!cmd) {
		qprintf(shell->out, "help: unknown command '%s'.\n", cmdname);
		return;
	}
	
	qprintf(shell->out, " %s", cmd->name);
	print_aliases(shell, cmd);
	qprintf(shell->out, " - %s\n",cmd->shorthelp);

	if (cmd->longhelp)
		qprintf(shell->out, "\n%s\n", cmd->longhelp);
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
	printf("Topaz Hypervisor version %s\n", CONFIG_HV_VERSION);
}

static command_t version = {
	.name = "version",
	.action = version_fn,
	.shorthelp = "Print the hypervisor version",
};
shell_cmd(version);

static void lp_fn(shell_t *shell, char *args)
{
	int i;
	
	qprintf(shell->out, "Partition   Name\n");
	
	for (i = 0; i < last_lpid; i++)
		qprintf(shell->out, "%-11d %s\n", i, guests[i].name);
}

static command_t lp = {
	.name = "list-partitions",
	.aliases = (const char *[]){ "lp", NULL },
	.action = lp_fn,
	.shorthelp = "List partitions",
};
shell_cmd(lp);

static void print_stat(shell_t *shell, guest_t *guest,
                       int stat, const char *str)
{
	int i;
	unsigned int total = 0;
	
	for (i = 0; i < guest->cpucnt; i++) {
		gcpu_t *gcpu = guest->gcpus[i];
		
		if (gcpu)
			total += guest->gcpus[i]->stats[stat];
	}

	qprintf(shell->out, "%s %-10u\n", str, total);
}

static void pi_fn(shell_t *shell, char *args)
{
	char *numstr;
	unsigned int num;
	guest_t *guest;
	
	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	qprintf(shell->out, "Partition %u: %s\n", num, guest->name);
	print_stat(shell, guest, stat_emu_total,
	           "Total emulated instructions: ");
	print_stat(shell, guest, stat_emu_tlbwe,
	           "Emulated TLB writes:         ");
	print_stat(shell, guest, stat_emu_spr,
	           "Emulated SPR accesses:       ");
	print_stat(shell, guest, stat_decr,
	           "Decrementer interrupts:      ");
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

static void gdt_fn(shell_t *shell, char *args)
{
	char *numstr, *cmdstr;
	unsigned int num;
	guest_t *guest;

	args = stripspace(args);
	cmdstr = nextword(&args);
	numstr = nextword(&args);

	if (!numstr || !cmdstr) {
		qprintf(shell->out, "Usage: guest-device-tree <cmd> <number>\n");
		return;
	}

	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	qprintf(shell->out, "Partition %u: %s\n", num, guest->name);

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
		entry = get_ppaace(liodn);
		if (entry && entry->v) {
			memset(str, 0, BUFF_SIZE);
			decode_wse(entry->wse, str);
			qprintf(shell->out, "liodn#: %d(0x%x)"
				"\n\tWindow Base Address: 0x%x"
				"\n\tTranslated Window Base Address: 0x%x"
				"\n\tsize: %s\n\n",
				liodn, liodn, (entry->wbal << PAGE_SHIFT),
				(entry->twbal << PAGE_SHIFT), str);
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
	unsigned int num;
	guest_t *guest;
	
	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (start_guest(guest))
		qprintf(shell->out, "Couldn't start partition.\n");
}

static command_t start = {
	.name = "start",
	.action = start_fn,
	.shorthelp = "Start a partition",
	.longhelp = "  Usage: start <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
};
shell_cmd(start);

static void restart_fn(shell_t *shell, char *args)
{
	char *numstr;
	unsigned int num;
	guest_t *guest;
	
	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (restart_guest(guest))
		qprintf(shell->out, "Couldn't restart partition.\n");
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
	unsigned int num;
	guest_t *guest;
	
	args = stripspace(args);
	numstr = nextword(&args);

	if (!numstr) {
		qprintf(shell->out, "Usage: partition-info <number>\n");
		return;
	}
	
	num = get_partition_num(shell, numstr);
	if (num == -1)
		return;

	guest = &guests[num];
	if (stop_guest(guest))
		qprintf(shell->out, "Couldn't stop partition.\n");
}


static command_t stop = {
	.name = "stop",
	.action = stop_fn,
	.shorthelp = "Stop a partition",
	.longhelp = "  Usage: stop <number>\n\n"
	            "  The partition number can be obtained with list-partitions.",
};
shell_cmd(stop);

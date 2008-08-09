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

char *stripspace(const char *str)
{
	if (!str)
		return NULL;

	while (*str && *str == ' ')
		str++;

	if (!*str)
		return NULL;

	return (char *)str;
}

char *nextword(char **str)
{
	char *ret;

	if (!*str)
		return NULL;
	
	ret = stripspace(*str);
	if (!ret)
		return NULL;
	
	*str = strchr(ret, ' ');

	if (*str) {
		**str = 0;
		(*str)++;
	}
	
	return ret;
}

static int print_num_error(shell_t *shell, char *endp, const char *numstr)
{
	if (cpu->errno) {
		if (cpu->errno == ERR_RANGE)
			qprintf(shell->out, "Number exceeds range: %s\n", numstr);
		else if (cpu->errno == ERR_INVALID)
			qprintf(shell->out, "Unrecognized number format: %s\n", numstr);
		else
			qprintf(shell->out, "get_number: error %d: %s\n", cpu->errno, numstr);

		return 1;
	}

	if (endp && *endp) {
		qprintf(shell->out, "Trailing junk after number: %s\n", numstr);
		cpu->errno = ERR_INVALID;
		return 1;
	}
	
	return 0;
}

static int get_base(shell_t *shell, const char *numstr, int *skip)
{
	*skip = 0;

	if (numstr[0] == '0') {
		if (numstr[1] == 0)
			return 10;
	
		if (numstr[1] == 'x') {
			*skip = 2;
			return 16;
		}

		if (numstr[1] == 'b') {
			*skip = 2;
			return 2;
		}
		
		if (numstr[1] >= '0' && numstr[1] <= '7') {
			*skip = 1;
			return 8;
		}

		qprintf(shell->out, "Unrecognized number format: %s\n", numstr);
		cpu->errno = ERR_INVALID;
		return 0;
	}
	
	return 10;
}

uint64_t get_number64(shell_t *shell, const char *numstr)
{
	uint64_t ret;
	char *endp;
	int skip, base;

	if (numstr[0] == '-') {
		cpu->errno = ERR_RANGE;
		qprintf(shell->out, "Number exceeds range: %s\n", numstr);
		return 0;
	}

	base = get_base(shell, numstr, &skip);
	if (!base)
		return 0;

	ret = strtoull(&numstr[skip], &endp, base);

	if (print_num_error(shell, endp, numstr))
		return 0;

	return ret;
}

/* Only decimal numbers may be negative */
int64_t get_snumber64(shell_t *shell, const char *numstr)
{
	int64_t ret;
	char *endp;
	int skip, base;
	
	base = get_base(shell, numstr, &skip);
	if (!base)
		return 0;

	ret = strtoll(&numstr[skip], &endp, base);

	if (print_num_error(shell, endp, numstr))
		return 0;

	return ret;
}

uint32_t get_number32(shell_t *shell, const char *numstr)
{
	uint64_t ret = get_number64(shell, numstr);
	if (cpu->errno)
		return 0;

	if (ret >= 0x100000000ULL) {
		cpu->errno = ERR_RANGE;
		qprintf(shell->out, "Number exceeds range: %s\n", numstr);
		ret = 0;
	}

	return ret;
}

int32_t get_snumber32(shell_t *shell, const char *numstr)
{
	int64_t ret = get_snumber64(shell, numstr);
	if (cpu->errno)
		return 0;

	if (ret >= 0x80000000LL || ret < -0x80000000LL) {
		cpu->errno = ERR_RANGE;
		qprintf(shell->out, "Number exceeds range: %s\n", numstr);
		ret = 0;
	}

	return ret;
}

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
		qprintf(shell->out, "  aliases: ");
		
		while (*a) {
			qprintf(shell->out, "%s ", *a);
			a++;
		}
		
		qprintf(shell->out, "\n");
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
			qprintf(shell->out, "%s - %s\n", cmd->name, cmd->shorthelp);
			print_aliases(shell, cmd);
		}
		
		return;
	}
	
	cmd = find_command(cmdname);

	if (!cmd) {
		qprintf(shell->out, "help: unknown command '%s'.\n", cmdname);
		return;
	}
	
	qprintf(shell->out, "%s - %s\n", cmd->name, cmd->shorthelp);
	print_aliases(shell, cmd);

	if (cmd->longhelp)
		qprintf(shell->out, "\n%s\n", cmd->longhelp);
}

static command_t help = {
	.name = "help",
	.action = help_fn,
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
	
	num = get_number32(shell, numstr);
	if (cpu->errno)
		return;

	if (num >= last_lpid) {
		qprintf(shell->out, "Partition %u does not exist.\n", num);
		return;
	}

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

#ifdef CONFIG_PAMU
static void paact_dump_fn(shell_t *shell, char *args)
{
	int liodn;
	ppaace_t *entry;

	/*
	 * Currently all PAMUs in the platform share the same PAACT(s), hence,
	 * this command does not support a PAMU#
	 */

	for (liodn = 0; liodn < PAACE_NUMBER_ENTRIES; liodn++) {
		entry = get_ppaace(liodn);
		if (entry && entry->v) {
			qprintf(shell->out, "liodn#: %d wba: 0x%x wse: 0x%x twba: 0x%x\n",
				liodn, entry->wbal, entry->wse, entry->twbal);
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

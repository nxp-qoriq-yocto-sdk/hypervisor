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

static int get_partition_num(shell_t *shell, char *numstr)
{
	int num;

	num = get_number32(shell, numstr);
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

/* checks if the property is a printable string or multiple strings
 * return values : 0 indicates not a string, 1 indicates a string
 */
static int string_check(const void *prop, int len)
{
	const char *str = prop;

	if (str[len-1] != '\0')
		return 0;

	while (len) {
		switch (*str) {
			case 32 ... 126: /*printable character*/
				str++;
				len--;
				break;
			case 0:
				if (len != 1) {
					if (str[1] == '\0')
						return 0;
				}
				str++;
				len--;
				break;
			default:
				return 0;
		}
	}

	return 1;
}

static inline void print_tab(int depth, shell_t *shell)
{
	while (depth--)
		queue_writechar(shell->out, '\t');
}

static void wrap_output(int depth, shell_t *shell)
{
	queue_writechar(shell->out, '\r');
	queue_writechar(shell->out, '\n');
	print_tab(depth + 1, shell);
}

static void print_prop_data(const void *prop, int len, shell_t *shell, int depth)
{
	int i;

	if (string_check(prop, len)) {
		queue_writechar(shell->out, '"');
		for(i = 0; i < len; i += strlen(&((char *)prop)[i]) + 1) {
			if (i != 0)
				qprintf(shell->out, "\", \"");
			qprintf(shell->out, "%s", &((char *)prop)[i]);
		}
		queue_writechar(shell->out, '"');
		return;
	}

	if (!(len & (CELL_SIZE - 1))) {
		len >>= 2;
		queue_writechar(shell->out, '<');
		for (i = 0; i < len; i++) {
			qprintf(shell->out, "0x%x%s", ((uint32_t *)prop)[i], i == (len - 1) ? "" : " ");
				if (!((i + 1) % 4) && (i != len - 1)) {
					 wrap_output(depth, shell);
				}
		}
		queue_writechar(shell->out, '>');
		return;
	}

	qprintf(shell->out, "<binary data>");
}

static int print_device_tree(void *fdt, shell_t *shell)
{
	int depth = 0, offset = 0, nextoffset, paroffset = 0, len;
	const void *prop;
	uint32_t tag;
	const char *name;
	const struct fdt_property *node_prop;

	while (1) {
		tag = fdt_next_tag(fdt, offset, &nextoffset);
		switch (tag) {
		case FDT_BEGIN_NODE:
			name = fdt_get_name(fdt, offset, NULL);
			if (name) {
				print_tab(depth, shell);
				qprintf(shell->out, "%s {\n", name);
			}
			paroffset = offset;
			depth++;
			break;

		case FDT_END_NODE:
			depth--;
			print_tab(depth, shell);
			qprintf(shell->out, "};\n");
			break;

		case FDT_PROP:
			node_prop = fdt_offset_ptr(fdt, offset, sizeof(*node_prop));
			if (!node_prop) {
				qprintf(shell->out, "Corrupted device tree \n");
				return 1;
			}

			name = fdt_string(fdt, node_prop->nameoff);
			prop = fdt_getprop(fdt, paroffset, name, &len);
			if (len < 0) {
				qprintf(shell->out, "error reading property %s, errno =%d\n", name, len);
				return 1;
			}

			if (len == 0) {
				print_tab(depth, shell);
				qprintf(shell->out, "%s;\n", name);
			} else {
				print_tab(depth, shell);
				qprintf(shell->out, "%s = ", name);
				if (!strcmp(name, "fsl,hv-dtb")) {
					qprintf(shell->out, "<binary data>\n");
				} else {
					print_prop_data(prop, len, shell, depth);
					qprintf(shell->out, ";\n");
				}
			}
			break;

		case FDT_END:
			return 0;

		default:
			qprintf(shell->out, "Unknown device tree tag %x\n", tag);
			return 1;
		}
		offset = nextoffset;
	}

	return 0;
}

static void gdt_fn(shell_t *shell, char *args)
{
	char *numstr, *cmdstr;
	unsigned int num;
	guest_t *guest;
	int ret;

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

	if(!strcmp(cmdstr, "print")) {
		ret = print_device_tree(guest->devtree, shell);
		if (ret)
			qprintf(shell->out, "Failed to print device tree\n");
	}
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
	int ret;

	ret = print_device_tree(fdt, shell);
	if (ret)
		qprintf(shell->out, "Failed to print device tree\n");
}

static command_t mdt = {
	.name = "master-device-tree",
	.aliases = (const char *[]){ "mdt", NULL },
	.action = mdt_fn,
	.shorthelp = "Display master device tree",
};

shell_cmd(mdt);

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

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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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
#include <devtree.h>
#include <shell.h>
#include <percpu.h>

extern command_t *shellcmd_begin, *shellcmd_end;

char *stripspace(const char *str)
{
	while (*str && *str == ' ')
		str++;

	return (char *)str;
}

char *nextword(char **str)
{
	char *ret = stripspace(*str);
	*str = strchr(ret, ' ');

	if (*str) {
		**str = 0;
		(*str)++;
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
	char *args = "";
	command_t *cmd;
	char *space;

	buf = stripspace(buf);
	if (strlen(buf) == 0)
		return 0;
	
	space = strchr(buf, ' ');
	if (space) {
		args = space + 1;
		*space = 0;
	}
	
	cmd = find_command(buf);
	if (cmd)
		cmd->action(shell, args);
	else
		qprintf(shell->out, "Unknown command '%s'.\n", buf);

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

static void help_fn(shell_t *shell, char *args)
{
	command_t *cmd;
	const char *cmdname;

	if (strlen(args) == 0) {
		command_t **i, *cmd;

		qprintf(shell->out, "Commands:\n");

		for (i = &shellcmd_begin; i < &shellcmd_end; i++) {
			cmd = *i;
			qprintf(shell->out, "%s - %s\n", cmd->name, cmd->shorthelp);
		}
		
		return;
	}
	
	cmdname = nextword(&args);
	cmd = find_command(cmdname);

	if (!cmd) {
		qprintf(shell->out, "help: unknown command '%s'.\n", cmdname);
		return;
	}
	
	qprintf(shell->out, "%s - %s\n", cmd->name, cmd->shorthelp);
	
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
	
	printf("Partition   Name\n");
	
	for (i = 0; i < last_lpid; i++)
		printf("%-11d %s\n", i, guests[i].name);
}

static command_t lp = {
	.name = "list-partitions",
	.aliases = (const char *[]){ "lp", NULL },
	.action = lp_fn,
	.shorthelp = "List partitions",
};
shell_cmd(lp);

#ifndef _SHELL_H
#define _SHELL_H

#include <libos/readline.h>

typedef struct {
	readline_t *rl;
	queue_t *out;
} shell_t;

typedef struct command {
	const char *name;
	const char **aliases;
	const char *shorthelp;
	const char *longhelp;
	void (*action)(shell_t *shell, char *args);
} command_t;

void shell_init(void);
char *stripspace(const char *str);
char *nextword(char **str);

#define shell_cmd(x) __attribute__((section(".shellcmd"))) command_t *_x_PTR = (&x)

#endif

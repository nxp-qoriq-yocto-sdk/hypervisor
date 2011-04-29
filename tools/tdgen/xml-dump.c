/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "td-data.h"

static void show_usage (const char *program_name)
{
	fprintf (stderr, "Usage: %s [--core|--fpu|--tgt] --arch [e500mc|e5500]\n", program_name);
	exit(EXIT_FAILURE);
}

int main (int argc, char *argv[])
{
	bool core = false, fpu = false, tgt = false;
	arch_t arch;

	if (argc == 4) {

		if (strcmp (argv[1], "--core") == 0)
			core = true;
		else if (strcmp (argv[1], "--fpu") == 0)
			fpu = true;
		else if (strcmp (argv[1], "--tgt") == 0)
			tgt = true;
		else show_usage (argv[0]);

		if (strcmp (argv[2], "--arch") == 0)
			if (strcmp (argv[3], "e500mc") == 0)
				arch = e500mc;
			else if (strcmp (argv[3], "e5500") == 0)
				arch = e5500;
			else show_usage (argv[0]);
		else show_usage (argv[0]);

		if (core)
			printf ("%s", power_core_description[arch]);
		else if (fpu)
			printf ("%s", power_fpu_description[arch]);
		else if (tgt) printf ("%s", description[arch]);

	} else show_usage (argv[0]);

	return 0;
}

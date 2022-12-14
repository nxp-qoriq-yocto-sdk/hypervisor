/** @file
 * Misc utilities
 */
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

#include <libos/queue.h>
#include <percpu.h>
#include <errors.h>
#include <thread.h>

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

/** Find next word or quoted string.
 * @param[in] out output queue where to send errors. Pass NULL to suppress output.
 * @param[in, out] str input: pointer to start of string
 *                     output: points to the end of the found word or quoted string
 * @return pointer to the found word or quoted string, NULL on error
 *
 * The function supports alternating nested quotes.
 * Example:
 *     foo a'bc "bar" def'baz
 * will return the strings:
 *     foo
 *     abc "bar" defbaz
 */
char *nextword(queue_t *out, char **str)
{
	char *ret, *src, *dest;
	int quote = 0;

	ret = stripspace(*str);
	if (!ret)
		return NULL;

	for (src = dest = ret; *src && (quote || *src != ' '); src++) {
		if (quote) {
			if (*src == quote)
				quote = 0;
			else
				*dest++ = *src;
		} else if (*src == '"' || *src == '\'') {
			quote = *src;
		} else {
			*dest++ = *src;
		}
	}

	if (quote) {
		if (out)
			qprintf(out, !is_idle(), "Missing end quote\n");

		*str = NULL;
		return NULL;
	}

	*str = *src ? src + 1 : src;
	*dest = 0;
	
	return ret;
}

int print_num_error(queue_t *out, const char *numstr)
{
	int blocking = !is_idle();

	if (cpu->errno) {
		if (cpu->errno == ERR_RANGE)
			qprintf(out, blocking, "Number exceeds range: %s\n", numstr);
		else if (cpu->errno == ERR_INVALID)
			qprintf(out, blocking, "Unrecognized number format: %s\n", numstr);
		else
			qprintf(out, blocking, "get_number: error %d: %s\n", cpu->errno, numstr);

		return 1;
	}
	
	return 0;
}

static int get_base(const char *numstr, int *skip)
{
	*skip = 0;

	if (numstr[0] == '0') {
		if (numstr[1] == 0 || numstr[1] == ' ')
			return 10;
	
		if (numstr[1] == 'x' || numstr[1] == 'X') {
			*skip = 2;
			return 16;
		}

		if (numstr[1] == 'b' || numstr[1] == 'B') {
			*skip = 2;
			return 2;
		}
		
		if (numstr[1] >= '0' && numstr[1] <= '7') {
			*skip = 1;
			return 8;
		}

		cpu->errno = ERR_INVALID;
		return 0;
	}
	
	return 10;
}

uint64_t get_number64(const char *numstr)
{
	uint64_t ret;
	char *endp;
	int skip, base;
	
	cpu->errno = 0;

	if (numstr[0] == '-') {
		cpu->errno = ERR_RANGE;
		return 0;
	}

	base = get_base(numstr, &skip);
	if (!base)
		return 0;

	ret = strtoull(&numstr[skip], &endp, base);
	if (endp && *endp) {
		cpu->errno = ERR_INVALID;
		return 0;
	}

	return ret;
}

/* Only decimal numbers may be negative */
int64_t get_snumber64(const char *numstr)
{
	int64_t ret;
	char *endp;
	int skip, base;

	cpu->errno = 0;
	
	base = get_base(numstr, &skip);
	if (!base)
		return 0;

	ret = strtoll(&numstr[skip], &endp, base);
	if (endp && *endp) {
		cpu->errno = ERR_INVALID;
		return 0;
	}

	return ret;
}

uint32_t get_number32(const char *numstr)
{
	uint64_t ret = get_number64(numstr);
	if (cpu->errno)
		return 0;

	if (ret >= 0x100000000ULL) {
		cpu->errno = ERR_RANGE;
		ret = 0;
	}

	return ret;
}

int32_t get_snumber32(const char *numstr)
{
	int64_t ret = get_snumber64(numstr);
	if (cpu->errno)
		return 0;

	if (ret >= 0x80000000LL || ret < -0x80000000LL) {
		cpu->errno = ERR_RANGE;
		ret = 0;
	}

	return ret;
}

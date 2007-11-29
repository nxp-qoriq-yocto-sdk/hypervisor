// string and memory functions.
//
// This software is copyright (c) 2007 Scott Wood <scott@buserror.net>.
// 
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors or contributors be held liable for any damages
// arising from the use of this software.
// 
// Permission is hereby granted to everyone, free of charge, to use, copy,
// modify, prepare derivative works of, publish, distribute, perform,
// sublicense, and/or sell copies of the Software, provided that the above
// copyright notice and disclaimer of warranty be included in all copies or
// substantial portions of this software.

#include <stdint.h>
#include <string.h>

void *memcpy(void *dest, const void *src, size_t len)
{
	const char *cs = src;
	char *cd = dest;
	size_t i;

	for (i = 0; i < len; i++)
		cd[i] = cs[i];

	return dest;
}

void *memmove(void *dest, const void *src, size_t len)
{
	const char *cs = src;
	char *cd = dest;
	size_t i;

	if (dest < src)
		return memcpy(dest, src, len);

	for (i = len - 1; i >= 0; i--)
		cd[i] = cs[i];

	return dest;
}

int memcmp(const void *b1, const void *b2, size_t len)
{
	size_t pos;
	const char *c1 = b1;
	const char *c2 = b2;
	
	for (pos = 0; pos < len; pos++) {
		if (c1[pos] != c2[pos])
			return c1[pos] - c2[pos];
			
		pos++;
	}
	
	return 0;
}

void *memset(void *b, int ch, size_t len)
{
	char *c = b;
	
	while (len--)
		*c++ = ch;

	return b;
}

size_t strnlen(const char *s, size_t n)
{
	size_t pos = 0;

	while (pos < n && *s++)
		pos++;

	return pos;
}

size_t strlen(const char *s)
{
	size_t pos = 0;

	while (*s++)
		pos++;

	return pos;
}

char *strcpy(char *dest, const char *src)
{
	char *orig = dest;

	do {
		*dest = *src++;
	} while (*dest++);

	return orig;
}

char *strncpy(char *dest, const char *src, size_t len)
{
	char *orig = dest;

	while (len--) {
		*dest = *src++;
		
		if (!*dest++)
			break;
	}
	
	memset(dest, 0, len);
	return orig;
}

char *strcat(char *dest, const char *src)
{
	char *orig = dest;
	dest += strlen(dest);

	do {
		*dest = *src++;
	} while (*dest++);

	return orig;
}

char *strncat(char *dest, const char *src, size_t len)
{
	char *orig = dest;
	int orig_len = strlen(dest);
	
	len -= orig_len;
	dest += orig_len;

	while (len--) {
		*dest = *src++;
		
		if (!*dest++)
			break;
	}
	
	memset(dest, 0, len);
	return orig;
}

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && *s2 && *s1 == *s2) {
		s1++;
		s2++;
	}

	return *s2 - *s1;
}

int strncmp(const char *s1, const char *s2, int n)
{
	int i = 0;

	while (i < n && s1[i] && s2[i] && s1[i] == s2[i])
		i++;

	if (i == n)
		return 0;

	return *s2 - *s1;
}

char *strchr(const char *s, int c)
{
	while (*s && *s != c)
		s++;

	if (*s == c) 
		return (char *)s;

	return NULL;
}

void *memchr(const void *s, int c, size_t len)
{
	const char *cp = s;

	while (*cp && *cp != c && len-- != 0)
		cp++;

	if (*cp == c) 
		return (void *)cp;

	return NULL;
}

void *memrchr(const void *s, int c, size_t len)
{
	const char *cp = s;
	len--;

	while (cp[len] && cp[len] != c && (ssize_t)len >= 0)
		len--;

	if (cp[len] == c) 
		return (void *)&cp[len];

	return NULL;
}

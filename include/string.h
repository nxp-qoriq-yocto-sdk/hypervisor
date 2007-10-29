#ifndef STRING_H
#define STRING_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t len);
int memcmp(const void *b1, const void *b2, size_t len);
void *memset(void *block, int count, size_t len);
size_t strnlen(const char *s, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t len);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t len);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);
char *strchr(const char *s, int c);

#endif

/*
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
 * Authors: Andy Fleming <afleming@freescale.com>
 *          Timur Tabi <timur@freescale.com>
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

// args = r|w addr len
// the main address is filled with random values if w is passed in

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>

void read_shmem(int fd, void * addr, int len)
{
	char *mem;
	int i;

	mem = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, (off_t) addr);

	if (mem == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	for (i = 0; i < len; i++) {
		printf("%.2x", mem[i]);

		if ((i % 16) == 15) {
			printf("\n");
			continue;
		}

		if ((i % 4) == 3) printf(" ");
	}

	munmap(mem, len);
}

void write_shmem(int fd, void * addr, int len)
{
	char *mem;
	int i;

	mem = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, (off_t) addr);

	if (mem == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	for (i = 0; i < len; i++) {
		char val = (char)(random()%256);

		mem[i] = val;
		printf("%.2x", val);

		if ((i % 16) == 15) {
			printf("\n");
			continue;
		}

		if ((i % 4) == 3) printf(" ");
	}

	munmap(mem, len);
}

int main(int argc, char **argv)
{
	unsigned long addr;
	int read;
	int shmem;
	unsigned long len;

	if (!strcmp(argv[1], "r"))
		read=1;
	else
		read=0;

	if (argc > 2) {
		addr = strtoul(argv[2], NULL, 0);

		if (argc > 3)
			len = strtoul(argv[3], NULL, 0);
		else
			len = 4096;
	}

	shmem = open("/dev/fsl-hv-shmem", O_RDWR);

	if (!shmem)
		return 1;

	if (read) {
		printf("Reading %lu bytes from address %lx\n", len, addr);
		read_shmem(shmem, (void *)addr, len);
	} else {
		printf("Writing %lu bytes to address %lx\n", len, addr);
		write_shmem(shmem, (void *)addr, len);
	}

	close(shmem);

	return 0;
}

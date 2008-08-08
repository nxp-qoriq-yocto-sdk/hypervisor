
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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



#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

void init(unsigned long devtree_ptr);

int irq;
extern void *fdt;

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int vector;
	fh_vmpic_iack(&vector);
	// printf("ext int %d\n",vector);
	fh_vmpic_eoi(irq);

}

int X = 9;
int Y[] = {1, 2, 4, 8, 16, 32, 64, 128};

int const32(void)
{
	int i;
	for (i = 0; i < 32;)
		i++;
	return i;
}

int maximum(int *a, int n)
{
	int m, *p;
	m = a[0];
	p = a;
	while(p < &a[n]) {
		if(*p > m)
			m = *p;
		p++;
	}
	return m;
}

int max_of_Y(void)
{
	int n;
	n = maximum(Y, sizeof(Y)/sizeof(Y[0]));
	return n;
}

int dive(int n)
{
	int k;
	switch (n % 2) {
	case 0: k = const32();
		break;
	default: k = max_of_Y();
		break;
	}
	return k;
}

void gorp(void)
{
	int i;
	int a[64];
	for (i = 0; i < 64; i++) {
		a[i] = i;
	}
	return;
}
void start(unsigned long devtree_ptr)
{
	uint32_t status;
	char *str;
	uint32_t handle;
	uint32_t rxavail;
	uint32_t txavail;
	char buf[16];
	uint32_t x;
	unsigned int cnt;
	int i = 0, j, t;
	int node = -1;
	int len;

	init(devtree_ptr);

	node = fdt_path_offset(fdt, "/hypervisor/handles/byte-channel0");
	if (node < 0) {
		// printf("0 device tree error %d\n",node);
		return;
	}
	const int *prop = fdt_getprop(fdt, node, "reg", &len);
	if (prop) {
		handle = *prop;
	} else {
		// printf("device tree error\n");
		return;
	}

	str = "hello world\r\n";
	status = fh_byte_channel_send(handle, strlen(str), str);

	while (1) {
		X = 0;
		Y[0] = 0;
		Y[1] = 1;
		Y[2] = 2;
		Y[3] = 3;
		Y[4] = 4;
		Y[5] = 5;
		Y[6] = 6;
		Y[7] = 7;
		for (j = 0; j < 4; j++) {
			t = Y[2*j];
			Y[2*j] = Y[2*j+1];
			Y[2*j+1] = t;
		}
		gorp();
		Y[0] = 7;
		Y[1] = 6;
		Y[2] = 5;
		Y[3] = 4;
		Y[4] = 3;
		Y[5] = 2;
		Y[6] = 1;
		Y[7] = 0;
		X = dive(1024);
		Y[0] = 0;
		Y[1] = 1;
		Y[2] = 2;
		Y[3] = 3;
		Y[4] = 4;
		Y[5] = 5;
		Y[6] = 6;
		Y[7] = 7;
		X = dive(127);
		Y[0] = 7;
		Y[1] = 6;
		Y[2] = 5;
		Y[3] = 4;
		Y[4] = 3;
		Y[5] = 2;
		Y[6] = 1;
		Y[7] = 0;
	}
}
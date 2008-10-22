
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
#include <libos/mpic.h>
#include <libos/io.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libfdt.h>

extern void init(unsigned long devtree_ptr);
extern void *fdt;

int *handle_p;

volatile int extint_cnt = 0;
extern int coreint;

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int vector;
	int rc;
	unsigned int mask, active;

	rc = fh_vmpic_iack(&vector);

	if (coreint) {
		if (rc != FH_ERR_INVALID_STATE)
	        	printf("iack had bad rc: FAILED\n");
		vector = mfspr(SPR_EPR);
	} else {
		if (rc != 0)
	        	printf("iack had bad rc: FAILED\n");
	}

	if (vector == *handle_p) {
		extint_cnt++;
		fh_vmpic_set_mask(*handle_p, 1);
		/* Make sure we are masked now */
		fh_vmpic_get_mask(*handle_p, &mask);
		if (!mask)
			printf("Unexpected behavior : Interrupt not masked\n");
	} else {
		printf("Unexpected extint %u\n", vector);
	}

	fh_vmpic_eoi(vector);

	/* Make sure that eoi has made the interrupt inactive */
	fh_vmpic_get_activity(*handle_p, &active);
	if (active == 1)
		printf("Unexpected behavior : Interrupt in-service @eoi\n");
}

void dump_dev_tree(void)
{
#ifdef DEBUG
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("   node = %s\n",s);
	}
	printf("------\n");
#endif
}

void start(unsigned long devtree_ptr)
{
	int ret;
	int node;
	const char *path;
	int len;

	init(devtree_ptr);

	printf("vmpic test ");
	if (coreint)
		printf("- coreint mode\n");
	else
		printf("\n");

	dump_dev_tree();

	/* look up the stdout alias */
	ret = fdt_subnode_offset(fdt, 0, "aliases");
	if (ret < 0)
	        printf("no aliases node: FAILED\n");
	path = fdt_getprop(fdt, ret, "stdout", &ret);
	if (!path)
	        printf("no stdout alias: FAILED\n");

	/* get the interrupt handle for the serial device */
	node = fdt_path_offset(fdt, path);
	handle_p = (int *)fdt_getprop(fdt, node, "interrupts", &len);

	fh_vmpic_set_mask(*handle_p, 0);

	/* VMPIC config */
	fh_vmpic_set_int_config(*handle_p,1,15,0x00000001);

	/* enable TX interrupts at the UART */
	out8((uint8_t *)(CCSRBAR_VA+0x11d501),0x2);

	/* enable interrupts at the CPU */
	enable_extint();

	while (extint_cnt <= 0);

	printf(" > got external interrupt: PASSED\n");

	printf("Test Complete\n");

}



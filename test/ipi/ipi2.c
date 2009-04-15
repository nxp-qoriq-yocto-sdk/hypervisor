
/*
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>
#include <hvtest.h>

#undef DEBUG

static int extint_cnt;
static const uint32_t *handle_p_int;

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int irq;

	if (coreint)
		irq = mfspr(SPR_EPR);
	else
		fh_vmpic_iack(&irq);

	if (irq != *handle_p_int)
		printf("Unknown extirq %d\n", irq);
	else
		extint_cnt++;

	fh_vmpic_eoi(irq);
}

void ext_doorbell_handler(trapframe_t *frameptr)
{
	printf("Doorbell\n");
}

void ext_critical_doorbell_handler(trapframe_t *frameptr)
{
	printf("Critical doorbell\n");
}

static const uint32_t *get_specific_handle(const char *dbell_type,
                                           const char *prop, void *tree,
                                           uint32_t which_handle)
{
	int off = -1, ret;
	int len;

	ret = fdt_check_header(tree);
	if (ret)
		return NULL;
	ret = fdt_node_offset_by_compatible(tree, off, dbell_type);
	if (ret == -FDT_ERR_NOTFOUND)
		return NULL;
	if (ret < 0)
		return NULL;

	for (uint32_t i = 0; i < which_handle; i++) {
		ret = fdt_next_node(tree, ret, &len);
		if (ret == -FDT_ERR_NOTFOUND)
			return NULL;
		if (ret < 0)
			return NULL;
	}

	off = ret;

	return fdt_getprop(tree, off, prop, &len);
}

static const uint32_t *get_handle(const char *dbell_type,
                                  const char *prop, void *tree)
{
	int off = -1, ret;
	int len;

	ret = fdt_check_header(tree);
	if (ret)
		return NULL;
	ret = fdt_node_offset_by_compatible(tree, off, dbell_type);

	if (ret == -FDT_ERR_NOTFOUND)
		return NULL;
	if (ret < 0)
		return NULL;

	off = ret;

	return fdt_getprop(tree, off, prop, &len);
}

static int test_init(void)
{
	const uint32_t *handle_p;

	handle_p_int = get_specific_handle("fsl,hv-doorbell-receive-handle",
	                                   "interrupts", fdt, 32);

	if (!handle_p_int) {
		printf("Couldn't get recv doorbell handle\n");
		return -1;
	}

	/* VMPIC config */
	fh_vmpic_set_int_config(*handle_p_int, 1, 15, 0x00000001);
	fh_vmpic_set_mask(*handle_p_int, 0);
	 /*VMPIC*/ enable_critint();
	enable_extint();

	handle_p = get_handle("fsl,hv-doorbell-send-handle", "reg", fdt);

	if (!handle_p) {
		printf("Couldn't get send doorbell handle\n");
		return -1;
	}

	fh_partition_send_dbell(*handle_p);

	return 0;
}

static void dump_dev_tree(void)
{
#ifdef DEBUG
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("   node = %s\n", s);
	}
	printf("------\n");
#endif
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int rc;

	init(devtree_ptr);

	printf("inter-partition doorbell test #1\n");

	dump_dev_tree();

	printf(" > sending doorbell to self...");

	rc = test_init();
	if (rc)
		printf("error: test init failed\n");

	while (1) {
		if (extint_cnt) {
			printf("got external interrupt: PASSED\n");
			break;
		}
	}

	printf("Test Complete\n");
}

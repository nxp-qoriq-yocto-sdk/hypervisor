
/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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
#include <libos/epapr_hcalls.h>
#include <libos/fsl_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

static const uint32_t *handle_p_int;
volatile uint32_t extint;

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int vector;

	if (coreint)
		vector = mfspr(SPR_EPR);
	else
		ev_int_iack(0, &vector);

	printf("ext int %d\n",vector);
	ev_int_eoi(vector);
	extint++;
}

static const uint32_t *get_handle(const char *compatible_type,
                                  const char *prop, void *tree)
{
	int off = -1, ret;
	int len;

	ret = fdt_check_header(tree);
	if (ret)
		return NULL;
	ret = fdt_node_offset_by_compatible(tree, off, compatible_type);

	if (ret == -FDT_ERR_NOTFOUND)
		return NULL;
	if (ret < 0)
		return NULL;

	off = ret;

	return fdt_getprop(tree, off, prop, &len);
}

void libos_client_entry(unsigned long devtree_ptr)
{

	init(devtree_ptr);

	printf("Error interrupt test\n");

	handle_p_int = get_handle("fsl,p4080-pcie", "interrupts", fdt);

	if (!handle_p_int) {
		printf("Couldn't get pci-e error interrupt\n");
		return;
	}

	/* VMPIC config */
	ev_int_set_config(*handle_p_int, 1, 15, 0);
	ev_int_set_mask(*handle_p_int, 0);
	enable_critint();
	enable_extint();

	printf("Waiting for interrupt...\n");
	while (!extint);

	printf("Got error PCI-E interrupt - PASSED\n");
	printf("Test Complete\n");

}

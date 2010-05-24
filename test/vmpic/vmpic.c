
/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/mpic.h>
#include <libos/io.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libfdt.h>
#include <hvtest.h>

static const uint32_t *handle_p;

static volatile int extint_cnt = 0;

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int vector;
	int rc;
	unsigned int mask, active;

	rc = ev_int_iack(&vector);

	if (coreint) {
		if (rc != EV_INVALID_STATE)
	        	printf("iack had bad rc: FAILED\n");
		vector = mfspr(SPR_EPR);
	} else {
		if (rc != 0)
	        	printf("iack had bad rc: FAILED\n");
	}

	if (vector == *handle_p) {
		extint_cnt++;
		ev_int_set_mask(*handle_p, 1);
		/* Make sure we are masked now */
		ev_int_get_mask(*handle_p, &mask);
		if (!mask)
			printf("Unexpected behavior : Interrupt not masked\n");
	} else {
		printf("Unexpected extint %u\n", vector);
	}

	ev_int_eoi(vector);

	/* Make sure that eoi has made the interrupt inactive */
	ev_int_get_activity(*handle_p, &active);
	if (active == 1)
		printf("Unexpected behavior : Interrupt in-service @eoi\n");
}

extern uint8_t *uart_virt;

void libos_client_entry(unsigned long devtree_ptr)
{
	int ret;
	int node;
	const char *path;
	int len;
	uint32_t config;
	unsigned int priority;
	uint32_t destination;

	init(devtree_ptr);

	printf("vmpic test ");
	if (coreint)
		printf("- coreint mode\n");
	else
		printf("- legacy mode\n");

	node = fdt_path_offset(fdt, "/devices/uart2");
	if (node < 0) {
		printf("no /devices/uart2: BROKEN\n");
		return;
	}

	if (!test_init_uart(node)) {
		printf("Can't init /devices/uart2: BROKEN\n");
		return;
	}

	/* get the interrupt handle for the serial device */
	handle_p = fdt_getprop(fdt, node, "interrupts", &len);

	ev_int_set_mask(*handle_p, 0);

	ev_int_get_config(*handle_p, &config, &priority, &destination);
	if (config != 0x3 && priority != 0 && destination != 0x1) {
		printf("ERROR: unexpected default interrupt config\n");
	}

	/* VMPIC config */
	ev_int_set_config(*handle_p,0x3,15,0x00000001);

	/* enable TX interrupts at the UART */
	out8(&uart_virt[1], 0x2);

	/* enable interrupts at the CPU */
	enable_extint();

	while (extint_cnt <= 0);

	printf(" > got external interrupt: PASSED\n");

	printf("Test Complete\n");
}

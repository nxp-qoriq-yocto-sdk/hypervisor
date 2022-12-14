/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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

#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/bitops.h>
#include <hvtest.h>
#include <libfdt.h>

/* Handler for Libos client function of secondary cores */
void secondary_core_handler(void);

/* Test configuration:
 * Three guests are configured, two having no-auto-start, one not being started
 * for this test, and the other is slave to a manager partition automatically
 * started. This starts its slave which ends itself while the manager waits for
 * it to become stopped again.
 */

void libos_client_entry(unsigned long devtree_ptr)
{
	register_t pir;
	int ret, node, len;
	uint32_t slave_part_handle = 0;
	const void *p_handle = NULL;

	pir = mfspr(SPR_PIR);

	init(devtree_ptr);
	puts("Testing the system reset on partitions stop");

	/* Check the path towards slave partition, if any */
	node = fdt_path_offset(fdt, "/hypervisor/handles");
	if (node < 0)
		printf("[%ld] 'handles' node not found\n", pir);
	else {
		node = fdt_node_offset_by_compatible(fdt, node,
						     "fsl,hv-partition-handle");
		if (node > 0) {
			p_handle = fdt_getprop(fdt, node, "hv-handle", &len);
			if (!p_handle || len != 4) {
				printf("[%ld] Error getting slave "
				       "partition handle: %d\n", pir, len);
				return;
			}
			slave_part_handle = *(uint32_t*)p_handle;
			printf("[%ld] Found the slave partition handle: %d\n",
			       pir, slave_part_handle);
		}
	}

	if (p_handle) {
		unsigned int status;
		unsigned long wait_iterations = 5000000; /* Experimental value */

		while(--wait_iterations) {
			int ret;

			ret = fh_partition_get_status(slave_part_handle,
						      &status);
			if (ret) {
				printf("Error getting partition status: %d\n",
				       ret);
				continue;
			}

			if (status == FH_PARTITION_STOPPED) {
				ret = fh_partition_start(slave_part_handle,
				                         0, 0);
				if (ret) {
					printf("Error starting partition: %d\n",
					       ret);
					continue;
				} else {
					puts("Started slave partition");
					break;
				}

			}
		}

		if (wait_iterations == 0) {
			puts("Test Failed");
			return;
		}
	}

	/* Delay ending the test to allow visual confirmation of HV */
	puts("Delaying ...");
	delay_timebase(100000000);
	puts("PASSED\nTest Complete");

	fh_partition_stop(-1);
}

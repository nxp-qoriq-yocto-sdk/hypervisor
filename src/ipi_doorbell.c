/** @file
 *
 * Inter-partition doorbell interrupts
 */

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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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
#include <libos/bitops.h>

#include <ipi_doorbell.h>
#include <errors.h>
#include <devtree.h>

#include <string.h>
#include <vpic.h>
#include <vmpic.h>

void create_doorbells(void)
{
	int off = -1, ret;
	ipi_doorbell_t *dbell;

	while (1) {
		ret = fdt_node_offset_by_compatible(fdt, off,
		                                    "fsl,hv-doorbell");
		if (ret < 0)
			break;

		dbell = alloc_type(ipi_doorbell_t);
		if (!dbell) {
			printf("doorbell_global_init: failed to create doorbell.\n");
			return;
		}

		off = ret;
		ret = fdt_setprop(fdt, off, "fsl,hv-internal-doorbell-ptr",
		                  &dbell, sizeof(dbell));
		if (ret < 0)
			break;
	}

	if (ret == -FDT_ERR_NOTFOUND)
		return;

	printf("create_doorbells: libfdt error %d (%s).\n",
	       ret, fdt_strerror(ret));
}

static int doorbell_attach_guest(ipi_doorbell_t *dbell, guest_t *guest)
{
	struct ipi_doorbell_handle *db_handle;

	db_handle = alloc_type(struct ipi_doorbell_handle);
	db_handle->user.db = db_handle;
	db_handle->dbell = dbell;

	int ghandle = alloc_guest_handle(guest, &db_handle->user);
	if (ghandle < 0)
		return ERR_NOMEM;

	return ghandle;
}

void send_dbell_partition_init(guest_t *guest)
{
	int off = -1, ret;
	const char *endpoint;
	ipi_doorbell_t *dbell;

	while (1) {
		ret = fdt_node_offset_by_compatible(guest->devtree, off,
		                                    "fsl,doorbell-send-handle");
		if (ret == -FDT_ERR_NOTFOUND)
			return;
		if (ret < 0)
			break;

		off = ret;

		endpoint = fdt_getprop(guest->devtree, off,
		                       "fsl,endpoint", &ret);
		if (!endpoint)
			break;

		ret = lookup_alias(fdt, endpoint);
		if (ret < 0)
			break;

		/* Get the pointer corresponding to hv-internal-doorbell-ptr */
		dbell = ptr_from_node(fdt, ret, "doorbell");
		if (!dbell) {
			printf("send_doorbell_partition_init: endpoint not a doorbell\n");
			continue;
		}

		int32_t ghandle = doorbell_attach_guest(dbell, guest);
		if (ghandle < 0) {
			printf("send_doorbell_partition_init: cannot attach\n");
			return;
		}

		printf("send_dbell_partition_init: guest handle = %d\n",
		       ghandle);

		ret = fdt_setprop(guest->devtree, off, "reg", &ghandle, 4);
		if (ret < 0)
			break;
	}
}

void recv_dbell_partition_init(guest_t *guest)
{
	int off = -1, ret;
	const char *endpoint;
	guest_recv_dbell_list_t *dbell_node;
	register_t saved;
	uint32_t irq[2];
	vpic_interrupt_t *virq;
	ipi_doorbell_t *dbell;

	while (1) {
		ret = fdt_node_offset_by_compatible(guest->devtree, off,
		                                    "fsl,hv-doorbell-receive-handle");
		if (ret == -FDT_ERR_NOTFOUND)
			return;
		if (ret < 0)
			break;

		off = ret;

		endpoint = fdt_getprop(guest->devtree, off,
		                       "fsl,endpoint", &ret);
		if (!endpoint)
			break;

		ret = lookup_alias(fdt, endpoint);
		if (ret < 0)
			break;

		/* Get the pointer corresponding to hv-internal-doorbell-ptr */
		dbell = ptr_from_node(fdt, ret, "doorbell");
		if (!dbell) {
			printf("recv_doorbell_partition_init: no pointer\n");
			continue;
		}

		dbell_node = alloc_type(guest_recv_dbell_list_t);
		if (!dbell_node)
			return;

		virq = vpic_alloc_irq(guest);
		irq[0] = vmpic_alloc_handle(guest, &virq->irq);
		irq[1] = 0;

		dbell_node->guest_vint = virq;

		saved = spin_lock_critsave(&dbell->dbell_lock);
		dbell_node->next = dbell->recv_head;
		dbell->recv_head = dbell_node;
		spin_unlock_critsave(&dbell->dbell_lock, saved);

		ret = fdt_setprop(guest->devtree, off, "interrupts",
		                  irq, sizeof(irq));
		if (ret < 0)
			break;
	}
}

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
#include <libos/bitops.h>

#include <ipi_doorbell.h>
#include <errors.h>
#include <devtree.h>

#include <string.h>
#include <vpic.h>
#include <vmpic.h>

/**
 * send_doorbells - send a doorbell interrupt to all receivers for a doorbell
 *
 * returns the number of doorbell interrupts sent, or a negative number on error
 */
int send_doorbells(struct ipi_doorbell *dbell)
{
	register_t saved;
	int count = 0;

	if (!dbell)
		return -ERR_INVALID;

	saved = spin_lock_critsave(&dbell->dbell_lock);
	if (dbell->recv_head) {
		guest_recv_dbell_list_t *receiver = dbell->recv_head;
		while (receiver) {
			vpic_assert_vint(receiver->guest_vint);
			receiver = receiver->next;
			count++;
		}
	}
	spin_unlock_critsave(&dbell->dbell_lock, saved);

	return count;
}

static int create_doorbell(dt_node_t *node, void *arg)
{
	ipi_doorbell_t *dbell = alloc_type(ipi_doorbell_t);
	if (!dbell)
		return ERR_NOMEM;

	int ret = dt_set_prop(node, "fsl,hv-internal-doorbell-ptr",
	                      &dbell, sizeof(dbell));
	if (ret < 0)
		free(dbell);

	return ret;
}

void create_doorbells(void)
{
	int ret = dt_for_each_compatible(hw_devtree, "fsl,hv-doorbell",
	                                 create_doorbell, NULL);
	if (ret < 0)
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: error %d.\n", __func__, ret);
}

int doorbell_attach_guest(ipi_doorbell_t *dbell, guest_t *guest)
{
	struct ipi_doorbell_handle *db_handle;

	db_handle = alloc_type(struct ipi_doorbell_handle);
	db_handle->user.db = db_handle;
	db_handle->dbell = dbell;

	int ghandle = alloc_guest_handle(guest, &db_handle->user);
	if (ghandle < 0) {
		free(db_handle);
		return ERR_NOMEM;
	}

	return ghandle;
}

/**
 * attach_receive_doorbell - attach a doorbell to a receive handle
 * @guest: the guest to update
 * @dbell: the doorbell to attach
 * @offset: offset in the guest device tree of the receive handle node
 *
 * Attach a doorbell to an existing doorbell receive handle node.  This
 * function also allocates a virq and creates an "interrupts" property in the
 * node with the virq values.
 */
int attach_receive_doorbell(guest_t *guest, struct ipi_doorbell *dbell,
                            dt_node_t *node)
{
	uint32_t irq[2];
	int ret;

	// The recv_list is a list of VIRQs that are "sent" when the doorbell
	// is rung.
	guest_recv_dbell_list_t *recv_list = alloc_type(guest_recv_dbell_list_t);
	if (!recv_list) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
			"%s: failed to create receive doorbell list object\n", __func__);
		return ERR_NOMEM;
	}

	vpic_interrupt_t *virq = vpic_alloc_irq(guest, 0);
	if (!virq) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: out of virqs.\n", __func__);
		ret = ERR_BUSY;
		goto error;
	}

	ret = vpic_alloc_handle(virq, irq);
	if (ret < 0) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: can't alloc vmpic irqs\n", __func__);
		goto error;
	}

	// Write the 'interrupts' property to the doorbell receive handle node
	ret = dt_set_prop(node, "interrupts", irq, sizeof(irq));
	if (ret < 0) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: Couldn't set 'interrupts' property: %i\n",
		         __func__, ret);
		goto error;
	}

	// Add this receive handle to the list of receive handles for the doorbell
	register_t saved = spin_lock_critsave(&dbell->dbell_lock);
	recv_list->next = dbell->recv_head;
	dbell->recv_head = recv_list;
	spin_unlock_critsave(&dbell->dbell_lock, saved);

	recv_list->guest_vint = virq;

	return 0;

error:
	// FIXME: destroy virq and free handles
	free(recv_list);

	return ret;
}

static ipi_doorbell_t *dbell_from_handle_node(dt_node_t *node)
{
	ipi_doorbell_t *dbell;
	dt_prop_t *endpoint;
	dt_node_t *epnode;

	endpoint = dt_get_prop(node, "fsl,endpoint", 0);
	if (!endpoint) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: %s has no endpoint\n", __func__, node->name);
		return NULL;
	}

	epnode = dt_lookup_alias(hw_devtree, endpoint->data);
	if (!epnode) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: endpoint %s of %s not found\n", __func__,
		         node->name, (const char *)endpoint->data);
		return NULL;
	}

	/* Get the pointer corresponding to hv-internal-doorbell-ptr */
	dbell = ptr_from_node(epnode, "doorbell");
	if (!dbell) {
		printf("%s: endpoint %s of %s not a doorbell\n", __func__,
		       node->name, (const char *)endpoint->data);
		return NULL;
	}

	return dbell;
}

int send_dbell_init_one(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	ipi_doorbell_t *dbell;
	int32_t ghandle;

	dbell = dbell_from_handle_node(node);
	if (!dbell)
		return 0;

	ghandle = doorbell_attach_guest(dbell, guest);
	if (ghandle < 0)
		return ghandle;

	return dt_set_prop(node, "reg", &ghandle, 4);
}

int recv_dbell_init_one(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	ipi_doorbell_t *dbell;
	int32_t ghandle;

	dbell = dbell_from_handle_node(node);
	if (!dbell)
		return 0;

	ghandle = attach_receive_doorbell(guest, dbell, node);
	if (ghandle < 0)
		return ghandle;

	return dt_set_prop(node, "reg", &ghandle, 4);
}

void send_dbell_partition_init(guest_t *guest)
{
	int ret = dt_for_each_compatible(guest->devtree,
	                                 "fsl,hv-doorbell-send-handle",
	                                 send_dbell_init_one, guest);
	if (ret < 0)
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: error %d.\n", __func__, ret);
}

void recv_dbell_partition_init(guest_t *guest)
{
	int ret = dt_for_each_compatible(guest->devtree,
	                                 "fsl,hv-doorbell-receive-handle",
	                                 recv_dbell_init_one, guest);
	if (ret < 0)
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: error %d.\n", __func__, ret);
}

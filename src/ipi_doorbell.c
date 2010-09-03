/** @file
 *
 * Inter-partition doorbell interrupts
 */

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

#include <string.h>

#include <libos/libos.h>
#include <libos/bitops.h>
#include <libos/alloc.h>
#include <libos/mpic.h>

#include <ipi_doorbell.h>
#include <errors.h>
#include <devtree.h>

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

	if (dbell->fast_dbell) {
		mpic_set_ipi_dispatch_register(dbell->fast_dbell->irq);
		return 1;
	}

	saved = spin_lock_intsave(&dbell->dbell_lock);
	if (dbell->normal_dbell->recv_head) {
		guest_recv_dbell_list_t *receiver =
					dbell->normal_dbell->recv_head;
		while (receiver) {
			vpic_assert_vint(receiver->guest_vint);
			receiver = receiver->next;
			count++;
		}
	}
	spin_unlock_intsave(&dbell->dbell_lock, saved);

	return count;
}

ipi_doorbell_t *alloc_doorbell(uint32_t type)
{
	ipi_doorbell_t *dbell = alloc_type(ipi_doorbell_t);
	if (!dbell)
		return NULL;

	if (type == IPI_DOORBELL_TYPE_NORMAL) {
		dbell->normal_dbell = alloc_type(ipi_normal_doorbell_t);
		if (!dbell->normal_dbell) {
			printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
				"%s: Failed to allocate memory\n", __func__);
			free(dbell);

			return NULL;
		}
	} else if (type == IPI_DOORBELL_TYPE_FAST) {
		dbell->fast_dbell = alloc_type(ipi_fast_doorbell_t);
		if (!dbell->fast_dbell) {
			printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
				"%s: Failed to allocate memory\n", __func__);
			free(dbell);

			return NULL;
		}
	} else {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
			"%s: Invalid doorbell type\n", __func__);
		free(dbell);

		return NULL;
	}

	return dbell;
}

void destroy_doorbell(ipi_doorbell_t *dbell)
{
	if (dbell) {
		free(dbell->normal_dbell);
		free(dbell->fast_dbell);
		free(dbell);
	}
}

static uint32_t fdbell_lock;

static int create_doorbell(dt_node_t *node, void *arg)
{
	ipi_doorbell_t *dbell = alloc_type(ipi_doorbell_t);
	if (!dbell)
		return ERR_NOMEM;

	if (dt_node_is_compatible(node, "fast-doorbell")) {
		static uint32_t ipi_fast_dbell = 0;
		if (ipi_fast_dbell >=  4) {
			printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
				"%s: cannot create more than "
				"4 fast doorbells\n", __func__);
			free(dbell);

			return 0;
		}

		dbell->fast_dbell = alloc_type(ipi_fast_doorbell_t);
		if (!dbell->fast_dbell) {
			free(dbell);
			return ERR_NOMEM;
		}

		register_t saved = spin_lock_intsave(&fdbell_lock);
		dbell->fast_dbell->irq = mpic_get_ipi_irq(ipi_fast_dbell++);
		dbell->fast_dbell->global_handle = alloc_global_handle();
		if (dbell->fast_dbell->global_handle < 0) {
			free(dbell->fast_dbell);
			free(dbell);
			ipi_fast_dbell--;
			spin_unlock_intsave(&fdbell_lock, saved);

			return ERR_INVALID;
		}

		spin_unlock_intsave(&fdbell_lock, saved);

		mpic_irq_set_vector(dbell->fast_dbell->irq,
					dbell->fast_dbell->global_handle);
	} else {
		dbell->normal_dbell = alloc_type(ipi_normal_doorbell_t);
		if (!dbell->normal_dbell) {
			free(dbell);
			return ERR_NOMEM;
		}
	}

	node->dbell = dbell;

	return 0;
}

void create_doorbells(void)
{
	int ret = dt_for_each_compatible(config_tree, "doorbell",
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

static int attach_fast_doorbell(guest_t *guest, struct ipi_doorbell *dbell,
				dt_node_t *node)
{
	uint32_t handle[2];
	int ret;

	vmpic_interrupt_t *vmirq = alloc_type(vmpic_interrupt_t);
	if (!vmirq) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
			"%s: out of memory\n", __func__);

		return ERR_NOMEM;
	}

	vmirq->guest = guest;
	vmirq->irq = dbell->fast_dbell->irq;

	vmirq->user.intr = vmirq;
	vmirq->user.ops = NULL;

	vmpic_set_claimed(vmirq, 1);

	vmirq->handle = dbell->fast_dbell->global_handle;
	ret = set_guest_global_handle(guest, &vmirq->user, vmirq->handle);
	if (ret < 0) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
			"%s: error in setting global handle\n", __func__);
		free(vmirq);

		return ret;
	}

	handle[0] = vmirq->handle;
	handle[1] = 0;

	ret = dt_set_prop(node, "interrupts", handle, sizeof(handle));
	if (ret < 0) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
			"%s: Couldn't set 'interrupts' property: %i\n",
			__func__, ret);
		free(vmirq);

		return ret;
	}

	return 0;
}

/* This function also allocates a virq and creates an "interrupts" property
 * in the node with the virq values.
 */
static int attach_normal_doorbell(guest_t *guest, struct ipi_doorbell *dbell,
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

	ret = vpic_alloc_handle(virq, irq, 0);
	if (ret < 0)
		goto error;

	// Write the 'interrupts' property to the doorbell receive handle node
	ret = dt_set_prop(node, "interrupts", irq, sizeof(irq));
	if (ret < 0) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: Couldn't set 'interrupts' property: %i\n",
		         __func__, ret);
		goto error;
	}

	// Add this receive handle to the list of receive handles for the doorbell
	register_t saved = spin_lock_intsave(&dbell->dbell_lock);
	recv_list->next = dbell->normal_dbell->recv_head;
	dbell->normal_dbell->recv_head = recv_list;
	spin_unlock_intsave(&dbell->dbell_lock, saved);

	recv_list->guest_vint = virq;

	return 0;

error:
	// FIXME: destroy virq and free handles
	free(recv_list);

	return ret;
}

/**
 * attach_receive_doorbell - attach a doorbell to a receive handle
 * @guest: the guest to update
 * @dbell: the doorbell to attach
 * @node: the receive handle node in the guest device tree
 *
 * Attach a doorbell to an existing doorbell receive handle node.
 */
int attach_receive_doorbell(guest_t *guest, struct ipi_doorbell *dbell,
			    dt_node_t *node)
{
	if (dbell->fast_dbell)
		return attach_fast_doorbell(guest, dbell, node);
	else
		return attach_normal_doorbell(guest, dbell, node);
}

static ipi_doorbell_t *dbell_from_handle_node(dt_node_t *node)
{
	ipi_doorbell_t *dbell;
	dt_prop_t *global;
	dt_node_t *gdnode;

	global = dt_get_prop(node, "global-doorbell", 0);
	if (!global || (global->len != 4)) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: %s has no global-doorbell\n", __func__, node->name);
		return NULL;
	}

	gdnode = dt_lookup_phandle(config_tree, *(const uint32_t *)global->data);
	if (!gdnode) {
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: global-doorbell from %s not found\n", __func__,
		         node->name);
		return NULL;
	}

	dbell = gdnode->dbell;
	if (!dbell) {
		printf("%s: node %s (global-doorbell from %s) is not a doorbell\n",
		       __func__, gdnode->name, node->name);
		return NULL;
	}

	return dbell;
}

static int send_dbell_init_one(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	ipi_doorbell_t *dbell;
	dt_node_t *hnode;	// handle node
	int32_t ghandle;
	int ret;

	dbell = dbell_from_handle_node(node);
	if (!dbell)
		return 0;

	// Get a pointer to the guest's 'handles' node
	hnode = get_handles_node(guest);
	if (!hnode)
		return ERR_NOMEM;

	// Find or the pointer to the doorbell subnode, or create it
	hnode = dt_get_subnode(hnode, node->name, 1);
	if (!hnode)
		return ERR_NOMEM;

	dt_record_guest_phandle(hnode, node);

	// Insert the 'compatible' property.
	ret = dt_set_prop_string(hnode, "compatible", "epapr,hv-send-doorbell");
	if (ret)
		// Out of memory
		return ret;

	ghandle = doorbell_attach_guest(dbell, guest);
	if (ghandle < 0)
		return ghandle;

	ret = dt_set_prop(hnode, "reg", &ghandle, 4);
	if (ret)
		// Out of memory
		return ret;

	ret = dt_process_node_update(guest, hnode, node);
	if (ret < 0) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			 "%s: error %d merging node-update on %s\n",
			 __func__, ret, node->name);
		return ret;
	}

	create_aliases(node, hnode, guest->devtree);

	return 0;
}

static int recv_dbell_init_one(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	ipi_doorbell_t *dbell;
	dt_node_t *hnode;	// handle node
	int ret;

	dbell = dbell_from_handle_node(node);
	if (!dbell)
		return 0;

	// Get a pointer to the guest's 'handles' node
	hnode = get_handles_node(guest);
	if (!hnode)
		return ERR_NOMEM;

	// Find or the pointer to the doorbell subnode, or create it
	hnode = dt_get_subnode(hnode, node->name, 1);
	if (!hnode)
		return ERR_NOMEM;

	dt_record_guest_phandle(hnode, node);

	// Insert the 'compatible' property.
	ret = dt_set_prop_string(hnode, "compatible", "epapr,hv-receive-doorbell");
	if (ret)
		// Out of memory
		return ret;

	ret = attach_receive_doorbell(guest, dbell, hnode);
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: cannot attach doorbell to hnode %s\n",
			 __func__, guest->name, hnode->name);
		return ret;
	}

	ret = dt_process_node_update(guest, hnode, node);
	if (ret < 0) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			 "%s: error %d merging node-update on %s\n",
			 __func__, ret, node->name);
		return ret;
	}

	create_aliases(node, hnode, guest->devtree);

	return ret;
}

void send_dbell_partition_init(guest_t *guest)
{
	int ret = dt_for_each_compatible(guest->partition, "send-doorbell",
	                                 send_dbell_init_one, guest);
	if (ret < 0)
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: error %d.\n", __func__, ret);
}

void recv_dbell_partition_init(guest_t *guest)
{
	int ret = dt_for_each_compatible(guest->partition, "receive-doorbell",
	                                 recv_dbell_init_one, guest);
	if (ret < 0)
		printlog(LOGTYPE_DOORBELL, LOGLEVEL_ERROR,
		         "%s: error %d.\n", __func__, ret);
}

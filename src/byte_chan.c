/** @file
 * Byte channels.
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

#include <libos/queue.h>
#include <libos/libos.h>
#include <libos/bitops.h>
#include <libos/ns16550.h>

#include <byte_chan.h>
#include <bcmux.h>
#include <errors.h>
#include <devtree.h>

#include <stdint.h>
#include <string.h>
#include <vpic.h>
#include <vmpic.h>

/* Must be a power of two. */
#define QUEUE_SIZE 4096
/* PS: Change QUEUE_SIZE back to 256 once we get to real hardware where the
 *     serial port is running at a reasonable speed with respect to the
 *     cores - 4096 bytes shouldn't be needed anymore. We increased this to
 *     4096 bytes now as we have 256 registers that in all ammount to 1156
 *     bytes. Given that in the Remote Serial Protocol, two bytes are
 *     transmitted per byte (upper and lower nibble); we need to be able to
 *     receive at least 2312 bytes using byte_channel_receive() for the G
 *     packet's (i.e. write registers) payload.
 */

/* Allocate a byte channel. */
byte_chan_t *byte_chan_alloc(void)
{
	byte_chan_t *ret = alloc_type(byte_chan_t);
	if (!ret)
		return NULL;

	// FIXME: free() on failure
	if (queue_init(&ret->q[0], QUEUE_SIZE))
		goto err_bc;
	if (queue_init(&ret->q[1], QUEUE_SIZE))
		goto err_q0;

	ret->handles[0].tx = &ret->q[0];
	ret->handles[0].rx = &ret->q[1];
	ret->handles[1].tx = &ret->q[1];
	ret->handles[1].rx = &ret->q[0];

	return ret;

err_q0:
	queue_destroy(&ret->q[0]);
err_bc:
	free(ret);
	return NULL;
}

static uint32_t bchan_lock;

byte_chan_handle_t *byte_chan_claim(byte_chan_t *bc)
{
	byte_chan_handle_t *handle;
	int i;

	register_t saved = spin_lock_critsave(&bchan_lock);
	
	for (i = 0; i < 2; i++) {
		handle = &bc->handles[i];

		if (!handle->attached) {
			handle->attached = 1;
			break;
		}
	}

	spin_unlock_critsave(&bchan_lock, saved);

	return i == 2 ? NULL : handle;
}

int byte_chan_attach_chardev(byte_chan_t *bc, chardev_t *cd)
{
	int ret;
	byte_chan_handle_t *handle = byte_chan_claim(bc);
	if (!handle)
		return ERR_BUSY;

	if (!cd->ops->set_tx_queue || !cd->ops->set_rx_queue)
		return ERR_INVALID;

	ret = cd->ops->set_tx_queue(cd, handle->rx);
	if (ret < 0)
		goto err;

	ret = cd->ops->set_rx_queue(cd, handle->tx);
	if (ret < 0) {
		cd->ops->set_tx_queue(cd, NULL);
		goto err;
	}

	return 0;

err:
	handle->attached = 0;
	return ret;
}

int create_byte_channel(dt_node_t *node, void *arg)
{
	byte_chan_t *bc = byte_chan_alloc();
	if (!bc)
		return ERR_NOMEM;

	return dt_set_prop(node, "fsl,hv-internal-bc-ptr", &bc, sizeof(bc));
}

void create_byte_channels(void)
{
	int ret = dt_for_each_compatible(hw_devtree, "fsl,hv-byte-channel",
	                                 create_byte_channel, NULL);
	if (ret < 0)
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: error %d\n", __func__, ret);
}

static int connect_byte_channel(byte_chan_t *bc, dt_node_t *endpoint,
                                dt_node_t *bcnode, int index)
{
	int ret;
	void *ptr;
	
	ptr = ptr_from_node(endpoint, "chardev");
	if (ptr) {
		ret = byte_chan_attach_chardev(bc, ptr);
		if (ret == ERR_BUSY) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: endpoint %s in %s is full\n",
			         __func__, endpoint->name, bcnode->name);
			return 0;
		}

		return ret;
	}

#ifdef CONFIG_BCMUX
	ptr = ptr_from_node(endpoint, "mux");
	if (ptr) {
		dt_prop_t *channel = dt_get_prop(bcnode, "fsl,mux-channel", 0);
		const uint32_t *channeldata = channel->data;

		if (!channel) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: no mux channel in %s\n", __func__, bcnode->name);
			return 0;
		}

		if (channel->len % 4 || (index + 1) * 4 > channel->len) {
			printf("%s: bad fsl,mux-channel in %s\n", __func__, bcnode->name);
			return 0;
		}
	
		ret = mux_complex_add(ptr, bc, channeldata[index]);
		if (ret == ERR_BUSY) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: %s: mux channel already attached\n",
			         __func__, bcnode->name);
			return 0;
		}

		if (ret == ERR_RANGE) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: %s: mux channel out of range\n",
			         __func__, bcnode->name);
			return 0;
		}

		return ret;
	}
#endif

	printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
	         "%s: unrecognized endpoint %s in %s\n",
	         __func__, endpoint->name, bcnode->name);
	return 0;
}

int connect_byte_channel_node(dt_node_t *node, void *arg)
{
	byte_chan_t *bc = ptr_from_node(node, "bc");
	const uint32_t *endpoint;
	dt_node_t *epnode;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "fsl,endpoint", 0);
	if (!prop)
		return 0;

	if (prop->len != 4 && prop->len != 8) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: %s: Invalid fsl,endpoint length.\n",
		         __func__, node->name);
		return 0;
	}

	endpoint = prop->data;

	epnode = dt_lookup_phandle(hw_devtree, endpoint[0]);
	if (!epnode) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: %s: endpoint not found.\n", __func__, node->name);
		return 0;
	}
			
	connect_byte_channel(bc, epnode, node, 0);

	if (prop->len == 8) {
		epnode = dt_lookup_phandle(hw_devtree, endpoint[1]);
		if (!epnode) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: %s: second endpoint not found.\n",
			         __func__, node->name);
			return 0;
		}
			
		connect_byte_channel(bc, epnode, node, 1);
	}

	return 0;
}

void connect_global_byte_channels(void)
{
	int ret = dt_for_each_compatible(hw_devtree, "fsl,hv-byte-channel",
	                                 connect_byte_channel_node, NULL);
	if (ret < 0)
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: error %d\n", __func__, ret);
}

int byte_chan_attach_guest(byte_chan_t *bc, guest_t *guest,
                           vpic_interrupt_t *rxirq,
                           vpic_interrupt_t *txirq)
{
	byte_chan_handle_t *handle = byte_chan_claim(bc);
	if (!handle)
		return ERR_BUSY;

	handle->user.bc = handle;
	
	int ghandle = alloc_guest_handle(guest, &handle->user);
	if (ghandle < 0) {
		handle->attached = 0;
		return ERR_NOMEM;
	}

	if (rxirq) {
		rxirq->guest = guest;
		handle->rx->consumer = rxirq;
		handle->rx->data_avail = vpic_assert_vint_rxq;
	}

	if (txirq) {
		txirq->guest = guest;
		handle->tx->producer = txirq;
		handle->tx->space_avail = vpic_assert_vint_txq;
	}

	return ghandle;
}

static int byte_chan_partition_init_one(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	dt_prop_t *endpoint;
	dt_node_t *epnode;
	byte_chan_t *bc;
	uint32_t irq[4];
	vpic_interrupt_t *virq[2];
	int ret;

	endpoint = dt_get_prop(node, "fsl,endpoint", 0);
	if (!endpoint) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: %s has no endpoint property\n",
		         __func__, node->name);
		return 0;
	}

	epnode = dt_lookup_alias(hw_devtree, endpoint->data);
	if (!epnode) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: endpoint %s in %s not found.\n", __func__,
		         (const char *)endpoint->data, node->name);
		return 0;
	}
	
	bc = ptr_from_node(epnode, "bc");
	if (!bc) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: endpoint %s in %s not a byte channel.\n",
		         __func__, (const char *)endpoint->data, node->name);
		return 0;
	}

	virq[0] = vpic_alloc_irq(guest, 0);
	virq[1] = vpic_alloc_irq(guest, 0);

	if (!virq[0] || !virq[1]) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: can't alloc vpic irqs\n", __func__);
		return ERR_NOMEM;
	}
	
	if (vpic_alloc_handle(virq[0], &irq[0]) < 0||
	    vpic_alloc_handle(virq[1], &irq[2]) < 0) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: can't alloc vmpic irqs\n", __func__);
		return ERR_NOMEM;
	}

	int32_t ghandle = byte_chan_attach_guest(bc, guest, virq[0], virq[1]);
	if (ghandle == ERR_BUSY) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: endpoint %s in %s is full\n",
		         __func__, epnode->name, node->name);
		return 0;
	}
	if (ghandle < 0)
		return ghandle;

	ret = dt_set_prop(node, "reg", &ghandle, 4);
	if (ret < 0)
		return ret;

	return dt_set_prop(node, "interrupts", irq, sizeof(irq));
}

void byte_chan_partition_init(guest_t *guest)
{
	int ret = dt_for_each_compatible(guest->devtree, "fsl,hv-byte-channel-handle",
	                                 byte_chan_partition_init_one, guest);
	if (ret < 0)
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: error %d\n", __func__, ret);
}

/** Send data through a byte channel
 *
 * @param[in] bc the byte channel to use
 * @param[in] buf the data to send
 * @param[in] len the number of bytes to send
 * @return the number of bytes sent
 */
ssize_t byte_chan_send(byte_chan_handle_t *bc, const uint8_t *buf, size_t len)
{
	int ret = queue_write(bc->tx, buf, len);
	
	if (ret > 0)
		queue_notify_consumer(bc->tx);

	return ret;
}

/** Receive data through a byte channel
 *
 * @param[in] bc the byte channel to use
 * @param[out] buf the buffer to fill with data
 * @param[in] len the size of the buffer
 * @return the number of bytes received
 */
ssize_t byte_chan_receive(byte_chan_handle_t *bc, uint8_t *buf, size_t len)
{
	int ret = queue_read(bc->rx, buf, len);

	if (ret > 0)
		queue_notify_producer(bc->rx);

	if (ret == -1)
		ret = 0;

	return ret;
}

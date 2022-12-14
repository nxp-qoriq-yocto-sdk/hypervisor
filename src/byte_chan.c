/** @file
 * Byte channels.
 */

/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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

#include <stdint.h>
#include <string.h>

#include <libos/queue.h>
#include <libos/alloc.h>
#include <libos/bitops.h>
#include <libos/ns16550.h>

#include <byte_chan.h>
#include <bcmux.h>
#include <errors.h>
#include <devtree.h>
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
	ret->handles[0].user.bc = &ret->handles[0];

	ret->handles[1].tx = &ret->q[1];
	ret->handles[1].rx = &ret->q[0];
	ret->handles[1].user.bc = &ret->handles[1];

	return ret;

err_q0:
	queue_destroy(&ret->q[0]);
err_bc:
	free(ret);
	return NULL;
}

uint32_t bchan_lock;

byte_chan_handle_t *byte_chan_claim(byte_chan_t *bc)
{
	byte_chan_handle_t *handle;
	register_t saved = 0;
	int lock = !spin_lock_held(&bchan_lock);
	int i;

	if (lock)
		saved = spin_lock_intsave(&bchan_lock);

	for (i = 0; i < 2; i++) {
		handle = &bc->handles[i];

		if (!handle->attached) {
			handle->attached = 1;
			break;
		}
	}

	if (lock)
		spin_unlock_intsave(&bchan_lock, saved);

	return i == 2 ? NULL : handle;
}

static int __byte_chan_attach_chardev(byte_chan_t *bc, chardev_t *cd)
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

int byte_chan_attach_chardev(byte_chan_t *bc, chardev_t *cd)
{
	register_t saved;
	int ret;

	saved = spin_lock_intsave(&bchan_lock);
	ret = __byte_chan_attach_chardev(bc, cd);
	spin_unlock_intsave(&bchan_lock, saved);

	return ret;
}

static int connect_byte_channel(byte_chan_t *bc, dt_node_t *endpoint,
                                dt_node_t *bcnode)
{
	chardev_t *cd = NULL;
	mux_complex_t *mux;
	int ret;

	if (endpoint->endpoint)
		cd = endpoint->endpoint->dev.chardev;
	if (cd) {
		ret = __byte_chan_attach_chardev(bc, cd);
		if (ret == ERR_BUSY) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: endpoint %s in %s is full\n",
			         __func__, endpoint->name, bcnode->name);
			return 0;
		}

		return ret;
	}

#ifdef CONFIG_BCMUX
	mux = endpoint->bcmux;
	if (mux) {
		dt_prop_t *channel = dt_get_prop(bcnode, "mux-channel", 0);
		if (!channel || channel->len != 4) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: bad/missing mux channel in %s\n",
			         __func__, bcnode->name);
			return 0;
		}

		ret = mux_complex_add(mux, bc, *(const uint32_t *)channel->data);
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

static int byte_chan_attach_guest(dt_node_t *node, guest_t *guest)
{
	byte_chan_handle_t *handle = node->bch;
	vpic_interrupt_t *rxirq, *txirq;
	dt_node_t *gnode, *handles;
	uint32_t intspec[4];
	int ghandle;

	rxirq = vpic_alloc_irq(guest, 0);
	txirq = vpic_alloc_irq(guest, 0);

	if (!rxirq || !txirq)
		goto nomem;

	if (vpic_alloc_handle(rxirq, &intspec[0], 0) < 0||
	    vpic_alloc_handle(txirq, &intspec[2], 0) < 0)
		return ERR_NORESOURCE;

	ghandle = alloc_guest_handle(guest, &handle->user);
	if (ghandle < 0)
		return ghandle;

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

	handles = get_handles_node(guest);
	if (!handles)
		goto nomem;

	gnode = dt_get_subnode(handles, node->name, 1);
	if (!gnode)
		goto nomem;

	dt_record_guest_phandle(gnode, node);

	if (dt_set_prop_string(gnode, "compatible", "epapr,hv-byte-channel") < 0)
		goto nomem;
	if (dt_set_prop(gnode, "reg", &ghandle, 4) < 0)
		goto nomem;
	if (dt_set_prop(gnode, "hv-handle", &ghandle, 4) < 0)
		goto nomem;
	if (dt_set_prop(gnode, "interrupts", intspec, sizeof(intspec)) < 0)
		goto nomem;

	int ret = dt_process_node_update(guest, gnode, node);
	if (ret < 0) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			 "%s: error %d merging node-update on %s\n",
			 __func__, ret, node->name);
		return ret;
	}

	create_aliases(node, gnode, guest->devtree);
	return 0;

nomem:
	printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
	return ERR_NOMEM;
}

/* Given a node containing a byte-channel endpoint, initialize
 * it.  Handles byte-channels connected to char device, uart
 * mux, or another byte-channel.
 */
int init_byte_channel(dt_node_t *node)
{
	dt_prop_t *endpoint;
	dt_node_t *epnode = NULL;
	uint32_t phandle;
	register_t saved;

	endpoint = dt_get_prop(node, "endpoint", 0);
	if (endpoint) {
		if (endpoint->len != 4) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: %s has bad endpoint property\n",
			         __func__, node->name);
			return 0;
		}
		phandle = *(const uint32_t *)endpoint->data;

		epnode = dt_lookup_phandle(config_tree, phandle);
		if (!epnode) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: endpoint in %s not found.\n",
			         __func__, node->name);
			return 0;
		}
	}

	saved = spin_lock_intsave(&bchan_lock);
	assert(!node->bch);
	assert(!node->endpoint == !node->bc);

	if (node->endpoint) {
		if (epnode &&
		    (node->endpoint != epnode ||
		     epnode->endpoint != node)) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: endpoint %s in %s does not match %s.\n",
			         __func__, epnode->name, node->name,
			         node->endpoint->name);
			goto out;
		}

		epnode = node->endpoint;
	} else if (epnode) {
		assert(!node->endpoint);

		if (epnode->bc) {
			node->bc = epnode->bc;
		} else {
			node->bc = byte_chan_alloc();
			if (!node->bc)
				goto nomem;
		}

		if (dt_node_is_compatible(epnode, "byte-channel")) {
			assert(epnode->endpoint != node);

			if (epnode->endpoint) {
				printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
				         "%s: %s (endpoint of %s) is busy, other endpoint %s\n",
				         __func__, epnode->name, node->name,
				         epnode->endpoint->name);
				goto out;
			}

			epnode->endpoint = node;
			epnode->bc = node->bc;
		} else {
			assert(!epnode->bc);

			int ret = connect_byte_channel(node->bc, epnode, node);
			if (ret < 0) {
				printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
				         "%s: error %d connecting %s to %s\n",
				         __func__, ret, node->name, epnode->name);
				goto out;
			}
		}

		node->endpoint = epnode;
	} else {
		node->bc = byte_chan_alloc();
		if (!node->bc)
			goto nomem;
	}

	node->bch = byte_chan_claim(node->bc);
	assert(node->bch);

out:
	spin_unlock_intsave(&bchan_lock, saved);
	return 0;

nomem:
	spin_unlock_intsave(&bchan_lock, saved);
	printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
	return ERR_NOMEM;
}

static int byte_chan_partition_init_one(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	int ret;

	ret = init_byte_channel(node);
	if (ret)
		return ret;

	if (node->bch)
		byte_chan_attach_guest(node, guest);

	return 0;
}

byte_chan_t *other_attach_byte_chan(dt_node_t *bcnode, dt_node_t *onode)
{
	byte_chan_t *ret = NULL;
	register_t saved = spin_lock_intsave(&bchan_lock);

	if (bcnode->endpoint) {
		printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
		         "%s: endpoint %s of %s is busy\n",
		         __func__, bcnode->name, onode->name);
		goto out;
	}

	ret = bcnode->bc;
	if (!ret) {
		bcnode->bc = ret = byte_chan_alloc();
		if (!ret) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "%s: out of memory\n", __func__);
			goto out;
		}
	}

	bcnode->endpoint = onode;
	onode->endpoint = bcnode;

out:
	spin_unlock_intsave(&bchan_lock, saved);
	return ret;
}

void byte_chan_partition_init(guest_t *guest)
{
	int ret = dt_for_each_compatible(guest->partition, "byte-channel",
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
		queue_notify_consumer(bc->tx, 0);

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
	int ret = queue_read(bc->rx, buf, len, 0);

	if (ret > 0)
		queue_notify_producer(bc->rx);

	if (ret == -1)
		ret = 0;

	return ret;
}

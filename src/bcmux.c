/** @file
 *
 * Byte channel multiplexing
 */

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
#include <libos/queue.h>
#include <libos/ns16550.h>
#include <libos/alloc.h>

#include <byte_chan.h>
#include <bcmux.h>
#include <errors.h>
#include <devtree.h>

#include <string.h>

static connected_bc_t *channel_find(mux_complex_t *mux, char id)
{
	connected_bc_t *bc = mux->first_bc;

	while (bc) {
		if (bc->num == id)
			return bc;

		bc = bc->next;
	}

	return NULL;
}

/* Ctrl-X introduces a command, double Ctrl-X for a literal Ctrl-X */
#define CH_SWITCH_ESCAPE 0x18 

/* After Ctrl-X, '0' switches to channel 0, '1' to channel 1, etc */
#define CHAN0_MUX_CHAR '0'

/* After Ctrl-X, Ctrl-A tells the remote end to resend its TX channel
 * switch the next time it sends data.
 */
#define TX_RESET 0x01

/** Demultiplex incoming data
 *
 * This function reads data from multiplexed channel and writes it
 * to the appropriate connected channel(s).
 */
static void mux_get_data(queue_t *q)
{
	mux_complex_t *mux = q->consumer;
	queue_t *notify = NULL;
	int ch;

	if (spin_lock_held(&mux->byte_chan->rx_lock)) {
		printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
			"mux_get_data: bcmux recursion detected\n");
		return;
	}

	register_t saved = spin_lock_intsave(&mux->byte_chan->rx_lock);

	/* Add a string to byte channel */
	while ((ch = queue_readchar(mux->byte_chan->rx, 0)) >= 0) {
		mux->rx_count++;

		if (mux->rx_flag_state == 1) {
			mux->rx_flag_state = 0;
			if (ch == TX_RESET) {
				mux->current_tx_bc = NULL;
				continue;
			}

			if (ch >= CHAN0_MUX_CHAR) {
				if (notify) {
					queue_notify_consumer(notify);
					notify = NULL;
				}

				mux->current_rx_bc =
					channel_find(mux, ch - CHAN0_MUX_CHAR);

				printlog(LOGTYPE_BCMUX, LOGLEVEL_DEBUG,
				         "%s: switched to channel %d (%p)\n",
				         __func__, ch - CHAN0_MUX_CHAR, mux->current_rx_bc);
				continue;
			}

			if (ch != CH_SWITCH_ESCAPE) {
				printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
				         "%s: bad command %d\n", __func__, ch);
				continue;
			}
		} else if (ch == CH_SWITCH_ESCAPE) {
			mux->rx_flag_state = 1;
			continue;
		}

		if (mux->current_rx_bc) {
			queue_t *txq = mux->current_rx_bc->byte_chan->tx;
			int ret = queue_writechar(txq, ch);

			printlog(LOGTYPE_BCMUX, LOGLEVEL_VERBOSE,
				"mux: receive '%c'\n", ch);

			/* If there's no room in outbound queue, discard the data
			 * rather than stop processing input that could be
			 * for another channel.
			 */
			if (ret < 0)
				mux->rx_discarded++;
			else
				notify = txq;
		}
	}

	spin_unlock_intsave(&mux->byte_chan->rx_lock, saved);

	if (notify)
		queue_notify_consumer(notify);
};

static int mux_tx_switch(mux_complex_t *mux, connected_bc_t *cbc)
{
	int ret;

	if (mux->current_tx_bc == cbc)
		return 0;
	if (queue_get_space(mux->byte_chan->tx) < 2)
		return -1;

	printlog(LOGTYPE_BCMUX, LOGLEVEL_DEBUG,
		"mux_tx_switch: switched to channel '%d' (%p)\n",
		cbc->num, cbc);

	mux->current_tx_bc = cbc;
	smp_mbar();

	ret = queue_writechar(mux->byte_chan->tx, CH_SWITCH_ESCAPE);
	assert(ret == 0);
	ret = queue_writechar(mux->byte_chan->tx, cbc->num+CHAN0_MUX_CHAR);
	assert(ret == 0);

	return 0;
}

static int __mux_send_data(mux_complex_t *mux, connected_bc_t *cbc)
{
	int ret, c, sent;

	if (queue_empty(cbc->byte_chan->rx))
		return 0;

	sent = mux_tx_switch(mux, cbc);
	if (sent < 0)
		return -1;

	while (queue_get_space(mux->byte_chan->tx) >= 2) {
		c = queue_readchar(cbc->byte_chan->rx, 0);
		if (c < 0)
			break;

		printlog(LOGTYPE_BCMUX, LOGLEVEL_VERBOSE,
			"mux: send '%c'\n", c);

		if (c == CH_SWITCH_ESCAPE) {
			/* write the escape char twice */
			ret = queue_writechar(mux->byte_chan->tx, c);
			assert(ret >= 0);
			ret = queue_writechar(mux->byte_chan->tx, c);
			assert(ret >= 0);
			sent += 2;
		} else {
			ret = queue_writechar(mux->byte_chan->tx, c);
			assert(ret >= 0);
			sent++;
		}
	}

	return sent;
};

static void mux_send_data_pull(queue_t *q)
{
	mux_complex_t *mux = q->producer;
	int ret, total = 0;

	if (spin_lock_held(&mux->byte_chan->tx_lock)) {
		printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
			"mux_send_data_pull: bcmux recursion detected\n");
		return;
	}

	register_t saved = spin_lock_intsave(&mux->byte_chan->tx_lock);

	connected_bc_t *first_cbc = mux->next_tx_pull_bc;
	if (!first_cbc)
		first_cbc = mux->first_bc;

	connected_bc_t *cbc = first_cbc;

	do {
		ret = __mux_send_data(mux, cbc);
		if (ret > 0)
			queue_notify_producer(cbc->byte_chan->rx);

		cbc = cbc->next;
		if (!cbc)
			cbc = mux->first_bc;

		/* If we filled the output queue with this channel, start
		 * with the next one on the next pull.  It's not perfectly
		 * fair, but it avoids starvation.
		 */
		if (ret < 0) /* no more room */
			break;

		/* Limit total work per invocation to limit latency. */
		total += ret;
	} while (cbc != first_cbc && total < 64);

	/* If there was no data to be sent, remove the pull callback. */
	if (ret == 0 && total == 0)
		q->space_avail = NULL;

	mux->next_tx_pull_bc = cbc;
	spin_unlock_intsave(&mux->byte_chan->tx_lock, saved);
}

static void mux_send_data_push(queue_t *q)
{
	connected_bc_t *cbc = q->consumer;
	mux_complex_t *mux = cbc->mux_complex;
	int lock = !unlikely(cpu->crashing);
	register_t saved = disable_int_save();
	int ret;

	if (lock) {
		if (spin_lock_held(&mux->byte_chan->tx_lock)) {
			printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
				"mux_send_data_push: bcmux recursion detected\n");
			return;
		}

		spin_lock(&mux->byte_chan->tx_lock);
	}

again: 
	ret = __mux_send_data(mux, cbc);
	if (ret > 0)
		queue_notify_consumer(mux->byte_chan->tx);

	if (!queue_empty(cbc->byte_chan->rx)) {
		/* If we ran out of space, arm the pull callback. */
		mux->byte_chan->tx->space_avail = mux_send_data_pull;
		smp_sync();

		/* Check again to see if there was a race, and space was
		 * alredy made available.  We assume the queue size is
		 * larger than 16.
		 */
		if (queue_get_space(mux->byte_chan->tx) >= 16)
			goto again;
	}

	if (lock)
		spin_unlock(&mux->byte_chan->tx_lock);

	restore_int(saved);
}

/** Create a byte channel multiplexer.
 *
 * @param[in] bc byte channel handle of multiplexed stream
 * @param[out] mux_p newly created multiplexer
 */
static mux_complex_t *mux_complex_init(byte_chan_t *bc)
{
	byte_chan_handle_t *handle = byte_chan_claim(bc);
	if (!handle)
		return NULL;

	mux_complex_t *mux = alloc_type(mux_complex_t);
	if (!mux)
		return NULL;

	mux->byte_chan = handle;

	/* Here we register a mux as an end point for byte channel */
	handle->tx->producer = mux;
	handle->rx->consumer = mux;

	handle->tx->space_avail = NULL;
	handle->rx->data_avail = mux_get_data;

	return mux;
}

/** Register a byte channel with a multiplexer
 *
 * @param[in] mux multiplexer to register with
 * @param[in] bc byte channel handle to register (must not be
 * attached to anything else, or data may be corrupted).
 * @param[in] multiplexing_id channel descriptor
 */
int mux_complex_add(mux_complex_t *mux, byte_chan_t *bc,
                    char multiplexing_id)
{
	if ((unsigned char)multiplexing_id >= MAX_MUX_CHANNELS)
		return ERR_RANGE;

	byte_chan_handle_t *handle = byte_chan_claim(bc);
	if (!handle)
		return ERR_BUSY;

	connected_bc_t *cbc;
	cbc = alloc_type(connected_bc_t);
	if (!cbc)
		return ERR_NOMEM;

	cbc->num = multiplexing_id;
	cbc->byte_chan = handle;
	cbc->mux_complex = mux;

	handle->tx->producer = cbc;
	handle->rx->consumer = cbc;

	handle->tx->space_avail = NULL;
	handle->rx->data_avail = mux_send_data_push;

	register_t saved = spin_lock_intsave(&mux->byte_chan->rx_lock);
	spin_lock(&mux->byte_chan->tx_lock);

	if (mux->first_bc == NULL) {
		mux->first_bc = cbc;
		mux->current_rx_bc = mux->first_bc;
	} else {
		connected_bc_t *bc_chain = mux->first_bc;

		while (bc_chain->next != NULL)
			bc_chain = bc_chain->next;
		bc_chain->next = cbc;
	}

	mux->num_of_channels++;

	/* Not much we can easily do if these fail -- but it's init time,
	 * so they shouldn't.
	 */
	queue_writechar(mux->byte_chan->tx, CH_SWITCH_ESCAPE);
	queue_writechar(mux->byte_chan->tx, TX_RESET);

	spin_unlock(&mux->byte_chan->tx_lock);
	spin_unlock_intsave(&mux->byte_chan->rx_lock, saved);

	queue_notify_consumer(mux->byte_chan->tx);
	return 0;
}

static byte_chan_t *mux_attach_byte_chan(dt_node_t *muxnode, dt_node_t *bcnode)
{
	if (dt_node_is_compatible(bcnode, "byte-channel"))
		return other_attach_byte_chan(bcnode, muxnode);

	return NULL;
}

static byte_chan_t *mux_attach_chardev(dt_node_t *muxnode, dt_node_t *cdnode)
{
	if (dt_get_prop(cdnode, "device", 0) && cdnode->endpoint) {
		chardev_t *cd = cdnode->endpoint->dev.chardev;
		if (!cd)
			return NULL;

		byte_chan_t *bc = byte_chan_alloc();
		if (!bc) {
			printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
			         "%s: out of memory\n", __func__);
			return NULL;
		}

		int ret = byte_chan_attach_chardev(bc, cd);
		if (ret < 0) {
			printf("%s: error %d attaching %s to %s\n",
			       __func__, ret, muxnode->name, cdnode->name);
			return NULL;
		}

		return bc;
	}

	return NULL;
}

static int create_mux(dt_node_t *node, void *arg)
{
	dt_prop_t *prop;
	dt_node_t *endpoint;
	byte_chan_t *bc;

	prop = dt_get_prop(node, "endpoint", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
		         "%s: missing or bad endpoint in %s.\n",
		         __func__, node->name);
		return 0;
	}

	endpoint = dt_lookup_phandle(config_tree, *(const uint32_t *)prop->data);
	if (!endpoint) {
		printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
		         "%s: Cannot find endpoint in %s.\n",
		         __func__, node->name);
		return 0;
	}

	bc = mux_attach_byte_chan(node, endpoint);
	if (!bc)
		bc = mux_attach_chardev(node, endpoint);
	if (!bc) {
		printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
		         "%s: unrecognized endpoint %s in %s\n",
		         __func__, endpoint->name, node->name);
		return 0;
	}

	mux_complex_t *mux = mux_complex_init(bc);
	if (!mux)
		return ERR_NOMEM;

	node->bcmux = mux;
	return 0;
}

void create_muxes(void)
{
	int ret = dt_for_each_compatible(config_tree, "byte-channel-mux",
	                                 create_mux, NULL);
	if (ret < 0)
		printlog(LOGTYPE_BCMUX, LOGLEVEL_ERROR,
		         "%s: error %d\n", __func__, ret);
}

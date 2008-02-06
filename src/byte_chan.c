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
#include <stdint.h>
#include <string.h>

// Must be a power of two.
#define QUEUE_SIZE 256

/// Allocate a byte channel.
byte_chan_t *byte_chan_alloc(void)
{
	byte_chan_t *ret = alloc(sizeof(byte_chan_t), __alignof__(byte_chan_t));
	if (!ret)
		return NULL;

	// FIXME: free() on failure
	if (queue_init(&ret->q[0], QUEUE_SIZE))
		return NULL;
	if (queue_init(&ret->q[1], QUEUE_SIZE))
		return NULL;

	ret->handles[0].tx = &ret->q[0];
	ret->handles[0].rx = &ret->q[1];
	ret->handles[1].tx = &ret->q[1];
	ret->handles[1].rx = &ret->q[0];

	return ret;
}

static uint32_t bchan_lock;

int byte_chan_claim(byte_chan_handle_t *handle)
{
	register_t saved = spin_lock_critsave(&bchan_lock);
	int ret = -1;
	
	if (!handle->attached) {
		ret = 0;
		handle->attached = 1;
	}

	spin_unlock_critsave(&bchan_lock, saved);
	return ret;
}

int byte_chan_attach_chardev(byte_chan_handle_t *bc, chardev_t *cd)
{
	if (byte_chan_claim(bc))
		return -1;

	if (!cd->ops->set_tx_queue || !cd->ops->set_rx_queue)
		goto err;

	if (cd->ops->set_tx_queue(cd, bc->rx))
		goto err;
	if (cd->ops->set_rx_queue(cd, bc->tx)) {
		cd->ops->set_tx_queue(cd, NULL);
		goto err;
	}

	return 0;

err:
	bc->attached = 0;
	return -1;
}

int byte_chan_attach_guest(byte_chan_handle_t *bc, guest_t *guest)
{
	if (byte_chan_claim(bc))
		return -1;

	bc->user.bc = bc;
	
	int handle = alloc_guest_handle(guest, &bc->user);
	if (handle < 0) {
		bc->attached = 0;
		return -1;
	}

	return handle;
}

static byte_chan_t *test_bc;

/*!
    @fn byte_chan_global_init

    @brief parses global device tree and sets up all byte-channels 

*/
void byte_chan_global_init(void)
{
	/* need to parse device tree here */
	/* loop over channels */

	/* FIXME: right now this is hardcoded to
           set up 1 uart channel only */

	/* alloc a channel */
	test_bc = byte_chan_alloc();

#if 1
	/* Connect it to a bc mux */
	extern mux_complex_t *vuart_complex;
	mux_complex_add(vuart_complex, &test_bc->handles[0], '2');
#else
	/* connect it to a uart */  /* FIXME - dev tree, IRQ number, baudclock */
	chardev_t *cd = ns16550_init((uint8_t *)CCSRBAR_VA + 0x11c600, 0x24, 0, 16);
	if (!cd) {
		printf("byte_chan_global_init: failed to alloc uart\n");
		return;
	}

	if (byte_chan_attach_chardev(&test_bc->handles[0], cd)) {
		printf("byte_chan_global_init: Couldn't attach to uart\n");
		return;
	}
#endif
}

void byte_chan_partition_init(guest_t *guest)
{
	/* need to parse partition device tree here */
	/* loop over virtual-devices node */
	/* determine how many byte channels the partition has*/

	int handle = byte_chan_attach_guest(&test_bc->handles[1], guest);
	printf("byte chan guest handle %d\n", handle);
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

	return ret;
}

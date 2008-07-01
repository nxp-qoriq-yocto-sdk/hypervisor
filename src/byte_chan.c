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

void create_byte_channels(void)
{
	int off = -1, ret;

	while (1) {
		ret = fdt_node_offset_by_compatible(fdt, off, "fsl,hv-byte-channel");
		if (ret < 0)
			break;

		byte_chan_t *bc = byte_chan_alloc();
		if (!bc) {	
			printf("byte_chan_global_init: failed to create byte channel.\n");
			return;
		}

		off = ret;
		ret = fdt_setprop(fdt, off, "fsl,hv-internal-bc-ptr", &bc, sizeof(bc));
		if (ret < 0)
			break;
	}

	if (ret == -FDT_ERR_NOTFOUND)
		return;

	printf("create_byte_channels: libfdt error %d (%s).\n",
	       ret, fdt_strerror(ret));
}

static void connect_byte_channel(byte_chan_t *bc, int endpoint,
                                 int bcnode, int index)
{
	void *ptr = ptr_from_node(fdt, endpoint, "chardev");
	if (ptr) {
		int ret = byte_chan_attach_chardev(bc, ptr);
		if (ret < 0)
				printf("error %d attaching byte channel to chardev\n", ret);

		return;
	}

#ifdef CONFIG_BCMUX
	ptr = ptr_from_node(fdt, endpoint, "mux");
	if (ptr) {
		int len;
		const uint32_t *channel = fdt_getprop(fdt, bcnode,
		                                      "fsl,mux-channel", &len);

		if (!channel) {
			printf("connect_byte_channel: fsl,mux-channel: libfdt error %d (%s).\n",
			       len, fdt_strerror(len));
			return;
		}

		if (len % 4 || (index + 1) * 4 > len) {
			printf("connect_byte_channel: bad fsl,mux-channel\n");
			return;
		}
	
		int ret = mux_complex_add(ptr, bc, channel[index]);
		if (ret < 0)
				printf("error %d attaching byte channel to mux\n", ret);

		return;
	}
#endif

	printf("connect_byte_channel: unrecognized endpoint\n");
}

void connect_global_byte_channels(void)
{
	int off = -1, ret, len;
	byte_chan_t *bc;
	const uint32_t *endpoint;

	while (1) {
		ret = fdt_node_offset_by_compatible(fdt, off, "fsl,hv-byte-channel");
		if (ret == -FDT_ERR_NOTFOUND)
			return;
		if (ret < 0)
			break;

		off = ret;
		bc = ptr_from_node(fdt, off, "bc");
		if (!bc) {
			printf("connect_global_byte_channel: no pointer\n");
			continue;
		}

		endpoint = fdt_getprop(fdt, off, "fsl,endpoint", &ret);
		if (!endpoint) {
			if (ret == -FDT_ERR_NOTFOUND)
				continue;
			break;
		}

		len = ret;
		if (len != 4 && len != 8) {
			printf("connect_global_byte_channel: Invalid fsl,endpoint.\n");
			continue;
		}

		ret = fdt_node_offset_by_phandle(fdt, endpoint[0]);
		if (ret < 0) {
			if (ret == -FDT_ERR_NOTFOUND) {
				printf("connect_global_byte_channel: Invalid fsl,endpoint.\n");
				continue;
			}

			break;
		}
				
		connect_byte_channel(bc, ret, off, 0);

		if (len == 8) {
			ret = fdt_node_offset_by_phandle(fdt, endpoint[1]);
			if (ret < 0)
				break;
				
			connect_byte_channel(bc, ret, off, 1);
		}
	}

	printf("connect_global_byte_channels: libfdt error %d (%s).\n",
	       ret, fdt_strerror(ret));
}

void byte_chan_partition_init(guest_t *guest)
{
	int off = -1, ret;
	const char *endpoint;
	byte_chan_t *bc;
	uint32_t irq[4];
	vpic_interrupt_t *virq[2];

	while (1) {
		ret = fdt_node_offset_by_compatible(guest->devtree, off,
		                                    "fsl,hv-byte-channel-handle");
		if (ret == -FDT_ERR_NOTFOUND)
			return;
		if (ret < 0)
			break;

		off = ret;

		endpoint = fdt_getprop(guest->devtree, off, "fsl,endpoint", &ret);
		if (!endpoint) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "byte_chan_partition_init: no endpoint property\n");
			continue;
		}

		ret = lookup_alias(fdt, endpoint);
		if (ret < 0) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "byte_chan_partition_init: no endpoint\n");
			continue;
		}

		bc = ptr_from_node(fdt, ret, "bc");
		if (!bc) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "byte_chan_partition_init: no pointer\n");
			continue;
		}

		virq[0] = vpic_alloc_irq(guest);
		virq[1] = vpic_alloc_irq(guest);

		if (!virq[0] || !virq[1]) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "byte_chan_partition_init: can't alloc vpic irqs\n");
			return;
		}
		
		irq[0] = vmpic_alloc_handle(guest, &virq[0]->irq);
		irq[1] = 0;
		irq[2] = vmpic_alloc_handle(guest, &virq[1]->irq);
		irq[3] = 0;
		
		if (irq[0] < 0 || irq[2] < 0) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "byte_chan_partition_init: can't alloc vmpic irqs\n");
			return;
		} 

		int32_t ghandle = byte_chan_attach_guest(bc, guest, virq[0], virq[1]);
		if (ghandle < 0) {
			printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
			         "byte_chan_partition_init: cannot attach\n");
			continue;
		}

		ret = fdt_setprop(guest->devtree, off, "reg", &ghandle, 4);
		if (ret < 0)
			break;

		ret = fdt_setprop(guest->devtree, off, "interrupts", irq, sizeof(irq));
		if (ret < 0)
			break;
	}

	printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
	         "byte_chan_partition_init: error %d (%s).\n",
	         ret, ret >= -FDT_ERR_MAX ? fdt_strerror(ret) : "");
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

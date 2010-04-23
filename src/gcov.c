/** @file
 *
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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

#include <hv.h>
#include <devtree.h>
#include <byte_chan.h>
#include <bcmux.h>
#include <libos/alloc.h>
#include <libos/errors.h>

static struct byte_chan *bc;
static struct byte_chan_handle *bch;
static thread_t *gcov_thread;

struct data_info {
	uint32_t ea_size;   // size in bytes of effective addresses
	uint32_t data_size;  // size in bytes of data[]
} data_info;

typedef struct data_obj {
	void *addr;    // effective address of the data
	size_t size;   // size in bytes
} data_obj_t;

data_obj_t *gcov_data;

#define MAX_CMD_SIZE 16

/** Callback for RX interrupt.
 *
 */
static void gcov_rx(queue_t *q, int blocking)
{
	unblock(gcov_thread);
}

static void get_command(char *command)
{
	ssize_t byte_count = 0;
	uint8_t ch;
	int i = 0;

	while (1) {
		ch = queue_readchar_blocking(bch->rx, 0);
		command[i++] = ch;
		if (ch == '\0')
			break;

		if (i >= MAX_CMD_SIZE) {
			command[i] = '\0';
			break;
		}
	}
}

static void gcov_rx_thread(trapframe_t *regs, void *arg)
{
	ssize_t byte_count;
	char command[MAX_CMD_SIZE+1];
	int unknown_cmd_count = 0;

	while (1) {
		get_command(command);

		if (!strcmp(command,"get-data-info")) {
			unknown_cmd_count = 0;
			queue_write_blocking(bch->tx, (uint8_t *)&data_info,
			                     sizeof(data_info));
			queue_write_blocking(bch->tx, (uint8_t *)gcov_data,
			                     data_info.data_size);
		} else if (!strcmp(command,"get-data")) {
			size_t address;
			uint32_t length;

			unknown_cmd_count = 0;

			/* get effect addr & size from the host */
			byte_count = queue_read_blocking(bch->rx, (uint8_t *)&address,
			                                 sizeof(void *), 0);
			byte_count = queue_read_blocking(bch->rx, (uint8_t *)&length,
			                                 sizeof(size_t), 0);

			/* send the data to the host */
			queue_write_blocking(bch->tx, (uint8_t *)address, length);
		} else {
			unknown_cmd_count++;
			printlog(LOGTYPE_MISC, LOGLEVEL_WARN,
			         "warning: unknown gcov rx command %s\n",command);
		}

		if (unknown_cmd_count > 10) {  /* stop servicing gcov rx data */
			bch->rx->data_avail = NULL;
			printlog(LOGTYPE_MISC, LOGLEVEL_WARN,
			         "warning: unknown gcov command threshold exceeded, "
		                 "gcov rx service stopping\n");
			prepare_to_block();
			block();
		}
	}
}

static int gcov_byte_chan_init(dt_node_t *config, struct byte_chan **bc,
                        struct byte_chan_handle **bch)
{
	dt_node_t *mux_node;
	register_t saved;

	mux_node = dt_get_first_compatible(config, "byte-channel-mux");
	if (!mux_node || !mux_node->bcmux) {
		printlog(LOGTYPE_MISC, LOGLEVEL_WARN,
		         "warning: mux node missing or misconfigured.\n");
		return ERR_BADTREE;
	}

	assert(!*bc);
	assert(!*bch);

	saved = spin_lock_intsave(&bchan_lock);

	*bc = byte_chan_alloc();
	if (!*bc)
		goto nomem;

	if (mux_complex_add(mux_node->bcmux, *bc, CONFIG_GCOV_CHANNEL)) {
		spin_unlock_intsave(&bchan_lock, saved);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: error adding gcov byte-chan to mux\n",
		         __func__);
		return ERR_BADTREE;
	}

	*bch = byte_chan_claim(*bc);

	assert(*bch);

	spin_unlock_intsave(&bchan_lock, saved);
	return 0;

nomem:
	spin_unlock_intsave(&bchan_lock, saved);
	printlog(LOGTYPE_BYTE_CHAN, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
	return ERR_NOMEM;
}

void gcov_config(dt_node_t *config)
{
	if (gcov_byte_chan_init(config, &bc, &bch))
		return;

	gcov_thread = new_thread(gcov_rx_thread, NULL, 1);
	if (!gcov_thread) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "failed to create gcov thread\n");
		return;
	}

	data_info.ea_size = sizeof(void *);
	// FIXME: set correct data_info.size here
	data_info.data_size = sizeof(data_obj_t) * 1;

	gcov_data = malloc(data_info.data_size);
	if (!gcov_data) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return;
	}

	// FIXME populate gcov_data here

	/* register the callbacks */
	smp_lwsync();
	bch->rx->data_avail = gcov_rx;

	unblock(gcov_thread);
}


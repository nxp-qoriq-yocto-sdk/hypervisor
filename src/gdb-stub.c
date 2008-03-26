/** @file
 * HV GDB Stub.
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

#include <stdint.h>
#include <libfdt.h>

#include <libos/trapframe.h>

#include <events.h>

#include <byte_chan.h>
#include <gdb-stub.h>

/* TODO:
 * 0. Make all error messages uniform across all printf() calls.
 * 1. Add descriptive comment per function/data-structure.
 * 2. Macroise printfs for reporting error messages.
 */

/* If debugging, define DEBUG_GDB_STUB to 1, else 0; for enabling printf's
 * at program points that have proved to be helpful to collect state at.
 */
#define DEBUG_GDB_STUB 1

/* Context prefix for error messages. */
#define CTXT_EMSG "gdb-stub"

static const byte_chan_t *find_gdb_stub_byte_channel(void);
static int register_callbacks(void);

extern void *fdt;

/**
 * Enumerate the various events that we are interested in. The external world in
 * the hypervisor only knows that a "GDB event" occurred. However, within the 
 * stub, we need to have a finer view of things, as captured in the following 
 * enum 'event_type'. The global variable 'event_type', is used to record the 
 * current event.
 */

enum event_type {

	/* RX interrupt. */
	received_data,
};

/* TODO: make it per-guest. */
enum event_type event_type;

/**
 * The byte channel for the GDB stub. All communication in and out of the GDB
 * stub flows through this byte channel.
 *
 * TODO: There's more than one guest; thus, there can be more than one GDB 
 *       stub active.
 */

static const byte_chan_t *byte_channel = NULL;
static byte_chan_handle_t *byte_channel_handle = NULL;

/** Find in the device-tree, the byte-channel that the gdb-stub is connected to.
 * @return pointer to the byte-channel with the gdb-stub as it's end-point.
 */
static const byte_chan_t *find_gdb_stub_byte_channel(void)
{
	int start_offset = -1; 
	int prop_length = 0;
	int node_offset = 0;
	const uint32_t *fsl_ep = NULL;
	uint32_t bc_phandle = 0;
	int bc_offset = 0;
	byte_chan_t *const *bc_ptr = NULL;

	/* Search flat device tree for gdb-stub node. 
	 */
	node_offset = fdt_node_offset_by_compatible(fdt, start_offset, 
	                                            "fsl,hv-gdb-stub");
	if (node_offset == -FDT_ERR_NOTFOUND) {
		/* This is not an error - it just means that no GDB stubs were 
		 * configured. We should return silently. 
		 */
		return NULL;
	}
	if (node_offset < 0) {
		printf("%s: %s: libfdt error %d (%s).\n", CTXT_EMSG, __func__, 
		       node_offset, fdt_strerror(node_offset));
		return NULL;
	}

	/* Get value of fsl,endpoint (a phandle), which gives you the offset of
	 * the byte-channel in the device tree.
	 */
	fsl_ep = fdt_getprop(fdt, node_offset, "fsl,endpoint", &prop_length);
	if (fsl_ep == NULL) {
		if (prop_length == -FDT_ERR_NOTFOUND) {
			printf("%s: %s: Did not find fsl,endpoint in gdb-stub "
			       "node in the device tree.\n", CTXT_EMSG, 
			       __func__);
			return NULL;
		}
		printf("%s: %s: Internal error: libfdt error return: %d.\n", 
		       CTXT_EMSG, __func__, prop_length);
		return NULL;
	}
	if (prop_length != 4) {
		printf("%s: %s: Invalid fsl,endpoint.\n", CTXT_EMSG, __func__);
		return NULL;
	}
	bc_phandle = *fsl_ep;
	bc_offset = fdt_node_offset_by_phandle(fdt, bc_phandle);

	/* Get value of the internally created property: fsl,hv-internal-bc-ptr.
	 * The value is a pointer to the byte-channel as obtained upon a
	 * byte_chan_alloc()
	 */
	bc_ptr = fdt_getprop(fdt, bc_offset, "fsl,hv-internal-bc-ptr", 
		             &prop_length);
#if DEBUG_GDB_STUB
	/* Sanity check what you got. */
	printf("bc_ptr: 0x%x, *bc_ptr: 0x%x\n", 
	       (unsigned int) bc_ptr, 
	       (unsigned int) *bc_ptr);
#endif
	if (bc_ptr == NULL) {
		if (prop_length == -FDT_ERR_NOTFOUND) {
			printf("%s: %s: endpoint is not a byte channel\n", 
			       CTXT_EMSG, __func__);
			return NULL;
		}
		printf("%s: %s: Internal error: libfdt error return: %d.\n", 
		       CTXT_EMSG, __func__, prop_length);
		return NULL;
	}
	if (prop_length != 4) {
		printf("%s: %s: gdb-stub got invalid fsl,hv-internal-bc-ptr.\n",
		       CTXT_EMSG, __func__);
		return NULL;
	}

	/* OK, we have a good byte-channel to use. */
	return *bc_ptr; /* PS: Note deref. */
}

/** Callback for RX interrupt.
 * 
 */ 
static void rx(struct queue_t *q)
{
	/* Record the event type and setevent EV_GDB on the current CPU. */
	event_type = received_data;
	setevent(get_gcpu(), EV_GDB);
	/* TODO: What if there were some other event in progress?  It'll be 
	 * lost. The GDB event handler should check whether the input queue 
	 * is empty, rather than rely on a "received data" flag. 
	 */
}

static int register_callbacks(void)
{
	if (byte_channel != NULL) {
		byte_channel_handle = byte_chan_claim((byte_chan_t *) 
		                                      byte_channel);
		if (byte_channel_handle == NULL) {
			printf("%s: %s: gdb-stub failed to claim gdb-stub"
			       " byte-channel.\n", CTXT_EMSG, __func__);
			return GDB_STUB_INIT_FAILURE;
		}

		/* No callback on a TX, since we're polling. 
		 * Register RX callback.
		 */
		byte_channel_handle->rx->data_avail = rx;
		return GDB_STUB_INIT_SUCCESS;
	}
	else
		return GDB_STUB_INIT_FAILURE;
}

int gdb_stub_init(void)
{
	byte_channel = find_gdb_stub_byte_channel();
	return register_callbacks();
}

void gdb_stub_event_handler(trapframe_t *trap_frame)
{
	size_t len = 8, i = 0;
	uint8_t buf[len]; 
	ssize_t byte_count = 0;

	/* Zeroeth thing to do here is to send program state to GDB on host and
	 * proceed to handle the event.
	 */
#if DEBUG_GDB_STUB
#define TEST_MESSAGE "HV GDB stub.\n"

	byte_chan_send(byte_channel_handle, (const uint8_t *) TEST_MESSAGE, 
	               sizeof(TEST_MESSAGE));
#endif

	/* Handle the actual event that has occurred. */
	switch (event_type) {

		case received_data:
			/* Now, while data is available, do a byte_chan_receive.
			 */
			byte_count = byte_chan_receive(byte_channel_handle, buf,
			                               len);
			while (i < byte_count) {
				printf("got: %c\n", (buf[i]));
				i++;
			}
			/* Do not send back stuff yet - it'll confuse GDB.
			 * buf[byte_count] = '\n';
			 * byte_chan_send(byte_channel_handle, buf, byte_count+1)
			 */
			break;
	}
}

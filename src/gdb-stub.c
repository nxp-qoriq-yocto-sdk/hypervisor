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
 * 0. Add descriptive comment per function/data-structure.
 * 1. Do not use globals, used alloc() instead to carve out
 *    chunks of memory at init time.
 *    CAUTION: Do not free the memory you got from alloc().
 * 2. Add tracers to byte_chan_(send, receive) and commit.
 *    under #ifdef DEBUG.
 */

/* Context prefix for error messages. */
#define CTXT_EMSG "gdb-stub"

#define TRACE(fmt, info...) printlog(LOGTYPE_GDB_STUB,    \
				LOGLEVEL_DEBUG,           \
				"%s@[%s, %d]: " fmt "\n", \
				__func__, __FILE__, __LINE__, ## info)

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

	TRACE();

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
		TRACE("%s: %s: libfdt error %d (%s).\n", CTXT_EMSG, __func__,
		       node_offset, fdt_strerror(node_offset));
		return NULL;
	}

	/* Get value of fsl,endpoint (a phandle), which gives you the offset of
	 * the byte-channel in the device tree.
	 */
	fsl_ep = fdt_getprop(fdt, node_offset, "fsl,endpoint", &prop_length);
	if (fsl_ep == NULL) {
		if (prop_length == -FDT_ERR_NOTFOUND) {
			TRACE("%s: %s: Did not find fsl,endpoint in gdb-stub "
			       "node in the device tree.\n", CTXT_EMSG, 
			       __func__);
			return NULL;
		}
		TRACE("%s: %s: Internal error: libfdt error return: %d.\n",
		       CTXT_EMSG, __func__, prop_length);
		return NULL;
	}
	if (prop_length != 4) {
		TRACE("%s: %s: Invalid fsl,endpoint.\n", CTXT_EMSG, __func__);
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
	TRACE("bc_ptr: 0x%x, *bc_ptr: 0x%x\n",
		(unsigned int) bc_ptr,
		(unsigned int) *bc_ptr);
	if (bc_ptr == NULL) {
		if (prop_length == -FDT_ERR_NOTFOUND) {
			TRACE("%s: %s: endpoint is not a byte channel\n",
			       CTXT_EMSG, __func__);
			return NULL;
		}
		TRACE("%s: %s: Internal error: libfdt error return: %d.\n",
		       CTXT_EMSG, __func__, prop_length);
		return NULL;
	}
	if (prop_length != 4) {
		TRACE("%s: %s: gdb-stub got invalid fsl,hv-internal-bc-ptr.\n",
		       CTXT_EMSG, __func__);
		return NULL;
	}

	/* OK, we have a good byte-channel to use. */
	return *bc_ptr; /* PS: Note deref. */
}

/** Callback for RX interrupt.
 * 
 */ 
static void rx(queue_t *q)
{
	TRACE();
	/* Record the event type and setevent GEV_GDB on the current CPU. */
	event_type = received_data;
	setgevent(get_gcpu(), GEV_GDB);
	/* TODO: What if there were some other event in progress?  It'll be 
	 * lost. The GDB event handler should check whether the input queue 
	 * is empty, rather than rely on a "received data" flag. 
	 */
}

static int register_callbacks(void)
{
	TRACE();
	if (byte_channel != NULL) {
		byte_channel_handle = byte_chan_claim((byte_chan_t *) 
		                                      byte_channel);
		if (byte_channel_handle == NULL) {
			TRACE("%s: %s: gdb-stub failed to claim gdb-stub"
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
	TRACE();
	byte_channel = find_gdb_stub_byte_channel();
	return register_callbacks();
}

/* get_debug_char() and put_debug_char() are our
 * means of communicating with the outside world,
 * one character at a time.
 */

static uint8_t get_debug_char(void)
{
	ssize_t byte_count = 0;
	uint8_t ch;
	TRACE();
	do {
		byte_count = byte_chan_receive(byte_channel_handle, &ch, 1);
		/* internal error if byte_count > len */
	} while (byte_count <= 0);
	return ch;
}

static void put_debug_char(uint8_t c)
{
	size_t len = 1;
	uint8_t buf[len];
	buf[0] = c;
	TRACE();
	byte_chan_send(byte_channel_handle, buf, len);
}

#define NUMGPRS  32
#define NUMFPRS  32
#define NUMNIA   1
#define NUMMSR   1
#define NUMCR    1
#define NUMLR    1
#define NUMCTR   1
#define NUMXER   1
#define NUMFPSCR 1

#define NUM_32_BIT_REGS NUMGPRS +                                            \
                        NUMNIA  + NUMMSR + NUMCR + NUMLR + NUMCTR + NUMXER + \
                        NUMFPSCR

#define NUM_64_BIT_REGS NUMFPRS

/* Number of bytes of registers.  */
#define NUMREGBYTES (NUM_32_BIT_REGS * 4) + (NUM_64_BIT_REGS * 8)

/* BUFMAX defines the maximum number of characters in inbound/outbound buffers;
 * at least NUMREGBYTES*2 are needed for register packets
 */
#define BUFMAX      4096
#define BUFMAX_HEX "1000"

/* TODO: Where do we call this? */
static inline int bufsize_sanity_check(void)
{
	return BUFMAX >= NUMREGBYTES * 2;
}

#define ACK '+'
#define NAK '-'

static char hexit[] =
{
	'0', '1', '2', '3',
	'4', '5', '6', '7',
	'8', '9', 'a', 'b',
	'c', 'd', 'e', 'f',
};

typedef struct pkt
{
	uint8_t *buf;
	unsigned int len;
	uint8_t *cur;
} pkt_t;

/* These two packets contain the command received from GDB and the response to
 * be sent to GDB. cmd is to always contain a command whoose checksum has been
 * verified and with the leading '$', trailing '#' and 'checksum' removed.
 * Similarly, rsp is always to contain the content of the response packet
 * _without_ the leading '$' and '#' and the 'checksum'.
 * (All routines that operate on these buffers must ensure and should
 * assume that the content in these buffers is '\0' terminated).
 */
static uint8_t cbuf[BUFMAX];
static pkt_t command = { cbuf, BUFMAX, cbuf };
static pkt_t *cmd = &command;

static uint8_t rbuf[BUFMAX];
static pkt_t response = { rbuf, BUFMAX, rbuf };
static pkt_t *rsp = &response;

/* Note: See lexeme_token_pairs. */
typedef enum token
{
	unknown_command,
	reason_halted,
	continue_execution,
	read_registers,
	set_thread,
	read_memory,
	read_register,
	return_current_thread_id,
	get_section_offsets,
	supported_features,
} token_t;

typedef struct lexeme_token_pair
{
	char *lexeme;
	token_t token;
} lexeme_token_pair_t;

/* Note: lexeme_token_pairs and enum token_t have to be kept in synch as the token
 *       values are used to index the array.
 */
static lexeme_token_pair_t lexeme_token_pairs[] =
{
	{ "?", reason_halted },
	{ "c", continue_execution },
	{ "g", read_registers },
	{ "H", set_thread },
	{ "m", read_memory },
	{ "p", read_register },
	{ "qC", return_current_thread_id },
	{ "qOffsets", get_section_offsets },
	{ "qSupported", supported_features },
};

/* Aux */
static inline uint8_t checksum(uint8_t *p);
static inline uint8_t hdtoi(uint8_t hexit);
static inline uint8_t upper_nibble(uint8_t c);
static inline uint8_t lower_nibble(uint8_t c);
static inline token_t tokenize(uint8_t *lexeme);
static inline uint8_t hex(uint8_t c);

/* PRT */
static inline int bufsize_sanity_check(void);
static inline int pkt_len(pkt_t *pkt);
static inline int pkt_space(pkt_t *pkt);
static inline void pkt_write_byte(pkt_t *pkt, uint8_t c);
static inline void pkt_update_cur(pkt_t *pkt);
static inline void pkt_write_byte_update_cur(pkt_t *pkt, uint8_t c);
static inline void pkt_cat_string(pkt_t *pkt, char *s);
static inline uint8_t pkt_read_byte(pkt_t *pkt, unsigned int i);
static inline int pkt_full(pkt_t *pkt);
static inline void pkt_reset(pkt_t *pkt);
static inline uint8_t *content(pkt_t *pkt);
static void put_debug_char(uint8_t c);
static inline void ack(void);
static inline void nak(void);
static inline int got_ack(void);
static void receive_command(pkt_t *cmd);
static void transmit_response(pkt_t *rsp);
static inline void pkt_hex_copy(pkt_t *pkt, uint8_t *p, unsigned int length);

/* RSP */
void gdb_stub_event_handler(trapframe_t *trap_frame);

/* Auxiliary routines required by the RSP Engine.
 */

static inline uint8_t checksum(uint8_t *p)
{
	uint8_t s = 0;
	TRACE();
	while (*p)
		s += *p++;

	return s; /* Automatically, mod 256. */
}

static inline uint8_t hdtoi(uint8_t hexit)
{
	TRACE();
	if ('0' <= hexit && hexit <= '9')
		return hexit - '0';

	if ('a' <= hexit && hexit <= 'f')
		return hexit - 'a' + 10;

	if ('A' <= hexit && hexit <= 'F')
		return hexit - 'A' + 10;

	TRACE("Got other than [0-9a-f].");
	return 0;
}

static inline uint8_t upper_nibble(uint8_t c)
{
	TRACE();
	return (c & 0xf0) >> 4;
}

static inline uint8_t lower_nibble(uint8_t c)
{
	TRACE();
	return c & 0x0f;
}

/* TODO: A more generalized vesion of this would be nice in libos. */
static inline unsigned int htoi(uint8_t *hex_string)
{
	uint8_t *p = hex_string;
	unsigned int s = 0;
	TRACE();
	while (*p) {
		s <<= 4;
		s += hdtoi(*p++);
	}
	return s;
}

static inline token_t tokenize(uint8_t *lexeme)
{
	int i;

	for (i = 0;
	     i < sizeof (lexeme_token_pairs)/sizeof (lexeme_token_pairs[0]);
	     i++)
		if (strncmp((char *) lexeme, lexeme_token_pairs[i].lexeme,
			strlen(lexeme_token_pairs[i].lexeme)) == 0)
			return lexeme_token_pairs[i].token;

	return unknown_command;
}

static inline uint8_t hex(uint8_t c)
{
	return hexit [c];
}
/* Aux ends. */


/* Package Receive and Transmit.
 */

static inline int pkt_len(pkt_t *pkt)
{
	TRACE();
	return pkt->cur - pkt->buf;
}

static inline int pkt_space(pkt_t *pkt)
{
	TRACE();
	return pkt->len;
}

static inline void pkt_write_byte(pkt_t *pkt, uint8_t c)
{
	TRACE();
	if (pkt->cur < (pkt->buf + pkt->len))
		*(pkt->cur) = c;
	return;
}

static inline void pkt_update_cur(pkt_t *pkt)
{
	TRACE();
	if (pkt->cur < (pkt->buf + pkt->len))
		pkt->cur++;
	return;
}

static inline void pkt_write_byte_update_cur(pkt_t *pkt, uint8_t c)
{
	TRACE();
	if (pkt->cur < (pkt->buf + pkt->len))
		*(pkt->cur)++ = c;
	return;
}

static inline void pkt_cat_string(pkt_t *pkt, char *s)
{
	char *p;
	TRACE();
	p = s;
	while (*p) {
		pkt_write_byte_update_cur(pkt, *p++);
	}
	pkt_write_byte(pkt, 0);
	return;
}

static inline uint8_t pkt_read_byte(pkt_t *pkt, unsigned int i)
{
	TRACE();
	if (0 <= i && i < (pkt->cur - pkt->buf))
		return pkt->buf[i];
	else
		return 0;
}

static inline int pkt_full(pkt_t *pkt)
{
	TRACE();
	return (pkt->cur == (pkt->buf + pkt->len));
}

static inline void pkt_reset(pkt_t *pkt)
{
	TRACE();
	memset(pkt->buf, 0, pkt->len);
	pkt->cur = pkt->buf;
	return;
}

static inline uint8_t *content(pkt_t *pkt)
{
	TRACE();
	return pkt->buf;
}

static inline void ack(void)
{
	TRACE();
	put_debug_char(ACK);
	return;
}

static inline void nak(void)
{
	TRACE();
	put_debug_char(NAK);
	return;
}

static inline int got_ack(void)
{
	uint8_t c;
	TRACE();
	c = (get_debug_char() == ACK);
	TRACE("Got: %c", c ? ACK : NAK);
	return c;
}

/* Create well-defined content into cmd buffer.
 */
static void receive_command(pkt_t *cmd)
{
	uint8_t c, i;
	uint8_t ccs[3] = {};

	TRACE();
	do {
		while ((c = get_debug_char()) != '$') {
			TRACE("Skipping '%c'. (Expecting '$').", c);
		}

		while ((c = get_debug_char()) != '#') {
			pkt_write_byte(cmd, c);
			pkt_update_cur(cmd);
		}
		pkt_write_byte(cmd, '\0');
		pkt_update_cur(cmd);
		TRACE("Received command: %s", content(cmd));

		for (i = 0; i < 2; i++) {
			ccs[i] = get_debug_char();
		}
		TRACE("Got checksum: %s", ccs);

		if (!(c = (checksum(content(cmd)) == htoi(ccs)))) {
			TRACE("Checksum mismatch."
                               " Sending NAK and getting next packet.");
			nak();
		}
	} while (!c);

	TRACE("Checksum match; sending ACK");
	ack();
	return;
}

/* Create well-defined content into rsp buffer.
 */
static void transmit_response(pkt_t *rsp)
{
	int i;
	uint8_t c;
	TRACE();
	do {
		TRACE("Transmitting response: %s\n", content(rsp));
		put_debug_char('$');
		/* TODO: Why does this not work?
		 * qprintf(byte_channel_handle->tx, "%s", content(rsp));
		 */
		i = 0;
		while (i < pkt_len(rsp) && (c = pkt_read_byte(rsp, i))) {
			put_debug_char(c);
			i++;
		}
		put_debug_char('#');
		put_debug_char(hex(upper_nibble(checksum(content(rsp)))));
		put_debug_char(hex(lower_nibble(checksum(content(rsp)))));
	} while (!got_ack());
	pkt_reset(rsp);
	return;
}

static inline void pkt_hex_copy(pkt_t *pkt, uint8_t *p, unsigned int length)
{
	unsigned int i;

	TRACE();
	for (i = 0; i < length; i++, p++) {
		pkt_write_byte_update_cur(pkt, hex(upper_nibble(p[i])));
		pkt_write_byte_update_cur(pkt, hex(lower_nibble(p[i])));
	}
	pkt_write_byte(pkt, '\0');
	return;
}

/* PRT ends. */

/* RSP Engine.
 */
void gdb_stub_event_handler(trapframe_t *trap_frame)
{
	uint8_t *q;
	TRACE("In RSP Engine, main loop.");

	stub_start: {
		TRACE("At stub_start");
		/* Deregister call back. */
		byte_channel_handle->rx->data_avail = NULL;
		enable_critint();
	}

	/* TODO: Send back the target state, first thing. */
	while (1) {

		pkt_reset(cmd);
		receive_command(cmd);
		switch(tokenize(content(cmd))) {

		case continue_execution:
			/* disable_critint() on a continue.
			 * reregister call backs
			 * check byte channel if something arrived
			 * in the meanwhile and if something _did_
			 * arrive, loop back to the beginning
			 * of the gdb_stub_event_handler where you
			 * disable interrupts.
			 */
				TRACE("Got 'c' packet.");
			disable_critint();
			byte_channel_handle->rx->data_avail = rx;
			if (queue_empty(byte_channel_handle->rx)) {
				TRACE("Returning to guest.");
				return;
			}
			else {
				TRACE("Back to stub_start.");
				goto stub_start;
			}
			break;

		case read_registers:
			TRACE("Got 'g' packet.");
			pkt_hex_copy(rsp, (uint8_t *) (trap_frame -> gpregs),
			        32 * sizeof (register_t));
			break;

		case set_thread:
			TRACE("Got packet: 'H'");
			q = content(cmd);
			q++;
			switch(*q) {

				case 'c':
					TRACE("Got 'c' for step and"
					       " continue operations.");
					break;

				case 'g':
					TRACE("Got 'g' for other operations.");
					break;

				default:
					TRACE("Unhandled case '%c' in 'H' packet.", *q);
					break;
			}
			q++;
			if (strcmp((char *) q, "0") == 0) {
				TRACE("Pick up any thread.");
			} else if (strcmp((char *) q, "-1") == 0) {
				TRACE("All the threads.");
			} else {
				TRACE("Thread: %s", q);
			}
			pkt_cat_string(rsp, "OK");
			break;

		case read_memory:
			TRACE("Returning '0' as memory reads are not yet supported.");
			pkt_cat_string(rsp, "E0");
			break;

		case read_register:
			TRACE("Got 'p' packet.");
			q = content(cmd);
			q++;
			if (strncmp("40", (char *) q, 2) == 0) {
				TRACE("Returning $pc.");
				// trapframe_t
				pkt_hex_copy(rsp, (uint8_t *) &(trap_frame -> srr0),
				       sizeof (register_t));
				TRACE();
			}
			else {
				TRACE("Returning '0' as registers other than $pc "
				       "is not yet supported.");
				pkt_hex_copy(rsp, (uint8_t *) "0", 1);
			}
			break;

		case return_current_thread_id:
			TRACE("Got 'qC' packet.");
			/* For now let pid == 0 */
			pkt_cat_string(rsp, "QC0");
			break;

		case get_section_offsets:
			TRACE("Got 'qOffsets' packet.");
			pkt_cat_string(rsp, "Text=0;Data=0;Bss=0");
			break;

		case supported_features:
			TRACE("Got 'qSupported' packet.");
			pkt_cat_string(rsp, "PacketSize=" BUFMAX_HEX ";");
			pkt_cat_string(rsp, "qXfer:auxv:read-;"
			            "qXfer:features:read-;"
			            "qXfer:libraries:read-;"
			            "qXfer:memory-map:read-;"
			            "qXfer:spu:read-;"
			            "qXfer:spu:write-;"
			            "QPassSignals-;");
			break;

		case reason_halted:
			/* Corresp. to TARGET_SIGNAL_INT = 2
			 * in include/gdb/signals.h
			 * TODO: Also, signal == 2 For now.
			 */
			TRACE("Got stop reply packet: '?'");
			pkt_cat_string(rsp, "S");
			pkt_hex_copy(rsp, (uint8_t *) "2", 1);
			break;

		default:
			TRACE("Unhandled RSP directive: %s,"
			       " transmitting response: '%s'\n",
		               content(cmd), content(rsp));
			break;
		}
		transmit_response(rsp);
	}
}
/* RSP ends. */

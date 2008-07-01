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
#include <guestmemio.h>
#include <devtree.h>
#include <gdb-stub.h>
#include <greg.h>
#include <e500mc-data.h>

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
	/* TODO: Use ptr_from_node() (But, this call does not work).
	 * fsl_ep = ptr_from_node(fdt, node_offset, "fsl,endpoint");
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
	/* TODO: Use ptr_from_node() */
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

/* TODO: Where do we call this? */
static inline int bufsize_sanity_check(void)
{
	return BUFMAX >= NUMREGBYTES * 2;
}

#define ACK '+'
#define NAK '-'

static uint8_t hexit[] =
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
	read_register,
	read_registers,
	write_register,
	write_registers,
	set_thread,
	read_memory,
	write_memory,
	return_current_thread_id,
	get_section_offsets,
	supported_features,
	qxfer,
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
	{ "p", read_register },
	{ "g", read_registers },
	{ "P", write_register },
	{ "G", write_registers },
	{ "H", set_thread },
	{ "m", read_memory },
	{ "M", write_memory },
	{ "qC", return_current_thread_id },
	{ "qOffsets", get_section_offsets },
	{ "qSupported", supported_features },
	{ "qXfer", qxfer },
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
static inline void pkt_write_hex_byte_update_cur(pkt_t *pkt, uint8_t c);
static inline void pkt_cat_string(pkt_t *pkt, char *s);
static inline void pkt_cat_stringn(pkt_t *pkt, uint8_t *s, unsigned int n);
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
static inline void stringize_reg_value(uint8_t *value, uint64_t reg_value, unsigned int byte_length);
static inline int read_reg(trapframe_t *trap_frame, uint8_t *value, unsigned int reg_num);
static inline int write_reg(trapframe_t *trap_frame, uint8_t *value, unsigned int reg_num);
static inline uint8_t *scan_till(uint8_t *q, char c);
static inline unsigned int scan_num(uint8_t **buffer, char c);
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
	if (c == '#' || c == '$' || c == '}' || c == '*') {
		if (pkt->cur < (pkt->buf + pkt->len))
			*(pkt->cur) = '}';
		pkt_update_cur(pkt);
		if (pkt->cur < (pkt->buf + pkt->len))
			*(pkt->cur) = c ^ 0x20;
	} else if (pkt->cur < (pkt->buf + pkt->len))
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
	pkt_write_byte(pkt, c);
	pkt_update_cur(pkt);
	return;
}

static inline void pkt_write_hex_byte_update_cur(pkt_t *pkt, uint8_t c)
{
	TRACE();
	pkt_write_byte_update_cur(rsp, hex(upper_nibble(c)));
	pkt_write_byte_update_cur(rsp, hex(lower_nibble(c)));
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

static inline void pkt_cat_stringn(pkt_t *pkt, uint8_t *s, unsigned int n)
{
        uint8_t *p;
        TRACE();
        p = s;
        while (*p && (p - s) < n) {
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
	uint8_t c;
	uint32_t i = 0;
	uint8_t ccs[3] = {};

	TRACE();
	do {
		while ((c = get_debug_char()) != '$') {
			TRACE("Skipping '%c'. (Expecting '$').", c);
		}

		TRACE("Begin Looping:");
		while ((c = get_debug_char()) != '#') {
			TRACE("(Looping) Iteration: %d, got character: %c", i++, c);
			pkt_write_byte_update_cur(cmd, c);
		}
		TRACE("Done Looping...");
		pkt_write_byte_update_cur(cmd, '\0');
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
		pkt_write_hex_byte_update_cur(pkt, p[i]);
	}
	pkt_write_byte(pkt, '\0');
	return;
}

/* PRT ends. */

/* RSP Engine.
 */

static inline void stringize_reg_value(uint8_t *value, uint64_t reg_value,
                                       unsigned int byte_length)
{
	int i, offset;
	offset = (byte_length == 4 ? 4 : 0);
	for (i = 0; i < byte_length; i++) {
		value[2*i] = hex(upper_nibble((int)
				(((uint8_t *) &reg_value)[offset + i])));
		value[2*i+1] = hex(lower_nibble((int)
				(((uint8_t *) &reg_value)[offset + i])));
	}
	value[2*byte_length] = '\0';
}

static inline int read_reg(trapframe_t *trap_frame, uint8_t *value,
                           unsigned int reg_num)
{
	unsigned int byte_length = 0;
	register_t reg_value = 0xdeadbeef;
	uint64_t fp_reg_value = 0xdeadbeef;
	unsigned int e500mc_reg_num;
	uint8_t c;
	e500mc_reg_num = e500mc_reg_table[reg_num].e500mc_num;
	byte_length = e500mc_reg_table[reg_num].bitsize/8;
	switch(e500mc_reg_table[reg_num].cat) {
	case reg_cat_spr:
		c = read_gspr(trap_frame, e500mc_reg_num, &reg_value);
		break;
	case reg_cat_gpr:
		c = read_ggpr(trap_frame, e500mc_reg_num, &reg_value);
		break;
	case reg_cat_msr:
		c = read_gmsr(trap_frame, &reg_value);
		break;
	case reg_cat_cr:
		c = read_gcr(trap_frame, &reg_value);
		break;
	case reg_cat_unk:
	case reg_cat_pmr:
		c = 1;
		break;
	case reg_cat_fpr:
		c = read_gfpr(trap_frame, e500mc_reg_num, &fp_reg_value);
		stringize_reg_value(value, fp_reg_value, byte_length);
		return c;
	default: TRACE("Illegal register category.");
		c = 1;
		break;
	}
	stringize_reg_value(value, reg_value, byte_length);
	return c;
}

static inline int write_reg(trapframe_t *trap_frame, uint8_t *value,
                            unsigned int reg_num)
{
	uint64_t reg_value;
	unsigned int e500mc_reg_num;
	uint8_t c;
	e500mc_reg_num = e500mc_reg_table[reg_num].e500mc_num;
	reg_value = htoi((uint8_t *) value);
	switch(e500mc_reg_table[reg_num].cat) {
	case reg_cat_spr:
		c = write_gspr(trap_frame, e500mc_reg_num, reg_value);
		break;
	case reg_cat_gpr:
		c = write_ggpr(trap_frame, e500mc_reg_num, reg_value);
		break;
	case reg_cat_fpr:
		c = write_gfpr(trap_frame, e500mc_reg_num, &reg_value);
		break;
	case reg_cat_msr:
		c = write_gmsr(trap_frame, reg_value);
		break;
	case reg_cat_cr:
		c = write_gcr(trap_frame, reg_value);
		break;
	case reg_cat_unk:
	case reg_cat_pmr:
		c = 1;
		break;
	default: TRACE("Illegal register category.");
		c = 1;
		break;
	}
	return c;
}

/* Scan forward until a 'c' is found or a '\0' is hit,
 * whichever happens earlier.
 */
static inline uint8_t *scan_till(uint8_t *q, char c)
{
	while (*q && *q != c)
		q++;
	return q;
}

/* Extract out number in char-buffer, assuming that the
 * number is punctuated by character c. The position
 * in the buffer is advanced to point to the last
 * character read (i.e. c or '\0' - whichever occurs
 * earlier).
 */
static inline unsigned int scan_num(uint8_t **buffer, char c)
{
	uint8_t t, *sav_pos;
	unsigned int n;

	/* Important assumption: The data of interest
	 * starts /after/ the first character.
	 */
	sav_pos = ++(*buffer);
	*buffer = scan_till(*buffer, c);
	t = **buffer;
	**buffer = '\0';
	n = htoi(sav_pos);
	**buffer = t;
	return n;
}

#define BREAK_IF_END(cur_pos)               \
	if (!*(cur_pos)) {                  \
		pkt_cat_string(rsp, "E00"); \
		break;                      \
	}

void gdb_stub_event_handler(trapframe_t *trap_frame)
{
	uint8_t *cur_pos, *sav_pos, *data;
	unsigned int offset, length;
	uint8_t err_flag = 0;
	uint32_t *addr;
	uint8_t value[17];
	unsigned int reg_num, i;
	char *td; /* td: target description */

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

		case read_register:
			TRACE("Got 'p' packet.");
			cur_pos = content(cmd);
			reg_num = scan_num(&cur_pos, '\0');
			read_reg(trap_frame, value, reg_num);
			TRACE("Register: %d, read-value: %s", reg_num, value);
			pkt_cat_string(rsp, (char *)value);
			break;

		case read_registers:
			TRACE("Got 'g' packet.");
			for (reg_num = 0; reg_num < NUMREGS; reg_num++) {
				read_reg(trap_frame, value, reg_num);
				TRACE("Register: %d, read-value: %s", reg_num, value);
				pkt_cat_string(rsp, (char *)value);
			}
			break;

		case write_register:
			TRACE("Got 'P' packet.");
			err_flag = 0;
			cur_pos = content(cmd);
			reg_num = scan_num(&cur_pos, '=');
			BREAK_IF_END(cur_pos);
			data = ++cur_pos;
			TRACE("Register: %d, write-value: %s (decimal: %d)", reg_num, data, htoi(data));
			err_flag = write_reg(trap_frame, data, reg_num);
			pkt_cat_string(rsp, err_flag == 0 ? "OK" : "E00");
			break;

		case write_registers:
			TRACE("Got 'G' packet.");
			cur_pos = content(cmd);
			data = ++cur_pos;
			BREAK_IF_END(data);
			TRACE("Got data (register values): %s", data);
			err_flag = 0;
			for (reg_num = 0; reg_num < NUMREGS; reg_num++) {
				int32_t byte_length = 0;
				byte_length = e500mc_reg_table[reg_num].bitsize / 8;
				TRACE("PRE strncpy");
				strncpy((char *)value, (const char *)data, 2 * byte_length);
				TRACE("POST strncpy");
				value[2 * byte_length] = '\0';
				err_flag = write_reg(trap_frame, value, reg_num);
				data += 2 * byte_length;
			}
			TRACE("Done writing registers, sending: OK");
			pkt_cat_string(rsp, "OK");
			break;

		case set_thread:
			TRACE("Got packet: 'H'");
			cur_pos = content(cmd);
			cur_pos++;
			switch(*cur_pos) {

			case 'c':
				TRACE("Got 'c' for step and"
				       " continue operations.");
				break;

			case 'g':
				TRACE("Got 'g' for other operations.");
				break;

			default:
				TRACE("Unhandled case '%c' in 'H' packet.", *cur_pos);
				break;
			}
			cur_pos++;
			if (strcmp((char *) cur_pos, "0") == 0) {
				TRACE("Pick up any thread.");
			} else if (strcmp((char *) cur_pos, "-1") == 0) {
				TRACE("All the threads.");
			} else {
				TRACE("Thread: %s", cur_pos);
			}
			pkt_cat_string(rsp, "OK");
			break;

		case read_memory:
			TRACE("Got 'm' packet.");
			cur_pos = content(cmd);
			addr = (uint32_t *)scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, '\0');
			TRACE("addr: 0x%p, length: %d\n", addr, length);
			guestmem_set_data(trap_frame);
			for (i = 0; i < length; i++) {
				guestmem_in8((uint8_t *) addr + i, ((uint8_t *) value));
				if (value[0] == '\0')
					TRACE("guestmem_in8 set value[0] to: 0");
				else
					TRACE("guestmem_in8 set value[0] to: %c", value[0]);
				pkt_write_hex_byte_update_cur(rsp, value[0]);
				TRACE("byte address: 0x%p, upper nibble value: %c",
				      (uint8_t *) addr + i, hex(upper_nibble(value[0])));
				TRACE("byte address: 0x%p, lower nibble value: %c",
				      (uint8_t *) addr + i, hex(lower_nibble(value[0])));
			}
			break;

		case write_memory:
			TRACE("Got 'M' packet.");
			cur_pos = content(cmd);
			addr = (uint32_t *) scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, ':');
			BREAK_IF_END(cur_pos);
			cur_pos++;
			data = cur_pos;
			TRACE("addr: 0x%p, length: %d, data: %s\n", addr, length, data);
			err_flag = 0;
			for (i = 0; i < length; i++) {
				value[0] = data[2*i];
				value[1] = data[2*i+1];
				value[2] = 0;
				if (guestmem_out8((uint8_t *) addr + i, htoi((uint8_t *) value)) != 0)
					err_flag = 1;
			}
			pkt_cat_string(rsp, err_flag == 0 ? "OK" : "E00");
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
			                    "qXfer:features:read+;"
			                    "qXfer:libraries:read-;"
			                    "qXfer:memory-map:read-;"
			                    "qXfer:spu:read-;"
			                    "qXfer:spu:write-;"
			                    "QPassSignals-;");
			break;

		case qxfer:
			TRACE("Got 'qXfer' packet.");
			cur_pos = content(cmd);
			cur_pos += 5;
			if (strncmp(":features:read:", (char *) cur_pos, 15) == 0) {
				TRACE ("Got :features:read:");
				TRACE ("Retreiving annex.");
				cur_pos += 15;
				sav_pos = cur_pos;
				cur_pos = scan_till(cur_pos, ':');
				BREAK_IF_END(cur_pos);
				td = "";
				if (strncmp("target.xml", (char *) sav_pos, cur_pos - sav_pos) == 0) {
					TRACE("Using "
					      "td: "
					      "e500mc_description");
					td = e500mc_description;
				}
				else if (strncmp ("power-core.xml", (char *)sav_pos, cur_pos - sav_pos) == 0) {
					TRACE("Using "
					      "td: "
					      "power_core_description");
					td = power_core_description;
				}
				else if (strncmp ("power-fpu.xml", (char *)sav_pos, cur_pos - sav_pos) == 0) {
					TRACE("Using "
					      "td: "
					      "power_fpu_description");
					td = power_fpu_description;
				}
				BREAK_IF_END(td);
				offset = scan_num (&cur_pos, ',');
				BREAK_IF_END(cur_pos);
				length = scan_num (&cur_pos, '\0');
				TRACE ("td-len: %d, offset: %d, length: %d", strlen (td), offset, length);
				if (offset < strlen (td)) {
					pkt_cat_string(rsp, "m");
					pkt_cat_stringn(rsp, (uint8_t *)(td + offset), length);
				} else {
					pkt_cat_string(rsp, "l");
				}
			}
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

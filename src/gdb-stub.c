/** @file
 *
 * HV GDB Stub.
 *
 * - GDB Remote Serial Protocol support for GDB (version 6.7 or later).
 *   (Commands supported: g, G, m, M, c, s, ?, z0, Z0, qXfer:features:read).
 * - GDB Target Description Format support per Appendix F of GDB 6.7
 *   reference manual:
 *     Debugging with GDB, The GNU Source-Level Debugger,
 *     Ninth Edition, for gdb version 6.7.50.20080119
 *     Free Software Foundation.
 *
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 * Author: Anmol P. Paralkar <anmol@freescale.com>
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
#include <libos/trapframe.h>
#include <events.h>
#include <byte_chan.h>
#include <guestmemio.h>
#include <devtree.h>
#include <gdb-stub.h>
#include <greg.h>
#include <e500mc-data.h>

#define TRACE(fmt, info...) \
	printlog(LOGTYPE_GDB_STUB, \
	LOGLEVEL_VERBOSE, \
	"%s@[%s, %d]: " fmt "\n", \
	__func__, __FILE__, __LINE__, ## info)

static dt_node_t *find_stub_config_node(char *compatible);
static void rx(queue_t *q);

#define CHECK_MEM(p) \
	if (!p) { \
		printlog(LOGTYPE_GDB_STUB, LOGLEVEL_ERROR, \
		         "Out of memory?\n"); \
		return; \
	}

static stub_ops_t stub_ops = {
        .debug_int_handler = gdb_stub_process_trap,
        .wait_at_start_hook = gdb_wait_at_start,
};

void gdb_stub_init(void)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub;

	TRACE();

	stub = malloc(sizeof(gdb_stub_core_context_t));
	CHECK_MEM(stub);
	memset(stub, 0, sizeof(gdb_stub_core_context_t));

	/* find the config node for this stub & vcpu */
	stub->node = find_stub_config_node("gdb-stub");
	if (!stub->node) {
		printlog(LOGTYPE_GDB_STUB, LOGLEVEL_ERROR,
		         "%s: missing debug stub node\n",
		         __func__);
		return;
	}

	init_byte_channel(stub->node);
	/* note: the byte-channel handle is returned in node->bch */
	if (!stub->node->bch) {
		printlog(LOGTYPE_GDB_STUB, LOGLEVEL_ERROR,
		         "%s: gdb stub byte chan allocation failed\n",
		         __func__);
		return;
	}

	stub->cbuf = malloc(BUFMAX*sizeof(uint8_t));
	CHECK_MEM(stub->cbuf);
	memset(stub->cbuf, 0, BUFMAX*sizeof(uint8_t));
	stub->command.buf = stub->cbuf;
	stub->command.len = BUFMAX;
	stub->command.cur = stub->cbuf;
	stub->cmd = &stub->command;
	stub->rbuf = malloc(BUFMAX*sizeof(uint8_t));
	CHECK_MEM (stub->rbuf);
	memset(stub->rbuf, 0, BUFMAX*sizeof(uint8_t));
	stub->response.buf = stub->rbuf;
	stub->response.len = BUFMAX;
	stub->response.cur = stub->rbuf;
	stub->rsp = &stub->response;
	stub->breakpoint_table = malloc(MAX_BREAKPOINT_COUNT*sizeof(breakpoint_t));
	CHECK_MEM (stub->breakpoint_table);
	gcpu->debug_stub_data = stub;

	if (!gcpu->guest->stub_ops)
		gcpu->guest->stub_ops = &stub_ops;

	/* register the callbacks */
	stub->node->bch->rx->data_avail = rx;
	stub->node->bch->rx->consumer = gcpu;

	return; 
}

static int find_stub_by_vcpu(dt_node_t *node, void *arg)
{
	dt_node_t **ret = arg;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "debug-cpus", 0);
	if (!prop) {
		printlog(LOGTYPE_GDB_STUB, LOGLEVEL_ERROR, "Missing debug-cpus at %s\n", node->name);
		return 0;
	}

	/* FIXME: Handle multiple vcpus */
	int vcpu_num = *(const uint32_t *)prop->data;
	TRACE("VCPU_NUM = %d\n", vcpu_num);
	TRACE("GCPU_NUM = %d\n", get_gcpu()->gcpu_num);
	if (vcpu_num == get_gcpu()->gcpu_num) {
		*ret = node;
		return 1;
	}

	return 0;
}

/** Find in the device-tree, the byte-channel that this instance of the gdb-stub
 * is connected to.
 * @return pointer to the byte-channel with the gdb-stub as it's end-point.
 */
static dt_node_t *find_stub_config_node(char *compatible)
{
	gcpu_t *gcpu = get_gcpu();
	dt_node_t *node = NULL;
	int rc;

	rc = dt_for_each_compatible(gcpu->guest->partition, compatible,
	                       find_stub_by_vcpu, &node);

	return node;
}

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

enum event_type event_type;

/** Callback for RX interrupt.
 *
 */
static void rx(queue_t *q)
{
	TRACE();
	/* Record the event type and setgevent GEV_GDB on the current CPU. */
	event_type = received_data;
	setgevent((gcpu_t*)q->consumer, GEV_GDB);
	/* TODO: What if there were some other event in progress?  It'll be
	 * lost. The GDB event handler should check whether the input queue
	 * is empty, rather than rely on a "received data" flag.
	 */
}

/* get_debug_char() and put_debug_char() are our
 * means of communicating with the outside world,
 * one character at a time.
 */

static uint8_t get_debug_char(gdb_stub_core_context_t *stub)
{
	ssize_t byte_count = 0;
	uint8_t ch;
	TRACE();
	do {
		byte_count = byte_chan_receive(stub->node->bch, &ch, 1);
		/* internal error if byte_count > len */
	} while (byte_count <= 0);
	return ch;
}

static void put_debug_char(gdb_stub_core_context_t *stub, uint8_t c)
{
	size_t len = 1;
	uint8_t buf[len];
	buf[0] = c;
	TRACE();
	byte_chan_send(stub->node->bch, buf, len);
}

/* TODO: Where do we call this? */
static inline int bufsize_sanity_check(void)
{
	return BUFMAX >= NUMREGBYTES * 2;
}

#define ACK '+'
#define NAK '-'

static const uint8_t hexit[] =
{
	'0', '1', '2', '3',
	'4', '5', '6', '7',
	'8', '9', 'a', 'b',
	'c', 'd', 'e', 'f',
};

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
	insert_breakpoint,
	remove_breakpoint,
	single_step,
	return_current_thread_id,
	get_section_offsets,
	supported_features,
	qxfer,
	detach,
} token_t;

typedef struct lexeme_token_pair
{
	char *lexeme;
	token_t token;
} lexeme_token_pair_t;

/* Note: lexeme_token_pairs and enum token_t have to be kept in synch as the token
 *       values are used to index the array.
 */
static const lexeme_token_pair_t lexeme_token_pairs[] =
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
	{ "Z", insert_breakpoint },
	{ "z", remove_breakpoint },
	{ "s", single_step },
	{ "qC", return_current_thread_id },
	{ "qOffsets", get_section_offsets },
	{ "qSupported", supported_features },
	{ "qXfer", qxfer },
	{ "D", detach },
};

/* Aux */
static inline uint8_t checksum(uint8_t *p);
static inline uint8_t hdtoi(uint8_t hexit);
static inline uint8_t upper_nibble(uint8_t c);
static inline uint8_t lower_nibble(uint8_t c);
static inline uint32_t htoi(uint8_t *hex_string);
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
static inline void pkt_cat_stringn(pkt_t *pkt, uint8_t *s, uint32_t n);
static inline uint8_t pkt_read_byte(pkt_t *pkt, uint32_t i);
static inline int pkt_full(pkt_t *pkt);
static inline void pkt_reset(pkt_t *pkt);
static inline uint8_t *content(pkt_t *pkt);
static void put_debug_char(gdb_stub_core_context_t *stub, uint8_t c);
static inline void ack(gdb_stub_core_context_t *stub);
static inline void nak(gdb_stub_core_context_t *stub);
static inline int got_ack(gdb_stub_core_context_t *stub);
static void receive_command(trapframe_t *trap_frame, gdb_stub_core_context_t *stub);
static void transmit_response(gdb_stub_core_context_t *stub);
static void transmit_stop_reply_pkt_T(trapframe_t *trap_frame, gdb_stub_core_context_t *stub);
static inline void pkt_hex_copy(pkt_t *pkt, uint8_t *p, uint32_t length);

/* RSP */

static inline void stringize_reg_value(uint8_t *value, uint64_t reg_value, uint32_t byte_length);
static inline int read_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num);
static inline int write_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num);
static inline uint8_t *scan_till(uint8_t *q, char c);
static inline uint32_t scan_num(uint8_t **buffer, char c);
static inline uint32_t sign_extend(int32_t n, uint32_t sign_bit_position);
static inline uint32_t *next_insn_addr(trapframe_t *trap_frame);
static inline void dump_breakpoint_table(breakpoint_t *breakpoint_table);
static inline void clear_all_breakpoints(breakpoint_t *breakpoint_table);
static inline breakpoint_t *locate_breakpoint(breakpoint_t *breakpoint_table, uint32_t *addr);
static inline breakpoint_t *set_breakpoint(breakpoint_t *breakpoint_table, trapframe_t *trap_frame, uint32_t *addr, breakpoint_type_t type);
static inline void delete_breakpoint(breakpoint_t *breakpoint_table, breakpoint_t *breakpoint);
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
static inline uint32_t htoi(uint8_t *hex_string)
{
	uint8_t *p = hex_string;
	uint32_t s = 0;
	TRACE();
	if (*p == 0 && *(p + 1) && (*(p + 1) == 'x' || *(p + 1) == 'X'))
		p += 2;
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
	return hexit[c];
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
	TRACE("Upper nibble hexed: '%c'", hex(upper_nibble(c)));
	TRACE("Lower nibble hexed: '%c'", hex(lower_nibble(c)));
	pkt_write_byte_update_cur(pkt, hex(upper_nibble(c)));
	pkt_write_byte_update_cur(pkt, hex(lower_nibble(c)));
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

static inline void pkt_cat_stringn(pkt_t *pkt, uint8_t *s, uint32_t n)
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

static inline uint8_t pkt_read_byte(pkt_t *pkt, uint32_t i)
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

static inline void ack(gdb_stub_core_context_t *stub)
{
	TRACE();
	put_debug_char(stub, ACK);
	return;
}

static inline void nak(gdb_stub_core_context_t *stub)
{
	TRACE();
	put_debug_char(stub, NAK);
	return;
}

static inline int got_ack(gdb_stub_core_context_t *stub)
{
	uint8_t c;
	TRACE();
	c = (get_debug_char(stub) == ACK);
	TRACE("Got: %c", c ? ACK : NAK);
	return c;
}

#define CTRL_C 0x03
/* Create well-defined content into cmd buffer.
 */
static void receive_command(trapframe_t *trap_frame, gdb_stub_core_context_t *stub)
{
	uint8_t c;
	uint32_t i = 0;
	uint8_t ccs[3] = {};

	TRACE();
	do {
		while ((c = get_debug_char(stub)) != '$') {
			if (c == CTRL_C) {
				printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG, "gdb: Got CTRL-C.\n");
				transmit_stop_reply_pkt_T(trap_frame, stub);
			} else
				TRACE("Skipping '%c'. (Expecting '$').", c);
		}

		TRACE("Begin Looping:");
		while ((c = get_debug_char(stub)) != '#') {
			TRACE("(Looping) Iteration: %d, got character: '%c'", i++, c);
			pkt_write_byte_update_cur(stub->cmd, c);
		}
		TRACE("Done Looping.");
		pkt_write_byte_update_cur(stub->cmd, '\0');
		TRACE("Received command: %s", content(stub->cmd));

		for (i = 0; i < 2; i++) {
			ccs[i] = get_debug_char(stub);
		}
		TRACE("Got checksum: %s", ccs);

		if (!(c = (checksum(content(stub->cmd)) == htoi(ccs)))) {
			TRACE("Checksum mismatch. Sending NAK and getting next packet.");
			nak(stub);
		}
	} while (!c);

	TRACE("Checksum match; sending ACK");
	ack(stub);
	return;
}

/* Create well-defined content into rsp buffer.
 */
static void transmit_response(gdb_stub_core_context_t *stub)
{
	int i;
	uint8_t c;
	TRACE();
	do {
		TRACE("Transmitting response: %s", content(stub->rsp));
		put_debug_char(stub, '$');
		/* TODO: Why does this not work?
		 * qprintf(node->bch->tx, "%s", content(stub->rsp));
		 */
		i = 0;
		while (i < pkt_len(stub->rsp) && (c = pkt_read_byte(stub->rsp, i))) {
			put_debug_char(stub, c);
			i++;
		}
		put_debug_char(stub, '#');
		TRACE("Got checksum: %d", checksum(content(stub->rsp)));
		put_debug_char(stub, hex(upper_nibble(checksum(content(stub->rsp)))));
		put_debug_char(stub, hex(lower_nibble(checksum(content(stub->rsp)))));
	} while (!got_ack(stub));
	pkt_reset(stub->rsp);
	return;
}

static inline void pkt_hex_copy(pkt_t *pkt, uint8_t *p, uint32_t length)
{
	uint32_t i;

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

static inline void stringize_reg_value(uint8_t *value, uint64_t reg_value, uint32_t byte_length)
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

static inline int read_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num)
{
	uint32_t byte_length = 0;
	register_t reg_value = 0xdeadbeef;
	uint64_t fp_reg_value = 0xdeadbeef;
	uint32_t e500mc_reg_num;
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
	case reg_cat_pc:
		c = read_gpc(trap_frame, &reg_value);
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

static inline int write_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num)
{
	uint64_t reg_value;
	uint32_t e500mc_reg_num;
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
static inline uint32_t scan_num(uint8_t **buffer, char c)
{
	uint8_t t, *sav_pos;
	uint32_t n;

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

#define BREAK_IF_END(cur_pos) \
	if (!*(cur_pos)) { \
		pkt_cat_string(stub->rsp, "E"); \
		pkt_write_hex_byte_update_cur(stub->rsp, 0); \
		break; \
	}

static inline uint32_t sign_extend(int32_t n, uint32_t sign_bit_position)
{
	int32_t result = n;
	int32_t sign_bit, bitpos;
	TRACE("n: 0x%x", n);
	sign_bit = (n >> sign_bit_position) & 1;
	TRACE("Sign bit: %d", sign_bit);
	for (bitpos = sign_bit_position; bitpos < 32; bitpos++) {
		result = (sign_bit << bitpos) | result;
	}
	TRACE("Returning: 0x%x", result);
	return result;
}

static inline uint32_t *next_insn_addr(trapframe_t *trap_frame)
{
	uint32_t insn = 0x0;
	uint32_t opcd, xo, aa, li, bd;
	typedef enum branch_type { b, bc, bclr, bcctr } branch_type_t;
	branch_type_t branch_type = b;
	uint32_t *nia = 0x0;
	uint32_t *pc = (uint32_t *)trap_frame->srr0;
	TRACE("INSN_ADDR: 0x%p", pc);
	/* First check if this is a branch instruction at all.
	 * If not, return rightaway.
	 */
	guestmem_set_data(trap_frame);
	if (guestmem_in32(pc, &insn) != 0) {
		TRACE("Could not read insn, NIA: 0x%d", 0);
		return 0x0;
	}
	TRACE("Decoded insn: 0x%x at addr: 0x%p", insn, pc);
	/* We decode the following instructions:
	 * Branch: b (ba, bl, bla)
	 * Branch Conditional: bc (bca, bcl, bcla)
	 * Branch Conditional to Link Register: bclr (bclrl)
	 * Branch Conditional to Count Register: bcctr (bcctrl)
	 */
	TRACE("Checking if this is a branch insn.");
	opcd = (insn >> 26) & 0x3f;
	if (opcd != 16 /* Not bc? */
	    && opcd != 18 /* Not b? */
	    && (opcd != 19 /* Not bclr?, bcctr? (nor mcrf) */)) {
		TRACE ("Not a branch insn.");
		nia = pc + 1;
		TRACE("NIA: 0x%p", nia);
		return nia;
	}
	/* Do not let an mcrf slip through. */
	xo = (insn >> 1) & 0x3ff;
	if (xo == 0) /* mcrf XL-FORM */ {
		TRACE ("Not a branch insn.");
		nia = pc + 1;
		TRACE("NIA: 0x%p", nia);
		return nia;
	}
	TRACE("0x%x: Is a branch insn.", insn);
	switch (opcd) {
		case 18: branch_type = b; break;
		case 16: branch_type = bc; break;
		case 19: branch_type = (xo == 16) ? bclr : bcctr /* xo == 528 */;
	}
	TRACE("opcd: %d, xo: %d", opcd, xo);
	aa = (insn >> 1) & 0x1;
	TRACE("aa: 0x%x", aa);
	switch (branch_type) {
		case b:
			li = (insn >> 2) & 0xffffff;
			TRACE("li: 0x%x", li);
			if (aa == 0) {
				nia = (uint32_t *) ((sign_extend(li << 2, 25) >> 2) + pc);
			} else /* (aa == 1) */ {
				nia = (uint32_t *) sign_extend(li << 2, 25);
			}
			break;
		case bc:
			bd = (insn >> 2) & 0x3fff;
			if (aa == 0) {
				nia = (uint32_t *) ((sign_extend(bd << 2, 15) >> 2) + pc);
			} else {
				nia = (uint32_t *) sign_extend(bd << 2, 15);
			}
			break;
		case bclr:
			nia = (uint32_t *) (trap_frame->lr);
			break;
		case bcctr:
			nia = (uint32_t *) (trap_frame->ctr);
			break;
	}
	TRACE("nia: 0x%p", nia);
	return nia;
}

/* tw 12,r2,r2: 0x7d821008 */
const uint32_t trap_insn = 0x7d821008;

static inline void dump_breakpoint_table(breakpoint_t *breakpoint_table)
{
	uint32_t index, flag = 0;
	TRACE("Breakpoint table:");
	for (index = 0; index < MAX_BREAKPOINT_COUNT; index++) {
		if (breakpoint_table[index].taken) {
			flag = 1;
			TRACE("index: %d", index);
			TRACE("\taddr: 0x%p", breakpoint_table[index].addr);
			TRACE("\torig_insn: %x", breakpoint_table[index].orig_insn);
			TRACE("\tcount: %d", breakpoint_table[index].count);
			TRACE("\ttaken: %d", breakpoint_table[index].taken);
			if (breakpoint_table[index].associated)
				TRACE("\tassocated internal breakpoint: 0x%p",
				         breakpoint_table[index].associated->addr);
			else
				TRACE("\tno assocated internal breakpoint");
			TRACE("\ttype: %s", breakpoint_table[index].type == external ?
			      "external" : "internal");
		}
	}
	if (!flag)
		TRACE("\tEmpty.");
}

static inline void clear_all_breakpoints(breakpoint_t *breakpoint_table)
{
	uint32_t index;
	breakpoint_t *breakpoint = NULL;
	if (breakpoint_table == NULL)
		return;
	for (index = 0; index < MAX_BREAKPOINT_COUNT; index++) {
		if (breakpoint_table[index].taken) {
			breakpoint = &breakpoint_table[index];
			TRACE("Forcefully clearing breakpoint at addr: 0x%p.", breakpoint->addr);
			TRACE("Writing back original insn: 0x%x at address: 0x%p", breakpoint->orig_insn, breakpoint->addr);
			guestmem_out32(breakpoint->addr, breakpoint->orig_insn);
			memset(breakpoint, 0, sizeof(breakpoint_table[0]));
		}
	}
}

static inline breakpoint_t *locate_breakpoint(breakpoint_t *breakpoint_table, uint32_t *addr)
{
	uint32_t index;
	TRACE("Checking if there is an entry in the breakpoint table for address: 0x%p.", addr);
	for (index = 0; index < MAX_BREAKPOINT_COUNT; index++) {
		if (breakpoint_table[index].addr == addr) {
			TRACE("Found entry for address: 0x%p at index: %d in breakpoint table.", addr, index);
			return &breakpoint_table[index];
		}
	}
	TRACE("No entry for address: 0x%p in breakpoint table.", addr);
	return NULL;
}

static inline breakpoint_t *set_breakpoint(breakpoint_t *breakpoint_table, trapframe_t *trap_frame, uint32_t *addr, breakpoint_type_t type)
{
	uint32_t index;
	uint32_t status = 1;
	uint32_t orig_insn = 0x0;
	breakpoint_t *breakpoint = NULL;

	TRACE();

	/* First check if there already is a breakpoint at addr and return rightaway if so. */
	breakpoint = locate_breakpoint(breakpoint_table, addr);
	if (breakpoint) {
		if (breakpoint->type == type) {
			breakpoint->count++;
			TRACE("Previous breakpoint (addr: 0x%p) found in breakpoint table. Count is now: %d",
			       addr, breakpoint->count);
			return breakpoint;
		}
		if (breakpoint->type == external && type == internal) {
			TRACE("Previous external breakpoint (addr: 0x%p) found in breakpoint table. "
			      "Cannot override with internal breakpoint.", addr);
			return NULL;
		}
		if (breakpoint->type == internal && type == external) {
			TRACE("Resetting internal breakpoint at addr: 0x%p.", addr);
			TRACE("Creating new external breakpoint at addr: 0x%p", addr);
			/* Internal breakpoints will always have a ref count of 1 so
			 * the following delete should completely blow it off from
			 * the breakpoint table.
			 */
			delete_breakpoint(breakpoint_table, breakpoint);
		}
	}

	/* Now, check if we have any free slot and use the first one you find. */
	for (index = 0; index < MAX_BREAKPOINT_COUNT; index++) {
		if (!breakpoint_table[index].taken) {
			/* Read the original instruction. */
			guestmem_set_insn(trap_frame);
			status = guestmem_in32(addr, &orig_insn);
			if (status != 0) {
				TRACE("Could not read instruction at addr: 0x%p", addr);
				break;
			}
			TRACE("Read orig_insn: 0x%x at addr: 0x%p", orig_insn, addr);
			TRACE("Write trap instruction.");
			status = guestmem_out32(addr, trap_insn);
			/* TODO: Need to flush [ID]-caches here. */
			if (status != 0) {
				TRACE("Could not set breakpoint at addr: 0x%p", addr);
				break;
			}
			TRACE("Set breakpoint at addr: 0x%p", addr);
			/* Fill up entry with this breakpoint's info. */
			breakpoint_table[index].addr = addr;
			breakpoint_table[index].orig_insn = orig_insn;
			breakpoint_table[index].count = 1;
			breakpoint_table[index].taken = 1;
			breakpoint_table[index].associated = NULL;
			breakpoint_table[index].type = type;
			status = 0;
			TRACE("Entered %s breakpoint (addr: 0x%p) into breakpoint table.",
			       type == external ? "external" : "internal", addr);
			/* Quit iterating. */
			break;
		}
	}
	TRACE("Returning breakpoint table address: 0x%p for breakpoint at: 0x%p",
	       &breakpoint_table[index], addr);
	return (status == 0) ? &breakpoint_table[index] : NULL;
}

static inline void delete_breakpoint(breakpoint_t *breakpoint_table, breakpoint_t *breakpoint)
{
	TRACE();
	if (!breakpoint) {
		TRACE("Got NULL address - you cannot have a breakpoint there!");
		return;
	}
	breakpoint->count--;
	if (!breakpoint->count) {
		TRACE("Count: 0; Deleteing breakpoint at addr: 0x%p. Clearing entry.", breakpoint->addr);
		TRACE("Writing back original insn: 0x%x at address: 0x%p",
		       breakpoint->orig_insn, breakpoint->addr);
		guestmem_out32(breakpoint->addr, breakpoint->orig_insn);
		memset(breakpoint, 0, sizeof(breakpoint_table[0]));
		TRACE("Cleared all data in entry with memset.");
	}
}

static void transmit_stop_reply_pkt_T(trapframe_t *trap_frame, gdb_stub_core_context_t *stub)
{
	uint8_t value[32];
	TRACE("Sending T rsp");
	pkt_cat_string(stub->rsp, "T");
	pkt_write_hex_byte_update_cur(stub->rsp, 5);
	pkt_write_hex_byte_update_cur(stub->rsp, 1); /* r1 */
	pkt_cat_string(stub->rsp, ":");
	read_reg(trap_frame, value, 1);
	pkt_cat_string(stub->rsp, (char *)value);
	pkt_cat_string(stub->rsp, ";");
	pkt_write_hex_byte_update_cur(stub->rsp, 64); /* pc */
	pkt_cat_string(stub->rsp, ":");
	read_reg(trap_frame, value, 64);
	pkt_cat_string(stub->rsp, (char *)value);
	pkt_cat_string(stub->rsp, ";");
	/* FIXME: This is a HACK. Is this the right fix? */
	stub->node->bch->rx->data_avail = NULL;
	transmit_response(stub);
}

int gdb_stub_process_trap(trapframe_t *trap_frame)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub = gcpu->debug_stub_data;
	register_t nip_reg_value = 0;
	breakpoint_t *breakpoint = NULL;
	breakpoint_type_t bptype = breakpoint->type;

	printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG, "gdb: debug exception\n");

	/* register_t esr_reg_value = 0;
	 * esr_reg_value = mfspr(SPR_ESR);
	 * TRACE("esr_ptr check: %s", esr_reg_value == ESR_PTR ? "success" : "fail");
	 * TRACE("esr_reg_value: 0x%08lx", esr_reg_value); */
	nip_reg_value = trap_frame->srr0;
	/* TRACE("nip_reg_value: 0x%08lx", nip_reg_value);
	 *
	 * TRACE("Sanity check registers dump:");
	 * dump_regs(trap_frame); */
	/* Check if this is a GDB stub registered breakpoint.
	 * If so, invoke gdb_stub_event_handler();
	 * Else return with code 1.
	 */
	breakpoint = locate_breakpoint(stub->breakpoint_table, (uint32_t *)nip_reg_value);
	if (!breakpoint) {
		TRACE("Breakpoint not set by stub, returning (1).");
		return 1;
	}

	/* need to save this so after bp is cleared we still know */
	bptype = breakpoint->type;

	if (bptype == internal || bptype == internal_1st_inst) {
		TRACE("We've hit an internal (set by stub) breakpoint at addr: 0x%p", (uint32_t *)nip_reg_value);
		guestmem_set_data(trap_frame);
		if (breakpoint->associated) {
			TRACE("Replace the original instruction back at the associated internal "
			      "breakpoint: 0x%p", breakpoint->associated->addr);
			guestmem_out32(breakpoint->associated->addr,
			               breakpoint->associated->orig_insn);
			TRACE("Delete the associated internal breakpoint: 0x%p",
			       breakpoint->associated->addr);
			delete_breakpoint(stub->breakpoint_table, breakpoint->associated);
		}
		TRACE("Replace the original instruction back at the internal breakpoint: 0x%p", breakpoint->addr);
		guestmem_out32(breakpoint->addr, breakpoint->orig_insn);
		delete_breakpoint(stub->breakpoint_table, breakpoint);
		breakpoint = NULL;
	} else {
		TRACE("We've hit an external (user set) breakpoint at addr: 0x%p", (uint32_t *)nip_reg_value);
	}

	if (bptype != internal_1st_inst)
		transmit_stop_reply_pkt_T(trap_frame, stub);

	if (bptype == internal_1st_inst)
		printlog(LOGTYPE_GDB_STUB, LOGLEVEL_NORMAL,
			"gdb: waiting for host debugger...\n");

	TRACE("Calling: gdb_stub_event_handler().");
	gdb_stub_event_handler(trap_frame);
	TRACE("Returning from gdb_stub_process_trap()");
	return 0;
}

void gdb_stub_event_handler(trapframe_t *trap_frame)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub = gcpu->debug_stub_data;
	uint8_t *cur_pos, *sav_pos, *data;
	uint32_t offset, length;
	uint8_t err_flag = 0;
	uint32_t *addr = NULL;
	uint32_t *nia = NULL, *pc = NULL;
	uint8_t aux, value[17];
	uint32_t reg_num, i;
	uint8_t breakpoint_type;
	breakpoint_t *breakpoint = NULL;
	breakpoint_t *breakpoint_nia = NULL;
	breakpoint_t *breakpoint_incrpc = NULL;
	const char *td; /* td: target description */
	printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
		"gdb: in RSP Engine, main loop.\n");
	stub_start: {
		TRACE("At stub_start.");
		/* Deregister call back. */
		stub->node->bch->rx->data_avail = NULL;
		enable_critint();
	}
	while (1) {
		TRACE("In main loop.");
		dump_breakpoint_table(stub->breakpoint_table);
		pkt_reset(stub->cmd);
		receive_command(trap_frame, stub);
		switch(tokenize(content(stub->cmd))) {

		case continue_execution:
			/* o disable_critint() on a continue.
			 * o Reregister call backs.
			 * o Check byte channel if something arrived in the
			 *   meanwhile and if something _did_ arrive, loop
			 *   back to the beginning of the gdb_stub_event_handler
			 *   where interrupts are disabled.
			 */
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"GOT 'c' PACKET.\n");
			return_to_guest:
				disable_critint();
				stub->node->bch->rx->data_avail = rx;
				if (queue_empty(stub->node->bch->rx)) {
					TRACE("Returning to guest.");
					return;
				} else {
					TRACE("Back to stub_start.");
					goto stub_start;
				}
			break;

		case read_register:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'p' packet.\n");
			cur_pos = content(stub->cmd);
			reg_num = scan_num(&cur_pos, '\0');
			read_reg(trap_frame, value, reg_num);
			TRACE("Register: %d, read-value: %s", reg_num, value);
			pkt_cat_string(stub->rsp, (char *)value);
			break;

		case read_registers:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'g' packet.\n");
			for (reg_num = 0; reg_num < NUMREGS; reg_num++) {
				read_reg(trap_frame, value, reg_num);
				TRACE("Register: %d, read-value: %s", reg_num, value);
				pkt_cat_string(stub->rsp, (char *)value);
			}
			break;

		case write_register:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'P' packet.\n");
			err_flag = 0;
			cur_pos = content(stub->cmd);
			reg_num = scan_num(&cur_pos, '=');
			BREAK_IF_END(cur_pos);
			data = ++cur_pos;
			TRACE("Register: %d, write-value: %s (decimal: %d)",
			       reg_num, data, htoi(data));
			err_flag = write_reg(trap_frame, data, reg_num);
			if (err_flag == 0) {
				pkt_cat_string(stub->rsp, "OK");
			} else {
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		case write_registers:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'G' packet.\n");
			cur_pos = content(stub->cmd);
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
			pkt_cat_string(stub->rsp, "OK");
			break;

		case set_thread:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got packet: 'H'\n");
			cur_pos = content(stub->cmd);
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
			pkt_cat_string(stub->rsp, "OK");
			break;

		case read_memory:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'm' packet.\n");
			cur_pos = content(stub->cmd);
			addr = (uint32_t *)scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, '\0');
			TRACE("Read memory at addr: 0x%p, length: %d", addr, length);
			guestmem_set_data(trap_frame);
			for (i = 0; i < length; i++) {
				if (guestmem_in8((uint8_t *) addr + i, ((uint8_t *) (&value[0]))) != 0)
					value[0] = 0;
				TRACE("value[0]: %c", value[0]);
				pkt_write_hex_byte_update_cur(stub->rsp, value[0]);
				TRACE("byte address: 0x%p, upper nibble value: %c",
				       (uint8_t *)addr + i, hex(upper_nibble(value[0])));
				TRACE("byte address: 0x%p, lower nibble value: %c",
				       (uint8_t *)addr + i, hex(lower_nibble(value[0])));
			}
			break;

		case write_memory:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'M' packet.\n");
			cur_pos = content(stub->cmd);
			addr = (uint32_t *) scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, ':');
			BREAK_IF_END(cur_pos);
			cur_pos++;
			data = cur_pos;
			TRACE("addr: 0x%p, length: %d, data: %s", addr, length, data);
			err_flag = 0;
			for (i = 0; i < length; i++) {
				value[0] = data[2*i];
				value[1] = data[2*i+1];
				value[2] = 0;
				if (guestmem_out8((uint8_t *)addr + i, htoi((uint8_t *)value)) != 0)
					err_flag = 1;
			}
			if (err_flag == 0) {
				pkt_cat_string(stub->rsp, "OK");
			} else {
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		case insert_breakpoint:
		case remove_breakpoint:
			cur_pos = content(stub->cmd);
			aux = *cur_pos;
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got '%c' packet.\n", *cur_pos);
			err_flag = 0;
			breakpoint_type = scan_num(&cur_pos, ',');
			if (breakpoint_type != 0) {
				TRACE("We only support memory breakpoints.");
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
				break;
			}
			BREAK_IF_END(cur_pos);
			addr = (uint32_t *) scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, ':');
			if (length != 4) {
				TRACE("Breakpoint must be of length: 4.");
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
				break;
			}
			TRACE("Breakpoint type: %d, addr: 0x%p, length: %d",
			       breakpoint_type, addr, length);
			TRACE("aux: %c", aux);
			if (aux == 'Z') {
				TRACE ("Invoking set_breakpoint.");
				breakpoint = set_breakpoint(stub->breakpoint_table, trap_frame, addr, external);
				if (breakpoint)
					breakpoint->associated = NULL;
				err_flag = breakpoint ? 0 : 1;
			} else { /* 'z' */
				TRACE("Invoking locate_breakpoint.");
				breakpoint = locate_breakpoint(stub->breakpoint_table, addr);
				if (breakpoint) {
					TRACE("Located breakpoint placed at: 0x%p", addr);
					delete_breakpoint(stub->breakpoint_table, breakpoint);
				} else /* Vow! Spurious address! */ {
					TRACE("No breakpoint placed at: 0x%p", addr);
					err_flag = 1;
				}
			}
			if (err_flag == 0) {
				pkt_cat_string(stub->rsp, "OK");
			} else {
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		case single_step:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 's' packet.\n");
			cur_pos = content(stub->cmd);
			pc = (uint32_t *)trap_frame->srr0;
			/* If there is no address to resume at; we resume at pc. */
			addr = cur_pos[1] ? (uint32_t *)scan_num(&cur_pos, '\0') : pc;
			nia = next_insn_addr(trap_frame);
			TRACE("Single stepping to insn at nia: 0x%p or pc + 4: 0x%p", nia, pc + 1);
			TRACE("Setting internal breakpoint at nia: 0x%p.", nia);
			breakpoint_nia = set_breakpoint(stub->breakpoint_table, trap_frame, nia, internal);
			TRACE("%s setting internal breakpoint at nia: 0x%p.",
			       breakpoint_nia ? "Done" : "Not", nia);
			/* Optimize: If nia == pc + 1 - create only one internal breakpoint. */
			if (nia != pc + 1) {
				TRACE("Setting internal breakpoint at pc + 4: 0x%p.", pc + 1);
				breakpoint_incrpc = set_breakpoint(stub->breakpoint_table, trap_frame, pc + 1, internal);
				TRACE("%s setting internal breakpoint at pc + 4: 0x%p.",
				       breakpoint_incrpc ? "Done" : "Not", pc + 1);
			} else {
				breakpoint_incrpc = NULL;
			}
			if (breakpoint_nia) {
				breakpoint_nia->associated = breakpoint_incrpc;
			}
			if (breakpoint_incrpc) {
				breakpoint_incrpc->associated = breakpoint_nia;
			}
			TRACE("Stepping after hitting internal (set by stub) breakpoint.");
			TRACE("Done with s-packet, will return to guest.");
			goto return_to_guest;
			return;
			break;

		case return_current_thread_id:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'qC' packet.\n");
			/* For now let pid == 0 */
			pkt_cat_string(stub->rsp, "QC0");
			break;

		case get_section_offsets:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'qOffsets' packet.\n");
			pkt_cat_string(stub->rsp, "Text=0;Data=0;Bss=0");
			break;

		case supported_features:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'qSupported' packet.\n");
			pkt_cat_string(stub->rsp, "PacketSize=" BUFMAX_HEX ";");
			pkt_cat_string(stub->rsp, "qXfer:auxv:read-;"
			                          "qXfer:features:read+;"
			                          "qXfer:libraries:read-;"
			                          "qXfer:memory-map:read-;"
			                          "qXfer:spu:read-;"
			                          "qXfer:spu:write-;"
			                          "QPassSignals-;");
			break;

		case qxfer:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got 'qXfer' packet.\n");
			cur_pos = content(stub->cmd);
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
					TRACE("Using td: e500mc_description");
					td = e500mc_description;
				}
				else if (strncmp ("power-core.xml", (char *)sav_pos, cur_pos - sav_pos) == 0) {
					TRACE("Using td: power_core_description");
					td = power_core_description;
				}
				else if (strncmp ("power-fpu.xml", (char *)sav_pos, cur_pos - sav_pos) == 0) {
					TRACE("Using td: power_fpu_description");
					td = power_fpu_description;
				}
				BREAK_IF_END(td);
				offset = scan_num (&cur_pos, ',');
				BREAK_IF_END(cur_pos);
				length = scan_num (&cur_pos, '\0');
				TRACE ("td-len: %d, offset: %d, length: %d", strlen (td), offset, length);
				if (offset < strlen (td)) {
					pkt_cat_string(stub->rsp, "m");
					pkt_cat_stringn(stub->rsp, (uint8_t *)(td + offset), length);
				} else {
					pkt_cat_string(stub->rsp, "l");
				}
			}
			break;

		case reason_halted:
			/* Corresp. to TARGET_SIGNAL_INT = 2
			 * in include/gdb/signals.h
			 * TODO: Also, signal == 2 For now.
			 */
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Got stop reply packet: '?'\n");
			pkt_cat_string(stub->rsp, "S");
			pkt_write_hex_byte_update_cur(stub->rsp, 2);
			break;

		case detach:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG, "Got 'D' packet.\n");
			dump_breakpoint_table(stub->breakpoint_table);
			clear_all_breakpoints(stub->breakpoint_table);
			dump_breakpoint_table(stub->breakpoint_table);
			TRACE("Returning to guest on a detach.");
			pkt_cat_string(stub->rsp, "OK");
			transmit_response(stub);
			goto return_to_guest;
			break;

		default:
			printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
				"Unhandled RSP directive: %s, transmitting response: '%s'\n",
			       content(stub->cmd), content(stub->rsp));
			break;
		}
		transmit_response(stub);
	}
}

void gdb_wait_at_start(uint32_t entry, register_t msr)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub = gcpu->debug_stub_data;
	trapframe_t trap_frame;
	breakpoint_t *breakpoint;

	printlog(LOGTYPE_GDB_STUB, LOGLEVEL_DEBUG,
		"setting breakpoint on 1st instruction\n");

	/* contruct enough of a trap frame so that set_breakpoint() can
	 * do its thing.
	 */
	trap_frame.srr1 = msr;

	/* set up EPLC and EPSC so we can set our breakpoint */
	mtspr(SPR_EPLC, EPC_EGS | (gcpu->guest->lpid << EPC_ELPID_SHIFT));
	mtspr(SPR_EPSC, EPC_EGS | (gcpu->guest->lpid << EPC_ELPID_SHIFT));

	breakpoint = set_breakpoint(stub->breakpoint_table, &trap_frame, (uint32_t *)entry, internal_1st_inst);
}



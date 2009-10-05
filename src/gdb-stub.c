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
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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
#include <malloc.h>

#include <libos/trapframe.h>
#include <libos/core-regs.h>
#include <libos/trap_booke.h>

#include <events.h>
#include <byte_chan.h>
#include <guestmemio.h>
#include <devtree.h>
#include <gdb-stub.h>
#include <greg.h>
#include <e500mc-data.h>
#include <debug-stub.h>

#define TRACE(fmt, info...) \
	printlog(LOGTYPE_DEBUG_STUB, \
	LOGLEVEL_VERBOSE, \
	"%s@[%s, %d]: " fmt "\n", \
	__func__, __FILE__, __LINE__, ## info)
#define DEBUG(fmt, info...) \
	printlog(LOGTYPE_DEBUG_STUB, \
	LOGLEVEL_DEBUG, \
	"%s@[%s, %d]: " fmt "\n", \
	__func__, __FILE__, __LINE__, ## info)

void gdb_stub_start(trapframe_t *trap_frame);
void gdb_stub_stop(void);
void gdb_stub_init(void);

static void rx_gevent_handler(trapframe_t *trap_frame);
static int handle_debug_event(trapframe_t *trap_frame);
#ifdef USE_DEBUG_INTERRUPT
static int debug_exception(trapframe_t *trap_frame);
static void debug_gevent_handler(trapframe_t *trap_frame);
#endif
static void rx(queue_t *q);
static breakpoint_t *set_breakpoint(breakpoint_t *breakpoint_table, trapframe_t *trap_frame, uint32_t *addr, breakpoint_type_t type);
static void gdb_stub_main_loop(trapframe_t *trap_frame, int event_type);

#define CHECK_MEM(p) \
	if (!p) { \
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR, \
		         "Out of memory?\n"); \
		return; \
	}

#define GDB_GEVENT 0
#define GDB_DEBUG_EXCEPTION 1

static stub_ops_t attr_debug_stub stub_ops = {
        .compatible = "gdb-stub",
        .vcpu_init = gdb_stub_init,
        .vcpu_start = gdb_stub_start,
        .vcpu_stop = gdb_stub_stop,
#ifdef USE_DEBUG_INTERRUPT
        .debug_interrupt =  debug_exception,
#else
        .debug_interrupt =  handle_debug_event,
#endif
};

void gdb_stub_init(void)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub;

	DEBUG();

	stub = malloc(sizeof(gdb_stub_core_context_t));
	CHECK_MEM(stub);
	memset(stub, 0, sizeof(gdb_stub_core_context_t));

	stub->node = gcpu->dbgstub_cfg;

	stub->gev_rx = register_gevent(&rx_gevent_handler);
	if (stub->gev_rx < 0) {
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR,
		         "%s: gevent registration failed\n",
		         __func__);
		return;
	}

#ifdef USE_DEBUG_INTERRUPT
	stub->gev_dbg = register_gevent(&debug_gevent_handler);
	if (stub->gev_dbg < 0) {
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR,
		         "%s: gevent registration failed\n",
		         __func__);
		return;
	}
#endif

	init_byte_channel(stub->node);
	/* note: the byte-channel handle is returned in node->bch */
	if (!stub->node->bch) {
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR,
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

	gcpu->dbgstub_cpu_data = stub;

	return; 
}

void gdb_stub_start(trapframe_t *trap_frame)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub;
	breakpoint_t *breakpoint;

	DEBUG();

	if (!gcpu->dbgstub_cpu_data) {
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR,
		         "%s: stub vcpu start failed\n",
		         __func__);
		return;
	}
	stub = gcpu->dbgstub_cpu_data;

	if (!stub->node->bch) {
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR,
		         "%s: byte-channel handle is NULL\n",
		         __func__);
		return;
	}

	/* register the callbacks */
	stub->node->bch->rx->consumer = gcpu;
	smp_lwsync();
	stub->node->bch->rx->data_avail = rx;

	if (dt_get_prop(gcpu->dbgstub_cfg, "gdb-wait-at-start", 0)) {
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
			"setting breakpoint on 1st instruction\n");

		/* set up EPLC and EPSC so we can set our breakpoint */
		mtspr(SPR_EPLC, EPC_EGS | (gcpu->guest->lpid << EPC_ELPID_SHIFT));
		mtspr(SPR_EPSC, EPC_EGS | (gcpu->guest->lpid << EPC_ELPID_SHIFT));

		breakpoint = set_breakpoint(stub->breakpoint_table, trap_frame, (uint32_t *)trap_frame->srr0, internal_1st_inst);
		if (!breakpoint) {
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR,
			"setting entry breakpoint failed\n");
		}
	}

#ifdef USE_DEBUG_INTERRUPT
	/* enable hardware debug mode */
	mtspr(SPR_DBCR0, DBCR0_IDM | DBCR0_TRAP);
	trap_frame->srr1 |= MSR_DE;
#endif

	return;
}

void gdb_stub_stop(void)
{
	gdb_stub_core_context_t *stub = get_gcpu()->dbgstub_cpu_data;

	/* de-register the callback */
	stub->node->bch->rx->data_avail = NULL;

	return; 
}

/** Callback for RX interrupt.
 *
 */
static void rx(queue_t *q)
{
 	gcpu_t *gcpu = (gcpu_t*)q->consumer;
 	gdb_stub_core_context_t *stub = gcpu->dbgstub_cpu_data;
	DEBUG();

 	setgevent(gcpu, stub->gev_rx);
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
	TRACE();
	byte_chan_send(stub->node->bch, &c, 1);
}

/* TODO: Where do we call this? */
static int bufsize_sanity_check(void)
{
	return BUFMAX >= NUMREGBYTES * 2;
}

#define ACK '+'
#define NAK '-'

static const uint8_t hexit_table[] =
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
	compute_crc32,
	return_current_thread_id,
	get_section_offsets,
	supported_features,
	qxfer,
	detach,
	raw_write,
	monitor,
	kill,
} token_t;

typedef struct lexeme_token_pair
{
	const char *lexeme;
	token_t token;
} lexeme_token_pair_t;

/* Note: 0. lexeme_token_pairs and enum token_t have to be kept in synch as the token
 *          values are used to index the array.
 *       1. Place { "foobar", foobar } /before/ { "foob", foob } /before/ { "foo", foo }
 *          in the table below so that tokenize() does not end-up returning the token
 *          for a prefix of the intended lexeme (in the case that a prefix is also a valid
 *          lexeme). e.g. qCRC, qC.
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
	{ "qCRC", compute_crc32 },
	{ "qC", return_current_thread_id },
	{ "qOffsets", get_section_offsets },
	{ "qSupported", supported_features },
	{ "qXfer", qxfer },
	{ "D", detach },
	{ "X", raw_write },
	{ "qRcmd", monitor },
	{ "k", kill },
};

typedef enum brkpt
{
	memory_breakpoint,
	hardware_breakpoint,
	write_watchpoint,
	read_watchpoint,
	access_watchpoint,
} brkpt_t;

/* Aux */
static uint8_t checksum(uint8_t *p, uint32_t length);
static uint8_t hdtoi(uint8_t hexit);
static uint8_t upper_nibble(uint8_t c);
static uint8_t lower_nibble(uint8_t c);
static uint64_t htoi(uint8_t *hex_string);
static uint8_t *htos(uint8_t *hex_string);
static token_t tokenize(uint8_t *lexeme);
static uint8_t hex(uint8_t c);
static uint32_t crc32(trapframe_t *trap_frame, uint32_t *addr, uint32_t length, uint8_t *err_flag);

/* PRT */
static int bufsize_sanity_check(void);
static int pkt_len(pkt_t *pkt);
static int pkt_space(pkt_t *pkt);
static void pkt_write_byte(pkt_t *pkt, uint8_t c);
static void pkt_write_byte_verbatim(pkt_t *pkt, uint8_t c);
static void pkt_update_cur(pkt_t *pkt);
static void pkt_write_byte_update_cur(pkt_t *pkt, uint8_t c);
static void pkt_write_byte_verbatim_update_cur(pkt_t *pkt, uint8_t c);
static void pkt_write_hex_byte_update_cur(pkt_t *pkt, uint8_t c);
static void pkt_cat_string(pkt_t *pkt, const char *s);
static void pkt_cat_stringn(pkt_t *pkt, const uint8_t *s, uint32_t n);
static uint8_t pkt_read_byte(pkt_t *pkt, uint32_t i);
static int pkt_full(pkt_t *pkt);
static void pkt_reset(pkt_t *pkt);
static uint8_t *content(pkt_t *pkt);
static void put_debug_char(gdb_stub_core_context_t *stub, uint8_t c);
static void ack(gdb_stub_core_context_t *stub);
static void nak(gdb_stub_core_context_t *stub);
static int got_ack(gdb_stub_core_context_t *stub);
static void receive_command(trapframe_t *trap_frame, gdb_stub_core_context_t *stub);
static void transmit_response(gdb_stub_core_context_t *stub);
static void pkt_write_dac_state(trapframe_t *trap_frame, gdb_stub_core_context_t *stub);
static void transmit_stop_reply_pkt_T(trapframe_t *trap_frame, gdb_stub_core_context_t *stub);
static void pkt_hex_copy(pkt_t *pkt, uint8_t *p, uint32_t length);

/* RSP */

static void stringize_reg_value(uint8_t *value, uint64_t reg_value, uint32_t byte_length);
static int read_cpu_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num);
static int write_cpu_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num);
static uint8_t *scan_till(uint8_t *q, char c);
static uint64_t scan_num(uint8_t **buffer, char c);
static uint32_t sign_extend(int32_t n, uint32_t sign_bit_position);
#ifndef USE_DEBUG_INTERRUPT
static uint32_t *next_insn_addr(trapframe_t *trap_frame);
#endif
static void dump_breakpoint_table(breakpoint_t *breakpoint_table);
static void clear_all_breakpoints(breakpoint_t *breakpoint_table);
static breakpoint_t *locate_breakpoint(breakpoint_t *breakpoint_table, uint32_t *addr);
static void delete_breakpoint(breakpoint_t *breakpoint_table, breakpoint_t *breakpoint);

/* Auxiliary routines required by the RSP Engine.
 */

static uint8_t checksum(uint8_t *p, uint32_t length)
{
	uint8_t s = 0, *q = p;
	TRACE("length: %d", length);
	while (q - p < length) {
		TRACE("p[%d]: 0x%02x\n", q - p, *q);
		s += *q++;
	}
	TRACE("checksum: %d", s);

	return s; /* Automatically, mod 256. */
}

static uint8_t hdtoi(uint8_t hexit)
{
	TRACE();
	if ('0' <= hexit && hexit <= '9')
		return hexit - '0';

	if ('a' <= hexit && hexit <= 'f')
		return hexit - 'a' + 10;

	if ('A' <= hexit && hexit <= 'F')
		return hexit - 'A' + 10;

	DEBUG("Got other than [0-9a-f].");
	return 0;
}

static uint8_t upper_nibble(uint8_t c)
{
	TRACE();
	return (c & 0xf0) >> 4;
}

static uint8_t lower_nibble(uint8_t c)
{
	TRACE();
	return c & 0x0f;
}

/* TODO: A more generalized vesion of this would be nice in libos. */
static uint64_t htoi(uint8_t *hex_string)
{
	uint8_t *p = hex_string;
	uint64_t s = 0;
	TRACE();
	if (*p == 0 && *(p + 1) && (*(p + 1) == 'x' || *(p + 1) == 'X'))
		p += 2;
	while (*p) {
		s <<= 4;
		s += hdtoi(*p++);
	}
	return s;
}

/* Convert in situ, a GDB RSP hex coded string (i.e. having two hex chars/byte)
 * into it's regular ASCII string.
 * TODO: static inline stoh(uint8_t *hex_string); hex_string should contain
 *       /twice/ as much space for the non trivial content.
 */
static uint8_t *htos(uint8_t *hex_string)
{
	uint8_t a[3] = { 0 };
	uint8_t *p = hex_string;
	uint8_t *q = hex_string;

	while (*p) {
		a[0] = p[0];
		a[1] = p[1];
		*q = htoi(a);
		p += 2;
		q += 1;
	}
	*q = '\0';
	return hex_string;
}

static token_t tokenize(uint8_t *lexeme)
{
	unsigned int i;

	for (i = 0;
	     i < sizeof (lexeme_token_pairs)/sizeof (lexeme_token_pairs[0]);
	     i++)
		if (strncmp((const char *) lexeme, lexeme_token_pairs[i].lexeme,
			strlen(lexeme_token_pairs[i].lexeme)) == 0)
			return lexeme_token_pairs[i].token;

	return unknown_command;
}

static uint8_t hex(uint8_t c)
{
	return hexit_table[c];
}

static uint32_t crc32(trapframe_t *trap_frame, uint32_t *addr, uint32_t length, uint8_t *err_flag)
{
	uint32_t crc32_sum, crc32_pol, crc32_aux, crc32_flag;
	uint8_t value[1];
	uint32_t i, j;
	/* Polynomial:
	 * x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
	 * Representation: 0x04c11db7 (One bit per non-zero coefficient above. With the coefficient of
	 *                 the leading term implied).
	 * Initial value for checksum: 0xffffffff
	 * Important: Must correspond to crc32() in remote.c in the GDB sources.
	 */
	crc32_sum = 0xffffffff;
	crc32_pol = 0x04c11db7;
	guestmem_set_data(trap_frame);
	for (i = 0; i < length; i++) {
		if (guestmem_in8((uint8_t *)addr + i, value) != 0) {
			*err_flag = 1;
			break;
		}
		crc32_flag = 0;
		crc32_aux = (crc32_sum & 0xff000000) ^ (*value << 24);
		for (j = 0; j < 8; j++) {
			if (crc32_aux & 0x80000000)
				crc32_flag = 1;
			crc32_aux <<= 1;
			if (crc32_flag) {
				crc32_aux ^= crc32_pol;
				crc32_flag = 0;
			}
		}
		crc32_sum = (crc32_sum << 8) ^ crc32_aux;
	}
	return crc32_sum;
}

/* Aux ends. */


/* Package Receive and Transmit.
 */

static int pkt_len(pkt_t *pkt)
{
	TRACE();
	return pkt->cur - pkt->buf;
}

static int pkt_space(pkt_t *pkt)
{
	TRACE();
	return pkt->len;
}

static void pkt_write_byte(pkt_t *pkt, uint8_t c)
{
	TRACE();
	if (c == '#' || c == '$' || c == '}' || c == '*') {
		if (pkt->cur < (pkt->buf + pkt->len))
			*(pkt->cur) = '}';
		pkt_update_cur(pkt);
		if (pkt->cur < (pkt->buf + pkt->len))
			*(pkt->cur) = c ^ 0x20;
	} else
		pkt_write_byte_verbatim(pkt, c);
	return;
}

static void pkt_write_byte_verbatim(pkt_t *pkt, uint8_t c)
{
	TRACE();
	if (pkt->cur < (pkt->buf + pkt->len))
		*(pkt->cur) = c;
	return;
}

static void pkt_update_cur(pkt_t *pkt)
{
	TRACE();
	if (pkt->cur < (pkt->buf + pkt->len))
		pkt->cur++;
	return;
}

/* Use pkt_write_byte_verbatim_update_cur() for incoming packets. */
static void pkt_write_byte_verbatim_update_cur(pkt_t *pkt, uint8_t c)
{
	TRACE();
	pkt_write_byte_verbatim(pkt, c);
	pkt_update_cur(pkt);
	return;
}

/* Use pkt_write_byte_update_cur() for outgoing packets, since
 * pkt_write_byte() will escape [#$}*].
 */
static void pkt_write_byte_update_cur(pkt_t *pkt, uint8_t c)
{
	TRACE();
	pkt_write_byte(pkt, c);
	pkt_update_cur(pkt);
	return;
}

static void pkt_write_hex_byte_update_cur(pkt_t *pkt, uint8_t c)
{
	TRACE();
	TRACE("Upper nibble hexed: '%c'", hex(upper_nibble(c)));
	TRACE("Lower nibble hexed: '%c'", hex(lower_nibble(c)));
	pkt_write_byte_update_cur(pkt, hex(upper_nibble(c)));
	pkt_write_byte_update_cur(pkt, hex(lower_nibble(c)));
	return;
}

static void pkt_cat_string(pkt_t *pkt, const char *s)
{
	const char *p;
	TRACE();
	p = s;
	while (*p) {
		pkt_write_byte_update_cur(pkt, *p++);
	}
	pkt_write_byte(pkt, 0);
	return;
}

static void pkt_cat_stringn(pkt_t *pkt, const uint8_t *s, uint32_t n)
{
	const uint8_t *p;
	TRACE();
	p = s;
	while (*p && (uint32_t)(p - s) < n) {
		pkt_write_byte_update_cur(pkt, *p++);
	}
	pkt_write_byte(pkt, 0);
	return;
}

static uint8_t pkt_read_byte(pkt_t *pkt, uint32_t i)
{
	TRACE();
	if (i < (uint32_t)(pkt->cur - pkt->buf))
		return pkt->buf[i];
	else
		return 0;
}

static int pkt_full(pkt_t *pkt)
{
	TRACE();
	return (pkt->cur == (pkt->buf + pkt->len));
}

static void pkt_reset(pkt_t *pkt)
{
	TRACE();
	memset(pkt->buf, 0, pkt->len);
	pkt->cur = pkt->buf;
	return;
}

static uint8_t *content(pkt_t *pkt)
{
	TRACE();
	return pkt->buf;
}

static void ack(gdb_stub_core_context_t *stub)
{
	TRACE();
	put_debug_char(stub, ACK);
	return;
}

static void nak(gdb_stub_core_context_t *stub)
{
	TRACE();
	put_debug_char(stub, NAK);
	return;
}

static int got_ack(gdb_stub_core_context_t *stub)
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

	DEBUG();
	do {
		while ((c = get_debug_char(stub)) != '$') {
			if (c == CTRL_C) {
				printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "gdb: Got CTRL-C.\n");
				transmit_stop_reply_pkt_T(trap_frame, stub);
			} else
				TRACE("Skipping '%c'. (Expecting '$').", c);
		}

		TRACE("Begin Looping:");
		while ((c = get_debug_char(stub)) != '#') {
			TRACE("(Looping) Iteration: %d, got character: 0x%02x", i++, c);
 			pkt_write_byte_verbatim_update_cur(stub->cmd, c);
		}
		TRACE("Done Looping.");
		DEBUG("Received command:");
		for (i = 0; i < pkt_len(stub->cmd); i++)
			DEBUG("cmd[%d] = 0x%02x", i, stub->cmd->buf[i]);

		for (i = 0; i < 2; i++) {
			ccs[i] = get_debug_char(stub);
		}
		TRACE("Got checksum: %s", ccs);

		if (!(c = (checksum(content(stub->cmd), pkt_len(stub->cmd)) == htoi(ccs)))) {
			TRACE("Checksum mismatch. Sending NAK and getting next packet.");
			nak(stub);
			pkt_reset(stub->cmd);
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
		TRACE("Got checksum: %d", checksum(content(stub->rsp), pkt_len(stub->rsp)));
		put_debug_char(stub, hex(upper_nibble(checksum(content(stub->rsp), pkt_len(stub->rsp)))));
		put_debug_char(stub, hex(lower_nibble(checksum(content(stub->rsp), pkt_len(stub->rsp)))));
	} while (!got_ack(stub));
	pkt_reset(stub->rsp);
	return;
}

static void pkt_hex_copy(pkt_t *pkt, uint8_t *p, uint32_t length)
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

static void stringize_reg_value(uint8_t *value, uint64_t reg_value, uint32_t byte_length)
{
	unsigned int i, offset;
	offset = (byte_length == 4 ? 4 : 0);
	for (i = 0; i < byte_length; i++) {
		value[2*i] = hex(upper_nibble((int)
				(((uint8_t *) &reg_value)[offset + i])));
		value[2*i+1] = hex(lower_nibble((int)
				(((uint8_t *) &reg_value)[offset + i])));
	}
	value[2*byte_length] = '\0';
}

static int read_cpu_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num)
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
	case reg_cat_pmr:
		c = read_pmr(trap_frame, e500mc_reg_num, &reg_value);
		break;
	case reg_cat_fpr:
		c = read_gfpr(trap_frame, e500mc_reg_num, &fp_reg_value);
		stringize_reg_value(value, fp_reg_value, byte_length);
		return c;
	case reg_cat_fpscr:
		c = read_fpscr(&fp_reg_value);
		stringize_reg_value(value, fp_reg_value, byte_length);
		return c;
	case reg_cat_unk:
	default: DEBUG("Illegal register category.");
		c = 1;
		break;
	}
	stringize_reg_value(value, reg_value, byte_length);
	return c;
}

static int write_cpu_reg(trapframe_t *trap_frame, uint8_t *value, uint32_t reg_num)
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
	case reg_cat_pc:
		c = write_gpc(trap_frame, reg_value);
		break;
	case reg_cat_fpr:
		c = write_gfpr(trap_frame, e500mc_reg_num, &reg_value);
		break;
	case reg_cat_msr:
		c = write_gmsr(trap_frame, reg_value, 1);
		break;
	case reg_cat_cr:
		c = write_gcr(trap_frame, reg_value);
		break;
	case reg_cat_pmr:
		c = write_pmr(trap_frame, e500mc_reg_num, reg_value);
		break;
	case reg_cat_fpscr:
		c = write_fpscr(reg_value);
		break;
	case reg_cat_unk:
	default: DEBUG("Illegal register category.");
		c = 1;
		break;
	}
	return c;
}

/* Scan forward until a 'c' is found or a '\0' is hit,
 * whichever happens earlier.
 */
static uint8_t *scan_till(uint8_t *q, char c)
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
static uint64_t scan_num(uint8_t **buffer, char c)
{
	uint8_t t, *sav_pos;
	uint64_t n;

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

static uint32_t sign_extend(int32_t n, uint32_t sign_bit_position)
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

#ifndef USE_DEBUG_INTERRUPT
static uint32_t *next_insn_addr(trapframe_t *trap_frame)
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
		DEBUG("Could not read insn, NIA: 0x%d", 0);
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
		DEBUG ("Not a branch insn.");
		nia = pc + 1;
		DEBUG("NIA: 0x%p", nia);
		return nia;
	}
	/* Do not let an mcrf slip through. */
	xo = (insn >> 1) & 0x3ff;
	if (xo == 0) /* mcrf XL-FORM */ {
		DEBUG ("Not a branch insn.");
		nia = pc + 1;
		DEBUG("NIA: 0x%p", nia);
		return nia;
	}
	DEBUG("0x%x: Is a branch insn.", insn);
	switch (opcd) {
		case 18: branch_type = b; break;
		case 16: branch_type = bc; break;
		case 19: branch_type = (xo == 16) ? bclr : bcctr /* xo == 528 */;
	}
	DEBUG("opcd: %d, xo: %d", opcd, xo);
	aa = (insn >> 1) & 0x1;
	DEBUG("aa: 0x%x", aa);
	switch (branch_type) {
		case b:
			li = (insn >> 2) & 0xffffff;
			DEBUG("li: 0x%x", li);
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
	DEBUG("nia: 0x%p", nia);
	return nia;
}
#endif

/* tw 12,r2,r2: 0x7d821008 */
const uint32_t trap_insn = 0x7d821008;

static void dump_breakpoint_table(breakpoint_t *breakpoint_table)
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

static void clear_all_breakpoints(breakpoint_t *breakpoint_table)
{
	uint32_t index;
	breakpoint_t *breakpoint = NULL;
	if (breakpoint_table == NULL)
		return;
	for (index = 0; index < MAX_BREAKPOINT_COUNT; index++) {
		if (breakpoint_table[index].taken) {
			breakpoint = &breakpoint_table[index];
			DEBUG("Forcefully clearing breakpoint at addr: 0x%p.", breakpoint->addr);
			DEBUG("Writing back original insn: 0x%x at address: 0x%p", breakpoint->orig_insn, breakpoint->addr);
			guestmem_out32(breakpoint->addr, breakpoint->orig_insn);
			guestmem_icache_block_sync((char *)breakpoint->addr);
			memset(breakpoint, 0, sizeof(breakpoint_table[0]));
		}
	}
}

static breakpoint_t *locate_breakpoint(breakpoint_t *breakpoint_table, uint32_t *addr)
{
	uint32_t index;
	DEBUG("Checking if there is an entry in the breakpoint table for address: 0x%p.", addr);
	for (index = 0; index < MAX_BREAKPOINT_COUNT; index++) {
		if (breakpoint_table[index].addr == addr) {
			DEBUG("Found entry for address: 0x%p at index: %d in breakpoint table.", addr, index);
			return &breakpoint_table[index];
		}
	}
	DEBUG("No entry for address: 0x%p in breakpoint table.", addr);
	return NULL;
}

static breakpoint_t *set_breakpoint(breakpoint_t *breakpoint_table, trapframe_t *trap_frame, uint32_t *addr, breakpoint_type_t type)
{
	uint32_t index;
	uint32_t status = 1;
	uint32_t orig_insn = 0x0;
	breakpoint_t *breakpoint = NULL;

	DEBUG();

	/* First check if there already is a breakpoint at addr and return rightaway if so. */
	breakpoint = locate_breakpoint(breakpoint_table, addr);
	if (breakpoint) {
		if (breakpoint->type == type) {
			breakpoint->count++;
			DEBUG("Previous breakpoint (addr: 0x%p) found in breakpoint table. Count is now: %d",
			       addr, breakpoint->count);
			return breakpoint;
		}
		if (breakpoint->type == external && type == internal) {
			DEBUG("Previous external breakpoint (addr: 0x%p) found in breakpoint table. "
			      "Cannot override with internal breakpoint.", addr);
			return NULL;
		}
		if (breakpoint->type == internal && type == external) {
			DEBUG("Resetting internal breakpoint at addr: 0x%p.", addr);
			DEBUG("Creating new external breakpoint at addr: 0x%p", addr);
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
				DEBUG("Could not read instruction at addr: 0x%p", addr);
				break;
			}
			DEBUG("Read orig_insn: 0x%x at addr: 0x%p", orig_insn, addr);
			DEBUG("Write trap instruction.");
			status = guestmem_out32(addr, trap_insn);
			status += guestmem_icache_block_sync((char *)addr);
			if (status != 0) {
				DEBUG("Could not set breakpoint at addr: 0x%p", addr);
				break;
			}
			DEBUG("Set breakpoint at addr: 0x%p", addr);
			/* Fill up entry with this breakpoint's info. */
			breakpoint_table[index].addr = addr;
			breakpoint_table[index].orig_insn = orig_insn;
			breakpoint_table[index].count = 1;
			breakpoint_table[index].taken = 1;
			breakpoint_table[index].associated = NULL;
			breakpoint_table[index].type = type;
			status = 0;
			DEBUG("Entered %s breakpoint (addr: 0x%p) into breakpoint table.",
			       type == external ? "external" : "internal", addr);
			/* Quit iterating. */
			break;
		}
	}
	DEBUG("Returning breakpoint table address: 0x%p for breakpoint at: 0x%p",
	       &breakpoint_table[index], addr);
	return (status == 0) ? &breakpoint_table[index] : NULL;
}

static void delete_breakpoint(breakpoint_t *breakpoint_table, breakpoint_t *breakpoint)
{
	DEBUG();
	if (!breakpoint) {
		DEBUG("Got NULL address - you cannot have a breakpoint there!");
		return;
	}
	breakpoint->count--;
	if (!breakpoint->count) {
		DEBUG("Count: 0; Deleteing breakpoint at addr: 0x%p. Clearing entry.", breakpoint->addr);
		DEBUG("Writing back original insn: 0x%x at address: 0x%p",
		       breakpoint->orig_insn, breakpoint->addr);
		guestmem_out32(breakpoint->addr, breakpoint->orig_insn);
		guestmem_icache_block_sync((char *)breakpoint->addr);
		memset(breakpoint, 0, sizeof(breakpoint_table[0]));
		DEBUG("Cleared all data in entry with memset.");
	}
}

static void pkt_write_dac_state(trapframe_t *trap_frame, gdb_stub_core_context_t *stub)
{
	uint8_t value[32], i;
	static const struct dac_info
	{
		int32_t dbsr_rmask;
		int32_t dbsr_wmask;
		uint32_t reg_num;
	} dac_tab[2] = {
		{
			DBSR_DAC1R,
			DBSR_DAC1W,
			77 /* DAC1 */
		},
		{
			DBSR_DAC2R,
			DBSR_DAC2W,
			78 /* DAC2 */
		}
	};

	DEBUG();

	/* Iterate 0: DAC1
	 * Iterate 1: DAC2
         */
	for (i = 0; i < 2; i++) {
		if (stub->dbsr & (dac_tab[i].dbsr_rmask)) {
			if (stub->dbsr & (dac_tab[i].dbsr_wmask)) {
				DEBUG("awatch");
				pkt_cat_string(stub->rsp, "awatch");
			} else {
				DEBUG("rwatch");
				pkt_cat_string(stub->rsp, "rwatch");
			}
		} else if (stub->dbsr & (dac_tab[i].dbsr_wmask)) {
			DEBUG("watch");
			pkt_cat_string(stub->rsp, "watch");
		} else {
			continue;
		}
		pkt_cat_string(stub->rsp, ":");
		read_cpu_reg(trap_frame, value, dac_tab[i].reg_num);
		pkt_cat_string(stub->rsp, (char *)value);
		pkt_cat_string(stub->rsp, ";");
	}
}

/* TODO: Autogenerate defines for the e500mc_reg_table indexes (used in transmit_stop_reply_pkt_T (),
 *       pkt_write_dac_state()) for calls to read_cpu_reg().
 */
static void transmit_stop_reply_pkt_T(trapframe_t *trap_frame, gdb_stub_core_context_t *stub)
{
	uint8_t value[32];
	DEBUG("Sending T rsp");
	pkt_cat_string(stub->rsp, "T");
	pkt_write_hex_byte_update_cur(stub->rsp, 5);
	pkt_write_dac_state(trap_frame, stub);
	pkt_write_hex_byte_update_cur(stub->rsp, 1); /* r1 */
	pkt_cat_string(stub->rsp, ":");
	read_cpu_reg(trap_frame, value, 1);
	pkt_cat_string(stub->rsp, (char *)value);
	pkt_cat_string(stub->rsp, ";");
	pkt_write_hex_byte_update_cur(stub->rsp, 64); /* pc */
	pkt_cat_string(stub->rsp, ":");
	read_cpu_reg(trap_frame, value, 64);
	pkt_cat_string(stub->rsp, (char *)value);
	pkt_cat_string(stub->rsp, ";");
	/* FIXME: This is a HACK. Is this the right fix? */
	stub->node->bch->rx->data_avail = NULL;
	transmit_response(stub);
}

#ifdef USE_DEBUG_INTERRUPT
/* Note: this is invoked in debug interrupt context
 * (i.e. CE=0, EE=0)
 *
 */
static int debug_exception(trapframe_t *trap_frame)
{
	gcpu_t *gcpu = get_gcpu();
 	gdb_stub_core_context_t *stub = gcpu->dbgstub_cpu_data;
	stub->dbsr = mfspr(SPR_DBSR);

	mtspr(SPR_DBSR, stub->dbsr); /* clear all hw debug events */

	 /* clear ICMP */
	if (stub->dbsr & DBSR_ICMP) {
		mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_ICMP);
	}

	/* send event to trigger the handling at normal 
	 * interrupt context
	 */
 	setgevent(gcpu, stub->gev_dbg);

	return 0;   /* isr handled event */
}

static void debug_gevent_handler(trapframe_t *trap_frame)
{
	handle_debug_event(trap_frame);
}

#endif

static int handle_debug_event(trapframe_t *trap_frame)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub = gcpu->dbgstub_cpu_data;
	register_t nip_reg_value = 0;
	breakpoint_t *breakpoint = NULL;
	breakpoint_type_t bptype = breakpoint->type;
	int trap = 0;

	DEBUG("DBSR=%08lx, srr0=%08lx, srr1=%08lx\n",stub->dbsr,trap_frame->srr0,trap_frame->srr1);

	/* enable interrupts, so UART keeps working */
	enable_int();

#ifdef USE_DEBUG_INTERRUPT
	if (stub->dbsr) {

		if (stub->dbsr & ~(DBSR_TRAP | DBSR_ICMP | DBSR_IAC1 | DBSR_IAC2 |
             	    DBSR_DAC1R | DBSR_DAC1W | DBSR_DAC2R | DBSR_DAC2W))
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR, "%s: unknown debug event (%08lx)\n",
			         __func__, stub->dbsr);

		if (stub->dbsr & DBSR_TRAP) {
			trap = 1;
		} else if (stub->dbsr & (DBSR_ICMP| DBSR_IAC1 | DBSR_IAC2 | DBSR_DAC1R |
			   DBSR_DAC1W | DBSR_DAC2R | DBSR_DAC2W)) {
			transmit_stop_reply_pkt_T(trap_frame, stub);
		}

		stub->dbsr = 0;  /* clear all pending events */

	}
#else
	trap = 1;
#endif

	if (trap) {
		nip_reg_value = trap_frame->srr0;

		/* Check if this is a GDB stub registered breakpoint.
		 * If so, invoke gdb_stub_main_loop();
		 * Else return with code 1.
		 */
		breakpoint = locate_breakpoint(stub->breakpoint_table,
		                               (uint32_t *)nip_reg_value);
		if (!breakpoint) {
			DEBUG("Not our breakpoint/trap, reflecting to guest.");
#ifndef USE_DEBUG_INTERRUPT
			return 1;   /* reflect the trap to the guest */
#else
			/* need to reflect program interrupt, not debug interrupt */
			trap_frame->exc = EXC_PROGRAM;
			mtspr(SPR_ESR, ESR_PTR);
			reflect_trap(trap_frame);
			return 0;   /* don't want debug_trap() doing any fixup */
#endif
		}

		/* need to save this so after bp is cleared we still know */
		bptype = breakpoint->type;

		if (bptype == internal || bptype == internal_1st_inst) {
			int ret;
			DEBUG("We've hit an internal (set by stub) breakpoint at addr: 0x%p", (uint32_t *)nip_reg_value);
			guestmem_set_data(trap_frame);
			if (breakpoint->associated) {
				DEBUG("Replace the original instruction back at the associated internal "
				      "breakpoint: 0x%p", breakpoint->associated->addr);
				ret = guestmem_out32(breakpoint->associated->addr,
				               breakpoint->associated->orig_insn);
				ret += guestmem_icache_block_sync((char *)breakpoint->associated->addr);
				DEBUG("Delete the associated internal breakpoint: 0x%p",
				       breakpoint->associated->addr);
				delete_breakpoint(stub->breakpoint_table,
				                  breakpoint->associated);
			}
			DEBUG("Replace the original instruction back at the internal breakpoint: 0x%p", breakpoint->addr);
			ret = guestmem_out32(breakpoint->addr,
			                     breakpoint->orig_insn);
			ret += guestmem_icache_block_sync((char *)breakpoint->addr);
			delete_breakpoint(stub->breakpoint_table, breakpoint);
			breakpoint = NULL;
		} else {
			DEBUG("We've hit an external (user set) breakpoint at addr: 0x%p", (uint32_t *)nip_reg_value);
		}

		if (bptype == internal_1st_inst)
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_NORMAL,
				"gdb: waiting for host debugger...\n");
		else
			transmit_stop_reply_pkt_T(trap_frame, stub);
	}

	DEBUG("Calling: gdb_stub_main_loop().");
	gdb_stub_main_loop(trap_frame, GDB_DEBUG_EXCEPTION);
	DEBUG("Returning from handle_debug_exception()");
	return 0;
}

void rx_gevent_handler(trapframe_t *trap_frame)
{
	DEBUG("Calling: gdb_stub_main_loop().");
	gdb_stub_main_loop(trap_frame,GDB_GEVENT);
}

/*
 * cause is the
 *
 */
void gdb_stub_main_loop(trapframe_t *trap_frame, int event_type)
{
	gcpu_t *gcpu = get_gcpu();
	gdb_stub_core_context_t *stub = gcpu->dbgstub_cpu_data;
	uint8_t *cur_pos, *sav_pos, *data;
	uint32_t offset, length;
	uint32_t crc32_sum;
	uint8_t err_flag = 0;
	uint8_t escape_flag = 0;
	uint32_t *addr = NULL;
	uint8_t aux, value[17];
	uint32_t reg_num, i, j;
	uint8_t breakpoint_type;
	breakpoint_t *breakpoint = NULL;
#ifndef USE_DEBUG_INTERRUPT
	uint32_t *nia = NULL, *pc = NULL;
	breakpoint_t *breakpoint_nia = NULL;
	breakpoint_t *breakpoint_incrpc = NULL;
#endif
	const char *td; /* td: target description */
	int loop_count = 0;
#ifdef USE_DEBUG_INTERRUPT
	register_t dbcr0 = 0;
#endif

	printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
		"gdb: in RSP Engine, main loop.\n");

stub_start:
	DEBUG("At stub_start.");

	if (event_type == GDB_DEBUG_EXCEPTION && loop_count !=0 && queue_empty(stub->node->bch->rx)) {
		DEBUG("Returning to guest.");
		return;
	}

	if (event_type == GDB_GEVENT && queue_empty(stub->node->bch->rx)) {
		DEBUG("Returning to guest.");
		return;
	}

	/* Deregister call back. */
	stub->node->bch->rx->data_avail = NULL;

	while (1) {
		loop_count++;
		TRACE("In main loop.");
		dump_breakpoint_table(stub->breakpoint_table);
		pkt_reset(stub->cmd);
		receive_command(trap_frame, stub);
		switch(tokenize(content(stub->cmd))) {

		case continue_execution:
			/* o Reregister call backs.
			 * o Check byte channel if something arrived in the
			 *   meanwhile and if something _did_ arrive, loop
			 *   back to the beginning of the gdb_stub_main_loop
			 *   where interrupts are disabled.
			 */
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"GOT 'c' PACKET.\n");
return_to_guest:
			stub->node->bch->rx->data_avail = rx;
			smp_sync();
			goto stub_start;

		case read_register:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'p' packet.\n");
			cur_pos = content(stub->cmd);
			reg_num = scan_num(&cur_pos, '\0');
			read_cpu_reg(trap_frame, value, reg_num);
			TRACE("Register: %d, read-value: %s", reg_num, value);
			pkt_cat_string(stub->rsp, (char *)value);
			break;

		case read_registers:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'g' packet.\n");
			for (reg_num = 0; reg_num < NUMREGS; reg_num++) {
				read_cpu_reg(trap_frame, value, reg_num);
				TRACE("Register: %d, read-value: %s", reg_num, value);
				pkt_cat_string(stub->rsp, (char *)value);
			}
			break;

		case write_register:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'P' packet.\n");
			err_flag = 0;
			cur_pos = content(stub->cmd);
			reg_num = scan_num(&cur_pos, '=');
			BREAK_IF_END(cur_pos);
			data = ++cur_pos;
			DEBUG("Register: %d, write-value: %s (decimal: %lld)",
			       reg_num, data, htoi(data));
			err_flag = write_cpu_reg(trap_frame, data, reg_num);
			if (err_flag == 0) {
				pkt_cat_string(stub->rsp, "OK");
			} else {
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		case write_registers:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'G' packet.\n");
			cur_pos = content(stub->cmd);
			data = ++cur_pos;
			BREAK_IF_END(data);
			DEBUG("Got data (register values): %s", data);
			err_flag = 0;
			for (reg_num = 0; reg_num < NUMREGS; reg_num++) {
				int32_t byte_length = 0;
				byte_length = e500mc_reg_table[reg_num].bitsize / 8;
				DEBUG("PRE strncpy");
				strncpy((char *)value, (const char *)data, 2 * byte_length);
				DEBUG("POST strncpy");
				value[2 * byte_length] = '\0';
				err_flag = write_cpu_reg(trap_frame, value, reg_num);
				data += 2 * byte_length;
			}
			DEBUG("Done writing registers, sending: OK");
			pkt_cat_string(stub->rsp, "OK");
			break;

		case set_thread:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got packet: 'H'\n");
			cur_pos = content(stub->cmd);
			cur_pos++;
			switch(*cur_pos) {

			case 'c':
				DEBUG("Got 'c' for step and"
				       " continue operations.");
				break;

			case 'g':
				DEBUG("Got 'g' for other operations.");
				break;

			default:
				DEBUG("Unhandled case '%c' in 'H' packet.", *cur_pos);
				break;
			}
			cur_pos++;
			if (strcmp((char *) cur_pos, "0") == 0) {
				DEBUG("Pick up any thread.");
			} else if (strcmp((char *) cur_pos, "-1") == 0) {
				DEBUG("All the threads.");
			} else {
				DEBUG("Thread: %s", cur_pos);
			}
			pkt_cat_string(stub->rsp, "OK");
			break;

		case read_memory:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'm' packet.\n");
			cur_pos = content(stub->cmd);
			addr = (uint32_t *)(uintptr_t)scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, '\0');
			TRACE("Read memory at addr: 0x%p, length: %d", addr, length);
			guestmem_set_data(trap_frame);
			err_flag = 0;
			for (i = 0; i < length; i++) {
				if (guestmem_in8((uint8_t *) addr + i, ((uint8_t *) (&value[0]))) != 0) {
					err_flag = 1;
					break;
				}
				TRACE("value[0]: %c", value[0]);
				pkt_write_hex_byte_update_cur(stub->rsp, value[0]);
				TRACE("byte address: 0x%p, upper nibble value: %c",
				       (uint8_t *)addr + i, hex(upper_nibble(value[0])));
				TRACE("byte address: 0x%p, lower nibble value: %c",
				       (uint8_t *)addr + i, hex(lower_nibble(value[0])));
			}
			if (err_flag == 1) {
				pkt_reset(stub->rsp);
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		case write_memory:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'M' packet.\n");
			cur_pos = content(stub->cmd);
			addr = (uint32_t *)(uintptr_t)scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, ':');
			BREAK_IF_END(cur_pos);
			cur_pos++;
			data = cur_pos;
			DEBUG("addr: 0x%p, length: %d, data: %s", addr, length, data);
			err_flag = 0;
			for (i = 0; i < length; i++) {
				int ret;
				value[0] = data[2*i];
				value[1] = data[2*i+1];
				value[2] = 0;
				ret = guestmem_out8((uint8_t *)addr + i, htoi((uint8_t *)value));
				ret += guestmem_icache_block_sync((char *)addr + i);
				if (ret != 0)
					err_flag = 1;
			}
			if (err_flag == 0) {
				pkt_cat_string(stub->rsp, "OK");
			} else {
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		case raw_write:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "Got 'X' packet.\n");
			cur_pos = content(stub->cmd);
			addr = (uint32_t *)(uintptr_t)scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, ':');
			BREAK_IF_END(cur_pos);
			cur_pos++;
			data = cur_pos;
			DEBUG("addr: 0x%p, length: %d", addr, length);
			err_flag = 0;
			escape_flag = 0;
			j = 0;
			for (i = 0; i < length; i++) {
				int ret;
				DEBUG("X packet byte %d: 0x%2x", i, data[i]);
				if (data[i] == '}') {
					DEBUG("Skipping escape character in X packet at %d", i);
					escape_flag = 1;
					continue;
				}
				if (escape_flag == 0) {
					DEBUG("Writing byte %d: 0x%2x in X packet to 0x%p[%d]", i, data[i], addr, j);
					ret = guestmem_out8((uint8_t *)addr + j, data[i]);
					ret += guestmem_icache_block_sync((char *)addr + j);
					if (ret != 0)
						err_flag = 1;
				} else {
					DEBUG("Writing (escaped) byte %d: 0x%2x in X packet to 0x%p[%d]", i, 0x20 ^ data[i], addr, j);
					escape_flag = 0;
					ret = guestmem_out8((uint8_t *)addr + j, 0x20 ^ data[i]);
					ret += guestmem_icache_block_sync((char *)addr + j);
					if (ret != 0)
						err_flag = 1;
				}
				j++;
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
			err_flag = 0;
			breakpoint_type = scan_num(&cur_pos, ',');
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got '%c%d' packet.\n", aux, breakpoint_type);
			if (breakpoint_type != memory_breakpoint
#ifdef USE_DEBUG_INTERRUPT
			    && breakpoint_type != hardware_breakpoint
			    && breakpoint_type != write_watchpoint
			    && breakpoint_type != read_watchpoint
			    && breakpoint_type != access_watchpoint
#endif
			) {
#ifdef USE_DEBUG_INTERRUPT
				DEBUG("We only support memory and hardware breakpoints,"
				      "and read, write or access watchpoints.");
#else
				DEBUG("We only support memory breakpoints.");
#endif
				/* Send back an empty response to indicate
				 * that this kind of a breakpoint is not
				 * supported.
				 */
				break;
			}
			BREAK_IF_END(cur_pos);
			addr = (uint32_t *)(uintptr_t)scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, '\0');
			if (length != 4) {
				DEBUG("Breakpoint must be of length: 4.");
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
				break;
			}
			DEBUG("Breakpoint type: %d, addr: 0x%p, length: %d",
			       breakpoint_type, addr, length);
			DEBUG("aux: %c", aux);
			if (aux == 'Z') { /* 'Z':Breakpoint insertion. */
				switch (breakpoint_type) {
				case memory_breakpoint:
					DEBUG ("Invoking set_breakpoint.");
					breakpoint = set_breakpoint(stub->breakpoint_table,
					                            trap_frame, addr, external);
					if (breakpoint)
						breakpoint->associated = NULL;
					err_flag = breakpoint ? 0 : 1;
					break;
#ifdef USE_DEBUG_INTERRUPT
				case hardware_breakpoint:
					err_flag = 0;
					dbcr0 = mfspr(SPR_DBCR0);
					if (!(dbcr0 & DBCR0_IAC1)) {
						mtspr(SPR_IAC1, (register_t)addr);
						mtspr(SPR_DBCR0, dbcr0 | DBCR0_IAC1);
					} else if (!(dbcr0 & DBCR0_IAC2)) {
						mtspr(SPR_IAC2, (register_t)addr);
						mtspr(SPR_DBCR0, dbcr0 | DBCR0_IAC2);
					}
					else {
						DEBUG("Failed to set hardware breakpoint at address: 0x%p.", addr);
						err_flag = 1;
					}
					DEBUG("HW breakpoint dbcr0: 0x%lx", mfspr(SPR_DBCR0));
					break;
				case write_watchpoint:
				case read_watchpoint:
				case access_watchpoint:
					err_flag = 0;
					dbcr0 = mfspr(SPR_DBCR0);
					if (!(dbcr0 & (DBCR0_DAC1R | DBCR0_DAC1W))) {
						mtspr(SPR_DAC1, (register_t)addr);
						if (breakpoint_type == write_watchpoint)
							mtspr(SPR_DBCR0, dbcr0 | DBCR0_DAC1W);
						else if (breakpoint_type == read_watchpoint)
							mtspr(SPR_DBCR0, dbcr0 | DBCR0_DAC1R);
						else /* access_watchpoint */
							mtspr(SPR_DBCR0, dbcr0 | DBCR0_DAC1R | DBCR0_DAC1W);
					} else if (!(dbcr0 & (DBCR0_DAC2R | DBCR0_DAC2W))) {
						mtspr(SPR_DAC2, (register_t)addr);
						if (breakpoint_type == write_watchpoint)
							mtspr(SPR_DBCR0, dbcr0 | DBCR0_DAC2W);
						else if (breakpoint_type == read_watchpoint)
							mtspr(SPR_DBCR0, dbcr0 | DBCR0_DAC2R);
						else /* access_watchpoint */
							mtspr(SPR_DBCR0, dbcr0 | DBCR0_DAC2R | DBCR0_DAC2W);
					}
					else {
						DEBUG("Failed to set hardware watchpoint at address: 0x%p.", addr);
						err_flag = 1;
					}
					DEBUG("Watchpoint dbcr0: 0x%lx", mfspr(SPR_DBCR0));
					break;
#endif
				default:
					/* If this is not a known breakpoint type;
					 * we would not be here in the first place.
					 */
					break;
				}
			} else { /* 'z':Breakpoint removal. */
				switch (breakpoint_type) {
				case memory_breakpoint:
					DEBUG("Invoking locate_breakpoint.");
					breakpoint = locate_breakpoint(stub->breakpoint_table, addr);
					if (breakpoint) {
						DEBUG("Located breakpoint placed at: 0x%p", addr);
						delete_breakpoint(stub->breakpoint_table, breakpoint);
						err_flag = 0;
					} else /* Vow! Spurious address! */ {
						DEBUG("No breakpoint placed at: 0x%p", addr);
						err_flag = 1;
					}
					break;
#ifdef USE_DEBUG_INTERRUPT
				case hardware_breakpoint:
					err_flag = 0;
					if (mfspr(SPR_IAC1) == (register_t)addr)
						mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_IAC1);
					else if (mfspr(SPR_IAC2) == (register_t)addr)
						mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_IAC2);
					else {
						DEBUG("No HW breakpoint at address: 0x%p.", addr);
						err_flag = 1;
					}
					break;
				case write_watchpoint:
				case read_watchpoint:
				case access_watchpoint:
					err_flag = 0;
					if (mfspr(SPR_DAC1) == (register_t)addr)
						mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~(DBCR0_DAC1R | DBCR0_DAC1W));
					else if (mfspr(SPR_DAC2) == (register_t)addr)
						mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~(DBCR0_DAC2R | DBCR0_DAC2W));
					else {
						DEBUG("No HW breakpoint at address: 0x%p.", addr);
						err_flag = 1;
					}
					break;
#endif
				default:
					/* If this is not a known breakpoint type;
					 * we would not be here in the first place.
					 */
					break;
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
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 's' packet.\n");
#ifndef USE_DEBUG_INTERRUPT
			cur_pos = content(stub->cmd);
			pc = (uint32_t *)trap_frame->srr0;
			/* If there is no address to resume at; we resume at pc. */
			addr = cur_pos[1] ? (uint32_t *)(uintptr_t)scan_num(&cur_pos, '\0') : pc;
			nia = next_insn_addr(trap_frame);
			DEBUG("Single stepping to insn at nia: 0x%p or pc + 4: 0x%p", nia, pc + 1);
			DEBUG("Setting internal breakpoint at nia: 0x%p.", nia);
			breakpoint_nia = set_breakpoint(stub->breakpoint_table, trap_frame, nia, internal);
			DEBUG("%s setting internal breakpoint at nia: 0x%p.",
			       breakpoint_nia ? "Done" : "Not", nia);
			/* Optimize: If nia == pc + 1 - create only one internal breakpoint. */
			if (nia != pc + 1) {
				DEBUG("Setting internal breakpoint at pc + 4: 0x%p.", pc + 1);
				breakpoint_incrpc = set_breakpoint(stub->breakpoint_table, trap_frame, pc + 1, internal);
				DEBUG("%s setting internal breakpoint at pc + 4: 0x%p.",
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
			DEBUG("Stepping after hitting internal (set by stub) breakpoint.");
#else
			/* set ICMP */
			mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) | DBCR0_ICMP);
#endif
			DEBUG("Done with s-packet, will return to guest.");
			goto return_to_guest;
			return;
			break;

		case return_current_thread_id:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'qC' packet.\n");
			/* For now let pid == 0 */
			pkt_cat_string(stub->rsp, "QC0");
			break;

		case get_section_offsets:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got 'qOffsets' packet.\n");
			pkt_cat_string(stub->rsp, "Text=0;Data=0;Bss=0");
			break;

		case supported_features:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
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
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
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
				DEBUG ("td-len: %d, offset: %d, length: %d", strlen (td), offset, length);
				if (offset < strlen (td)) {
					pkt_cat_string(stub->rsp, "m");
					pkt_cat_stringn(stub->rsp, (const uint8_t *)(td + offset), length);
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
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Got stop reply packet: '?'\n");
			pkt_cat_string(stub->rsp, "S");
			pkt_write_hex_byte_update_cur(stub->rsp, 2);
			break;

		case detach:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "Got 'D' packet.\n");
			dump_breakpoint_table(stub->breakpoint_table);
			clear_all_breakpoints(stub->breakpoint_table);
			dump_breakpoint_table(stub->breakpoint_table);
			DEBUG("Returning to guest on a detach.");
			pkt_cat_string(stub->rsp, "OK");
			transmit_response(stub);
			goto return_to_guest;
			break;

		case monitor:
			/* TODO: List monitor commands on "monitor help". And doc that! */
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "Got 'qRcmd' packet.\n");
			err_flag = 0;
			cur_pos = content(stub->cmd);
			cur_pos = scan_till(cur_pos, ',');
			BREAK_IF_END(cur_pos);
			cur_pos++;
			htos(cur_pos);
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "Got '%s' monitor command.\n", cur_pos);
			if (strcmp((char *) cur_pos, "restart") == 0) {
				printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "Restarting partition.\n");
				restart_guest(get_gcpu()->guest);
				pkt_cat_string(stub->rsp, "OK");
				transmit_response(stub);
				goto return_to_guest;
			} else {
				err_flag = 1;
			}
			if (err_flag == 0) {
				pkt_cat_string(stub->rsp, "OK");
			} else {
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		case kill:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "Got 'k' packet.\n");
			DEBUG("Resetting partition.");
			restart_guest(get_gcpu()->guest);
			goto return_to_guest;
			break;

		case compute_crc32:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG, "Got 'qCRC' packet.\n");
			cur_pos = content(stub->cmd);
			cur_pos = scan_till(cur_pos, ':');
			cur_pos++;
			addr = (uint32_t *)(uintptr_t)scan_num(&cur_pos, ',');
			BREAK_IF_END(cur_pos);
			length = scan_num(&cur_pos, '\0');
			DEBUG("addr: 0x%p, length: %d", addr, length);
			err_flag = 0;
			crc32_sum = crc32(trap_frame, addr, length, &err_flag);
			if (err_flag == 0) {
			        pkt_cat_string(stub->rsp, "C");
				sprintf((char *)value, "%x", crc32_sum);
			        pkt_cat_string(stub->rsp, (char *)value);
			} else {
				pkt_cat_string(stub->rsp, "E");
				pkt_write_hex_byte_update_cur(stub->rsp, 0);
			}
			break;

		default:
			printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_DEBUG,
				"Unhandled RSP directive: %s, transmitting response: '%s'\n",
			       content(stub->cmd), content(stub->rsp));
			break;
		}
		transmit_response(stub);
	}
}

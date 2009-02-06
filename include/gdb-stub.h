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
#ifndef __GDB_STUB_H__
#define __GDB_STUB_H__

#include <byte_chan.h>

#define GDB_STUB_INIT_SUCCESS  0
#define GDB_STUB_INIT_FAILURE -1

typedef struct pkt
{
	uint8_t *buf;
	uint32_t len;
	uint8_t *cur;
} pkt_t;

typedef enum breakpoint_type
{
	/* By "external" breakpoints, we mean those that have been set by the
	 * user. When asked by GDB to set a breakpoint, we set it and when
	 * asked by GDB to remove it, we remove it. We do nothing else with
	 * external breakpoints. The stub does not in any sense own this kind
	 * of breakpoint.
	 */
	external,

	/* A breakpoint set by the stub itself is an "internal" breakpoint.
	 * Internal breakpoints are strictly "once only". i.e. We set these
	 * in order to do (software) single step; as soon as we complete the
	 * single step, we delete this kind of breakpoint - including - its
	 * associated internal breakpoint.
	 */
	internal,

	/* A stub can set a breakpoint on the first instruction of the
	 * guest.  In this special case we need to enter the main
	 * stub loop and not send out a reason halted packet.  Otherwise
	 * handled like internal breakpoints.
	 */
	internal_1st_inst,

} breakpoint_type_t;

typedef struct breakpoint
{
	uint32_t *addr;
	uint32_t orig_insn;
	uint32_t count;
	uint8_t taken;
	struct breakpoint *associated;
	breakpoint_type_t type;
} breakpoint_t;

/* The _actual_ upper bound on the number of breakpoints that the
 * user can set per core is MAX_BREAKPOINT_COUNT/3. (We need to
 * insert upto two associated breakpoints per breakpoint (external
 * or internal) in order to single step,
 * (to replace the original insn, have the CPU execute it, and then
 * to replace back the trap_insn) when a breakpoint is hit).
 */
#define MAX_BREAKPOINT_COUNT 32*3

#ifndef CONFIG_DEBUG_STUB_PROGRAM_INTERRUPT
#define USE_DEBUG_INTERRUPT
#endif

/**
 * The following structure collects together all the parameters pertaining to
 * a GDB stub invocation on a per core basis.
 */
typedef struct gdb_stub_core_context {

	dt_node_t *node;  /* the config node in the dev tree for this stub */
	int gev_rx;       /* rx gevent handle */
#ifdef USE_DEBUG_INTERRUPT
	int gev_dbg;      /* debug exception gevent handle */
#endif
	register_t dbsr;  /* shadow copy of dbsr */

	/* These two packets contain the command received from GDB and the
	 * response to be sent to GDB. cmd must always contain a command whose
	 * checksum has been verified and with the leading '$', trailing '#'
	 * and 'checksum' removed. Similarly, rsp is always to contain the
	 * content of the response packet _without_ the leading '$' and '#'
	 * and the 'checksum'. (All routines that operate on these buffers
	 * must ensure and should assume that the content in these buffers
	 * is '\0' terminated).
	 */
	uint8_t *cbuf;
	pkt_t command;
	pkt_t *cmd;

	uint8_t *rbuf;
	pkt_t response;
	pkt_t *rsp;

	breakpoint_t *breakpoint_table;

} gdb_stub_core_context_t;

void gdb_stub_gevent_handler(trapframe_t *trap_frame);

#endif /* __GDB_STUB_H__ */

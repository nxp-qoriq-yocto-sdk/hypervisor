/** @file
 *
 * Copyright (C) 2010,2011 Freescale Semiconductor, Inc.
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

/*******************************************************************************
 * Target side support for the gcov-extract protocol.
 * Conforms to gcov architecture/design (v8)
 ******************************************************************************/

#include <hv.h>
#include <devtree.h>
#include <byte_chan.h>
#include <bcmux.h>
#include <libos/alloc.h>
#include <libos/errors.h>

static struct byte_chan *bc;
static struct byte_chan_handle *bch;
static thread_t *gcov_thread;

uint32_t word_size = sizeof(void *); /* The size of a word on the target (this) side. */

struct data_info {
	uint32_t ea_size;   // size in bytes of effective addresses
	uint32_t data_size;  // size in bytes of data[]
} data_info;

typedef struct data_obj {
	void *addr;    // effective address of the data
	size_t size;   // size in bytes
} data_obj_t;

static data_obj_t gcov_data = {

	.addr = 0x0,
	.size = 0
};

/* gcov_info_list is declared to be of type uint8_t * 'cause we do not know the internal
 * structure of the gcov_info type.
 */
static uint8_t *gcov_info_list;

void __gcov_init (uint8_t *info);

/* Keep __gcov_merge_add() dummy. The gcov_info struct instance emitted by GCC,
 * contains a pointer to __gcov_merge_add(). This function occurs in libgcov,
 * which we do not include in the HV build. __gcov_merge_add() isn't needed
 * until the .gcda's are generated (it's needed for aggregating data from
 * previous runs). We generate the .gcda's on the host using gcov-extract, which
 * links in libgcov. So, for now just keep a dummy copy of __gcov_merge_add() to
 * satisfy the link time dependency.
 */
void __gcov_merge_add (long *counters, unsigned n_counters);
void __gcov_merge_add (long *counters, unsigned n_counters) {}

/* The constructor functions emitted by GCC under -fprofile-arcs are of type
 * ctor_t below. We create a table pf the ctor_t function pointers in .data
 * (see hv.lds) whoose ends are marked by the symbols __CTOR_LIST__, __CTOR_END__
 * At initialization time, gcov_config() will iterate through this table calling
 * each ctor.
 */
typedef void (*ctor_t)(void);
extern ctor_t __CTOR_LIST__, __CTOR_END__;

#define MAX_CMD_SIZE 16

/** Callback for RX interrupt.
 *
 */
static void gcov_rx(queue_t *q, int blocking)
{
	unblock(gcov_thread);
}

static int hex_digit(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		 "%s: invalid hex char: 0x%02x\n", __func__, c);
	assert(0);

	return -1;
}

static int gcov_readchar(queue_t *q)
{
	int d1, d2;

	d1 = hex_digit(queue_readchar_blocking(q, 0));
	d2 = hex_digit(queue_readchar_blocking(q, 0));

	return (d1 << 4) | d2;
}

static int gcov_writechar(queue_t *q, uint8_t ch)
{
	char buf[3];

	snprintf(buf, sizeof(buf), "%02x", ch);

	return queue_write_blocking(q, (uint8_t *)buf, 2);
}

static ssize_t gcov_read(queue_t *q, uint8_t *buf, size_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		buf[i] = gcov_readchar(q);
	}

	return len;
}

static ssize_t gcov_write(queue_t *q, const uint8_t *buf, size_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		gcov_writechar(q, buf[i]);
	}

	return len;
}

static void get_command(char *command)
{
	ssize_t byte_count = 0;
	uint8_t ch;
	int i = 0;

	while (1) {
		ch = gcov_readchar(bch->rx);

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

		if (!strcmp(command, "get-data-info")) {

			unknown_cmd_count = 0;
			gcov_write(bch->tx, (uint8_t *)&data_info.ea_size, sizeof(data_info.ea_size));
			gcov_write(bch->tx, (uint8_t *)&data_info.data_size, sizeof(data_info.data_size));
			gcov_write(bch->tx, (uint8_t *)&gcov_data.addr, word_size);
			gcov_write(bch->tx, (uint8_t *)&gcov_data.size, word_size);

		} else if (!strncmp(command, "get-data", strlen("get-data"))) {

			void *address = 0x0;
			uint32_t length = 0;
			char *p;

			unknown_cmd_count = 0;

			/* get effect addr & size from the host */
			byte_count = gcov_read(bch->rx, (uint8_t *)&address, word_size);
			byte_count = gcov_read(bch->rx, (uint8_t *)&length, word_size);

			/* send the data to the host */
			gcov_write(bch->tx, (uint8_t *)address, length);
		} else if (!strncmp(command, "get-str-size", strlen("get-str-size"))) {
			void *address = 0x0;
			char *p, *q;
			uint32_t length = 0;
			/* get effect addr from the host */
			byte_count = gcov_read(bch->rx, (uint8_t *)&address, word_size);
			q = p = (char *) address;
			while (*q++);
			length = q - p;
			/* send the length to the host */
			gcov_write(bch->tx, (uint8_t *)&length, sizeof(length));
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
	ctor_t *ctor;

	if (gcov_byte_chan_init(config, &bc, &bch))
		return;

	gcov_thread = new_thread(gcov_rx_thread, NULL, 1);
	if (!gcov_thread) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "failed to create gcov thread\n");
		return;
	}

	data_info.ea_size = word_size;
	/* data_info.ea_size:
	 * One word for the address.
	 * One word for the size of the memory pointed to by this address.
	 * We only pass one such (address, size) pair after the first 8
	 * bytes in the reponse to a get-data-info command. Hence 2 words.
	 */
	data_info.data_size = 2 * word_size;

	/* Call the constructors. The constructor functions are emitted by GCC
	 * under -fprofile-arcs
	 */
	for (ctor = &__CTOR_LIST__; ctor < &__CTOR_END__; ctor++)
		(*ctor)();

	/* register the callbacks */
	smp_lwsync();
	bch->rx->data_avail = gcov_rx;

	unblock(gcov_thread);
}

/* __gcov_init () is invoked by an initializer ("constructor") function emitted
 * by GCC for each file that is compiled for coverage analysis with gcov by
 * using the option -fprofile-arcs. A section called .ctors is created, in the
 * emitted code for each such file, containing a pointer to this emitted
 * initializer function. If you see the linker script, you will see that a
 * table of function pointers is created in .data, delimited by the symbols
 * __CTOR_LIST__ and __CTOR_END__ containing each such initializer function
 * pointer from each .ctor section.
 *
 * The hypervisor initialization sequence calls gcov_config(). In gcov_config(),
 * we iterate through this table invoking each gcov'ed file's initialization
 * function, which in turn invokes __gcov_init() with a pointer to the
 * gcov_info struct instance generated by GCC for that file.
 *
 * In __gcov_init(), we chain each gcov_info struct instance that came in, onto
 * a linked-list (pointed to by the variable gcov_info_list), in order to
 * retrieve each gcov_info struct instance later on, when it comes time to
 * generate the .gcda's on the host. Then, we will (in our response to
 * gcov-extract's get-data-info command) specify gcov_data.addr (maintained here
 * to always point to the head of gcov_info_list) as the address for gcov-extract
 * to begin fetching the gcov_info's from.
 */
void __gcov_init(uint8_t *info)
{
#ifndef CONFIG_LIBOS_64BIT
	/* Hard coding alert! */
	memcpy(info + 4, &gcov_info_list, word_size);
	gcov_info_list = info;
#else
#error "Need to port gcov support to 64-bit"
#endif
	gcov_data.addr = gcov_info_list;
}

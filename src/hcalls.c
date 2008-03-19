#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/libos.h>

#include <hv.h>
#include <percpu.h>
#include <byte_chan.h>
#include <vmpic.h>

typedef void (*hcallfp_t)(trapframe_t *regs);

/* This could have latency implications if compare_and_swap
   fails often enough -- but it's unlikely. */
int alloc_guest_handle(guest_t *guest, handle_t *handle)
{
	int again;

	do {
		again = 0;
	
		for (int i = 0; i < MAX_HANDLES; i++) {
			if (guest->handles[i])
				continue;
		
			if (compare_and_swap((unsigned long *)&guest->handles[i],
			                     0, (unsigned long)handle))
				return i;

			again = 1;
		}
	} while (again);

	return -1;
}

static void unimplemented(trapframe_t *regs)
{
	printf("unimplemented hcall %ld\n", regs->gpregs[11]);
	regs->gpregs[3] = -1;
}

static void fh_partition_get_status(trapframe_t *regs)
{
	int lpar_id = regs->gpregs[3];
	printf("id = %d\n",lpar_id);

	/* processing goes here */

	/* if calling lpar is not privileged, return error */

	/* return values */
	regs->gpregs[4] = 0;  /* FIXME lpar status */
	regs->gpregs[5] = 2;  /* FIXME # cpus */
	regs->gpregs[6] = 0x1000000;  /* FIXME mem size */

	/* success */
	regs->gpregs[3] = 0;  
}

static void fh_whoami(trapframe_t *regs)
{
	regs->gpregs[4] = 0x99;  /* FIXME */
	regs->gpregs[3] = 0;  /* success */
}

#ifdef CONFIG_BYTE_CHAN
/*
 * r3: handle
 * r4 : count
 * r5,r6,r7,r8 : bytes
 *
 */
static void fh_byte_channel_send(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];
	size_t len = regs->gpregs[4];
	const uint8_t *buf = (const uint8_t *)&regs->gpregs[5];

#ifdef DEBUG
	printf("byte-channel send\n");
	printf("arg0 %08lx\n",regs->gpregs[3]);
	printf("arg1 %08lx\n",regs->gpregs[4]);
	printf("arg2 %08lx\n",regs->gpregs[5]);
	printf("arg2 %08lx\n",regs->gpregs[6]);
	printf("arg2 %08lx\n",regs->gpregs[7]);
	printf("arg2 %08lx\n",regs->gpregs[8]);
#endif

	if (len > 16) {
		regs->gpregs[3] = -2;  /* invalid arg */
		return;
	}

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	register_t saved = spin_lock_critsave(&bc->tx_lock);

	if (len > queue_get_space(bc->tx))
		regs->gpregs[3] = -3;  /* insufficient room */
	else {
		/* put chars into bytechannel queue here */
		int ret = byte_chan_send(bc, buf, len);
		assert(ret == len);
		regs->gpregs[3] = 0;  /* success */
	}

	spin_unlock_critsave(&bc->tx_lock, saved);
}

static void fh_byte_channel_receive(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];
	size_t max_receive = regs->gpregs[4];
	uint8_t *outbuf = (uint8_t *)&regs->gpregs[5];
	register_t saved;

	if (max_receive > 16) {
		regs->gpregs[3] = -2;  /* invalid arg */
		return;
	}

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	saved = spin_lock_critsave(&bc->rx_lock);
	regs->gpregs[4] = byte_chan_receive(bc, outbuf, max_receive);
	spin_unlock_critsave(&bc->rx_lock, saved);

	regs->gpregs[3] = 0;  /* success */
}

static void fh_byte_channel_poll(trapframe_t *regs)
{
	register_t saved;
	guest_t *guest = get_gcpu()->guest;

	int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	saved = spin_lock_critsave(&bc->rx_lock);
	regs->gpregs[4] = queue_get_avail(bc->rx);
	spin_unlock_critsave(&bc->rx_lock, saved);

	saved = spin_lock_critsave(&bc->tx_lock);
	regs->gpregs[5] = queue_get_space(bc->tx);
	spin_unlock_critsave(&bc->tx_lock, saved);

	regs->gpregs[3] = 0;  /* success */
	return;
}
#else
#define fh_byte_channel_send unimplemented
#define fh_byte_channel_receive unimplemented
#define fh_byte_channel_poll unimplemented
#endif

static hcallfp_t hcall_table[] = {
	&unimplemented,		/* 0 */
	&fh_whoami,		/* 1 */
	&unimplemented,		/* 2 */
	&unimplemented,		/* 3 */
	&unimplemented,		/* 4 */
	&unimplemented,		/* 5 */
	&fh_partition_get_status,/* 6 */
	&unimplemented,		/* 7 */
	&unimplemented,		/* 8 */
	&unimplemented,		/* 9 */
	&fh_vmpic_set_int_config, /* 10 */
	&fh_vmpic_get_int_config, /* 11 */
	&fh_vmpic_set_priority, /* 12 */
	&unimplemented,		/* 13 */
	&fh_vmpic_set_mask,	/* 14 */
	&unimplemented,		/* 15 */
	&unimplemented,		/* 16 */
	&fh_vmpic_eoi,		/* 17 */
	&fh_byte_channel_send,	/* 18 */
	&fh_byte_channel_receive,/* 19 */
	&fh_byte_channel_poll,	/* 20 */
	&fh_vmpic_iack,		/* 21 */
	&unimplemented,		/* 22 */
	&unimplemented,		/* 23 */
	&unimplemented,		/* 24 */
	&unimplemented,		/* 25 */
	&unimplemented,		/* 26 */
	&unimplemented,		/* 27 */
	&unimplemented,		/* 28 */
	&unimplemented,		/* 29 */
};

void hcall(trapframe_t *regs)
{
	unsigned int token = regs->gpregs[11];   /* hcall token is in r11 */

	if (unlikely(token >= sizeof(hcall_table) / sizeof(hcallfp_t))) {
		regs->gpregs[3] = -1; /* status */
		return;
	}

	hcall_table[token](regs);
}

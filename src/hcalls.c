
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
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/libos.h>
#include <libos/trap_booke.h>

#include <hv.h>
#include <percpu.h>
#include <byte_chan.h>
#include <vmpic.h>
#include <pamu.h>
#include <ipi_doorbell.h>
#include <paging.h>
#include <events.h>
#include <errors.h>

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

/**
 * Return a pointer to the guest_t for a given handle, or NULL if the handle
 * is invalid, does not point to a guest_t, or does not point to a guest_t
 * that the caller is allowed to access.
 */
static inline guest_t *handle_to_guest(int handle)
{
	if (handle == -1)
		return get_gcpu()->guest;

	if ((unsigned int)handle >= MAX_HANDLES)
		return NULL;

	if (!get_gcpu()->guest->handles[handle])
		return NULL;

	return get_gcpu()->guest->handles[handle]->guest;
}

static void unimplemented(trapframe_t *regs)
{
	printf("unimplemented hcall %ld\n", regs->gpregs[11]);
	regs->gpregs[3] = FH_ERR_UNIMPLEMENTED;
}

static void fh_partition_restart(trapframe_t *regs)
{
	guest_t *guest;
	
	guest = handle_to_guest(regs->gpregs[3]);
	if (!guest) {
		regs->gpregs[3] = EINVAL;
		return;
	}
	
	// TSR[WRS] is reset to zero during a normal restart
	get_gcpu()->tsr = 0;

	regs->gpregs[3] = restart_guest(guest) ? FH_ERR_INVALID_STATE : 0;
}

void restart_core(trapframe_t *regs)
{
	do_stop_core(regs, 1);
}

static void fh_partition_get_status(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);

	if (!guest) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	regs->gpregs[4] = guest->state;
	regs->gpregs[3] = 0;  
}

static void fh_whoami(trapframe_t *regs)
{
	regs->gpregs[4] = get_gcpu()->gcpu_num;
	regs->gpregs[3] = 0;  /* success */
}

/**
 * Structure definition for the fh_partition_memcpy scatter-gather list
 *
 * This structure must be aligned on 32-byte boundary
 *
 * @source: guest physical address to copy from
 * @destination: guest physical address to copy to
 * @size: number of bytes to copy
 * @reserved: reserved, must be zero
 */
struct fh_sg_list {
	uint64_t source;
	uint64_t target;
	uint64_t size;
	uint64_t reserved;
} __attribute__ ((aligned (32)));

#define SG_PER_PAGE	(PAGE_SIZE / sizeof(struct fh_sg_list))

/**
 * Copy a block of memory from one guest to another
 */
static void fh_partition_memcpy(trapframe_t *regs)
{
	guest_t *source = handle_to_guest(regs->gpregs[3]);
	guest_t *target = handle_to_guest(regs->gpregs[4]);

	unsigned int num_sgs = regs->gpregs[7];
	static struct fh_sg_list sg_list[SG_PER_PAGE];
	size_t sg_size = num_sgs * sizeof(struct fh_sg_list);
	phys_addr_t sg_gphys =
		(phys_addr_t) regs->gpregs[6] << 32 | regs->gpregs[5];
	static uint32_t sg_lock;

	if (!source || !target) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	while (num_sgs) {
		size_t bytes_to_copy = min(PAGE_SIZE, sg_size);
		unsigned int sg_to_copy = min(SG_PER_PAGE, num_sgs);

		spin_lock(&sg_lock);

		/* Read the next page of the guest's scatter-gather list into
		   memory. */
		if (copy_from_gphys(get_gcpu()->guest->gphys, sg_list, sg_gphys,
				    bytes_to_copy) != bytes_to_copy) {
			regs->gpregs[3] = EFAULT;
			spin_unlock(&sg_lock);
			return;
		}

		/* Now go through that list and copy the memory one entry at a
		   time. */
		for (unsigned i = 0; i < sg_to_copy; i++) {
			size_t size = copy_between_gphys(target->gphys,
				sg_list[i].target,
				source->gphys,
				sg_list[i].source,
				sg_list[i].size);

			if (size != sg_list[i].size) {
				regs->gpregs[3] = EFAULT;
				spin_unlock(&sg_lock);
				return;
			}
		}

		spin_unlock(&sg_lock);

		sg_gphys += bytes_to_copy;
		sg_size -= bytes_to_copy;
		num_sgs -= sg_to_copy;
	}

	regs->gpregs[3] = 0;
}

#ifdef CONFIG_PAMU
static void fh_dma_enable(trapframe_t *regs)
{
	unsigned int liodn = regs->gpregs[3];
	int ret;

	ret = pamu_enable_liodn(liodn);
	if (ret < 0) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	regs->gpregs[3] = 0;
}

static void fh_dma_disable(trapframe_t *regs)
{
	unsigned int liodn = regs->gpregs[3];
	int ret;

	ret = pamu_disable_liodn(liodn);
	if (ret < 0) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	regs->gpregs[3] = 0;
}
#else
#define fh_dma_enable unimplemented
#define fh_dma_disable unimplemented
#endif

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
		regs->gpregs[3] = EINVAL;
		return;
	}

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	register_t saved = spin_lock_critsave(&bc->tx_lock);

	if (len > queue_get_space(bc->tx))
		regs->gpregs[3] = EAGAIN;
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
	unsigned int handle = regs->gpregs[3];
	size_t max_receive = regs->gpregs[4];
	uint8_t *outbuf = (uint8_t *)&regs->gpregs[5];
	register_t saved;

	if (max_receive > 16) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = EINVAL;
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

	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	saved = spin_lock_critsave(&bc->rx_lock);
	regs->gpregs[4] = queue_get_avail(bc->rx);
	spin_unlock_critsave(&bc->rx_lock, saved);

	saved = spin_lock_critsave(&bc->tx_lock);
	regs->gpregs[5] = queue_get_space(bc->tx);
	spin_unlock_critsave(&bc->tx_lock, saved);

	regs->gpregs[3] = 0;  /* success */
}
#else
#define fh_byte_channel_send unimplemented
#define fh_byte_channel_receive unimplemented
#define fh_byte_channel_poll unimplemented
#endif

static void fh_partition_send_dbell(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	ipi_doorbell_handle_t *db_handle;

	unsigned int handle = regs->gpregs[3];

	/* FIXME: race against handle closure */
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	db_handle = guest->handles[handle]->db;
	if (!db_handle) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	regs->gpregs[3] = (send_doorbells(db_handle->dbell) >= 0) ? 0 : FH_ERR_CONFIG;
}

static void fh_partition_start(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	if (!guest) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	guest->entry = regs->gpregs[4];
	regs->gpregs[3] = start_guest(guest) ? FH_ERR_INVALID_STATE : 0;
}


static void fh_partition_stop(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	if (!guest) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	// TSR[WRS] is reset to zero during a normal restart
	get_gcpu()->tsr = 0;

	regs->gpregs[3] = stop_guest(guest) ? FH_ERR_INVALID_STATE : 0;
}

static hcallfp_t hcall_table[] = {
	unimplemented,                     /* 0 */
	fh_whoami,
	unimplemented,
	unimplemented,
	unimplemented,                     /* 4 */
	fh_partition_restart,
	fh_partition_get_status,
	fh_partition_start,
	fh_partition_stop,                 /* 8 */
	fh_partition_memcpy,
	fh_vmpic_set_int_config,
	fh_vmpic_get_int_config,
	fh_dma_enable,                     /* 12 */
	fh_dma_disable,
	fh_vmpic_set_mask,
	fh_vmpic_get_mask,
	fh_vmpic_get_activity,             /* 16 */
	fh_vmpic_eoi,
	fh_byte_channel_send,
	fh_byte_channel_receive,
	fh_byte_channel_poll,              /* 20 */
	fh_vmpic_iack,
	unimplemented,
	unimplemented,
	unimplemented,                     /* 24 */
	unimplemented,
	unimplemented,
	unimplemented,
	unimplemented,                     /* 28 */
	unimplemented,
	unimplemented,
	unimplemented,
	fh_partition_send_dbell            /* 32 */
};

void hcall(trapframe_t *regs)
{
	unsigned int token;

	if (unlikely(regs->srr1 & MSR_PR)) {
		printlog(LOGTYPE_EMU, LOGLEVEL_DEBUG,
		         "guest hcall from 0x%lx\n", regs->srr0);
		regs->exc = EXC_PROGRAM;
		mtspr(SPR_ESR, ESR_PPR);
		reflect_trap(regs);
	}

	token = regs->gpregs[11];   /* hcall token is in r11 */
	if (unlikely(token >= sizeof(hcall_table) / sizeof(hcallfp_t))) {
		regs->gpregs[3] = FH_ERR_UNIMPLEMENTED;
		return;
	}

	hcall_table[token](regs);
}

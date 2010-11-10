/*
 * Copyright (C) 2007-2010 Freescale Semiconductor, Inc.
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
#include <libos/alloc.h>
#include <libos/platform_error.h>
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>

#include <hv.h>
#include <percpu.h>
#include <byte_chan.h>
#include <vmpic.h>
#include <pamu.h>
#include <ipi_doorbell.h>
#include <paging.h>
#include <events.h>
#include <error_log.h>
#include <errors.h>
#include <guts.h>

#include <malloc.h>

typedef void (*hcallfp_t)(trapframe_t *regs);

#define GLOBAL_HANDLES 64
#define GLOBAL_HANDLE_INDEX ((GLOBAL_HANDLES + LONG_BITS - 1) / LONG_BITS)

static unsigned long global_handles[GLOBAL_HANDLE_INDEX];
static uint32_t global_handles_lock;

int alloc_global_handle(void)
{
	unsigned int i, j;
	register_t saved = spin_lock_intsave(&global_handles_lock);
	int ret = -1;

	for (j = 0; j < GLOBAL_HANDLE_INDEX; j++) {
		if (global_handles[j] != ~0UL) {
			for (i = 0; i < LONG_BITS; i++) {
				if (!(global_handles[j] & (1 << i))) {
					global_handles[j] |= 1 << i;

					ret = i + (j * LONG_BITS);
					break;
				}
			}
		}
	}

	spin_unlock_intsave(&global_handles_lock, saved);
	return ret;
}

int set_guest_global_handle(guest_t *guest, handle_t *handle,
                            int global_handle)
{
	if (global_handle >= GLOBAL_HANDLES) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			"%s: Invalid global handle\n", __func__);
		return -1;
	}

	handle->id = global_handle;
	handle->handle_owner = guest;
	guest->handles[global_handle] = handle;

	return global_handle;
}

/* This could have latency implications if compare_and_swap
   fails often enough -- but it's unlikely. */
int alloc_guest_handle(guest_t *guest, handle_t *handle)
{
	int again;
	handle->handle_owner = guest;

	do {
		again = 0;
	
		for (int i = GLOBAL_HANDLES; i < MAX_HANDLES; i++) {
			if (guest->handles[i])
				continue;
		
			if (compare_and_swap((unsigned long *)&guest->handles[i],
			                     0, (unsigned long)handle)) {
				handle->id = i;
				return i;
			}

			again = 1;
		}
	} while (again);

	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: out of handles in %s\n", __func__, guest->name);

	return ERR_NORESOURCE;
}

/**
 * Return a pointer to the guest_t for a given handle, or NULL if the handle
 * is invalid, does not point to a guest_t, or does not point to a guest_t
 * that the caller is allowed to access.
 */
guest_t *handle_to_guest(int handle)
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
	regs->gpregs[3] = EV_UNIMPLEMENTED;
}

static phys_addr_t reg_pair(trapframe_t *regs, int reg)
{
	return ((phys_addr_t)regs->gpregs[reg] << 32) + regs->gpregs[reg + 1];
}

static void dtprop_access(trapframe_t *regs, int set)
{
	size_t proplen;
	ssize_t ret;
	dt_node_t *node;
	char *path = NULL, *propname = NULL, *propval = NULL;
	guest_t *target_guest, *cur_guest = get_gcpu()->guest;

	target_guest = handle_to_guest(regs->gpregs[3]);
	if (!target_guest) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}
	regs->gpregs[3] = 0;

	ret = copy_string_from_gphys(cur_guest->gphys, reg_pair(regs, 4),
	                             PAGE_SIZE, &path);
	if (ret < 0) {
		regs->gpregs[3] = -ret;
		goto out;
	}
	
	ret = copy_string_from_gphys(cur_guest->gphys, reg_pair(regs, 6),
	                             PAGE_SIZE, &propname);
	if (ret < 0) {
		regs->gpregs[3] = -ret;
		goto out;
	}

	proplen = regs->gpregs[10];
	if (proplen > PAGE_SIZE * 8) {
		regs->gpregs[3] = EV_EINVAL;
		goto out;
	}

	propval = malloc(proplen);
	if (!propval) {
		regs->gpregs[3] = EV_ENOMEM;
		goto out;
	}
	
	if (set && copy_from_gphys(cur_guest->gphys, propval,
	                           reg_pair(regs, 8), proplen) < proplen) {
		regs->gpregs[3] = EV_EFAULT;
		goto out;
	}

	spin_lock_int(&target_guest->state_lock);

	/* Don't collide with device tree setup being done by the
	 * hypervisor.  The manager should wait until it receives a reset
	 * request for the partition before it tries to set up its device
	 * tree -- at that point the partition will be stopped.
	 */
	if (target_guest->state == guest_starting) {
		regs->gpregs[3] = EV_INVALID_STATE;
		goto unlock;
	}

	node = dt_lookup_path(target_guest->devtree, path, set);
	if (!node) {
		regs->gpregs[3] = set ? EV_ENOMEM : EV_ENOENT;
		goto unlock;
	}

	if (set) {
		if (dt_set_prop(node, propname, propval, proplen) < 0)
			regs->gpregs[3] = EV_ENOMEM;
	} else {
		dt_prop_t *prop = dt_get_prop(node, propname, 0);
		if (!prop) {
			regs->gpregs[3] = EV_ENOENT;
		} else {
			regs->gpregs[4] = prop->len;
			if (prop->len <= proplen)
				memcpy(propval, prop->data, prop->len);
			else
				regs->gpregs[3] = EV_BUFFER_OVERFLOW;
		}
	}

unlock:
	spin_unlock_int(&target_guest->state_lock);

	if (!set && regs->gpregs[3] == 0) {
		ret = copy_to_gphys(cur_guest->gphys, reg_pair(regs, 8),
		                    propval, proplen, 0);
		if ((size_t)ret < proplen)
			regs->gpregs[3] = EV_EFAULT;
	}

out:
	free(path);
	free(propname);
	free(propval);
}

static void hcall_partition_get_dtprop(trapframe_t *regs)
{
	dtprop_access(regs, 0);
}

static void hcall_partition_set_dtprop(trapframe_t *regs)
{
	dtprop_access(regs, 1);
}

static void hcall_partition_restart(trapframe_t *regs)
{
	guest_t *guest;
	const char *who;
	int ret;
	
	guest = handle_to_guest(regs->gpregs[3]);
	if (!guest) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}
	
	// TSR[WRS] is reset to zero during a normal restart
	get_gcpu()->watchdog_tsr = 0;

	if ((int)regs->gpregs[3] == -1)
		who = "self";
	else
		who = "manager";

	ret = restart_guest(guest, "restart", who);
	regs->gpregs[3] = ret ? EV_INVALID_STATE : 0;
}

static void hcall_partition_get_status(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);

	if (!guest) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	regs->gpregs[4] = guest->state;
	regs->gpregs[3] = 0;  

	/* Don't let internal states be public API. */
	if (guest->state >= guest_starting_min &&
	    guest->state <= guest_starting_max)
		regs->gpregs[4] = guest_starting;

	if (guest->state >= guest_stopping_min &&
	    guest->state <= guest_stopping_max)
		regs->gpregs[4] = guest_stopping;
}

static void hcall_whoami(trapframe_t *regs)
{
	regs->gpregs[4] = get_gcpu()->gcpu_num;
	regs->gpregs[3] = 0;  /* success */
}

static void hcall_send_nmi(trapframe_t *regs)
{
	unsigned int ncpu = get_gcpu()->guest->active_cpus;
	unsigned int vcpu_mask = regs->gpregs[3];
	unsigned int bit;

	bit = 31 - count_msb_zeroes_32(vcpu_mask) + 1;
	if (vcpu_mask != 0 && bit > ncpu) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	while (vcpu_mask) {
		gcpu_t *gcpu;

		bit = count_lsb_zeroes_32(vcpu_mask);
		gcpu = get_gcpu()->guest->gcpus[bit];

		setgevent(gcpu, gev_nmi);
		wake_hcall_nap(gcpu);
		vcpu_mask &= ~(1 << bit);
	}

	regs->gpregs[3] = 0; /* success */
}

/**
 * Structure definition for the hcall_partition_memcpy scatter-gather list
 *
 * This structure must be aligned on 32-byte boundary
 *
 * @source: guest physical address to copy from
 * @destination: guest physical address to copy to
 * @size: number of bytes to copy
 * @reserved: reserved, must be zero
 */
struct hcall_sg_list {
	uint64_t source;
	uint64_t target;
	uint64_t size;
	uint64_t reserved;
} __attribute__ ((aligned (32)));

#define SG_PER_PAGE	(PAGE_SIZE / sizeof(struct hcall_sg_list))

/**
 * Copy a block of memory from one guest to another
 */
static void hcall_partition_memcpy(trapframe_t *regs)
{
	guest_t *source = handle_to_guest(regs->gpregs[3]);
	guest_t *target = handle_to_guest(regs->gpregs[4]);

	unsigned int num_sgs = regs->gpregs[7];
	static struct hcall_sg_list sg_list[SG_PER_PAGE];
	size_t sg_size = num_sgs * sizeof(struct hcall_sg_list);
	phys_addr_t sg_gphys =
		(phys_addr_t) regs->gpregs[6] << 32 | regs->gpregs[5];
	static uint32_t sg_lock;

	if (!source || !target) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	while (num_sgs) {
		size_t bytes_to_copy = min(PAGE_SIZE, sg_size);
		unsigned int sg_to_copy = min(SG_PER_PAGE, num_sgs);

		spin_lock_int(&sg_lock);

		/* Read the next page of the guest's scatter-gather list into
		   memory. */
		if (copy_from_gphys(get_gcpu()->guest->gphys, sg_list, sg_gphys,
				    bytes_to_copy) != bytes_to_copy) {
			regs->gpregs[3] = EV_EFAULT;
			spin_unlock_int(&sg_lock);
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
				regs->gpregs[3] = EV_EFAULT;
				spin_unlock_int(&sg_lock);
				return;
			}
		}

		spin_unlock_int(&sg_lock);

		sg_gphys += bytes_to_copy;
		sg_size -= bytes_to_copy;
		num_sgs -= sg_to_copy;
	}

	regs->gpregs[3] = 0;
}

#ifdef CONFIG_PAMU
static void hcall_dma_enable(trapframe_t *regs)
{
	unsigned int liodn = regs->gpregs[3];
	regs->gpregs[3] =  hv_pamu_enable_liodn(liodn);
}

static void hcall_dma_disable(trapframe_t *regs)
{
	unsigned int liodn = regs->gpregs[3];
	regs->gpregs[3] = hv_pamu_disable_liodn(liodn);
}
#else
#define hcall_dma_enable unimplemented
#define hcall_dma_disable unimplemented
#endif

#ifdef CONFIG_BYTE_CHAN
/*
 * r3: handle
 * r4 : count
 * r5,r6,r7,r8 : bytes
 *
 */
static void hcall_byte_channel_send(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];
	size_t len = regs->gpregs[4];
	const uint8_t *buf = (const uint8_t *)&regs->gpregs[5];
#ifdef CONFIG_LIBOS_64BIT
	uint64_t tmp[2];

	tmp[0] = (regs->gpregs[5] << 32) | ((uint32_t)regs->gpregs[6]);
	tmp[1] = (regs->gpregs[7] << 32) | ((uint32_t)regs->gpregs[8]);
	buf = (const uint8_t *)tmp;
#endif

#ifdef DEBUG
	printf("byte-channel send\n");
	printf("arg0 %08lx\n",regs->gpregs[3]);
	printf("arg1 %08lx\n",regs->gpregs[4]);
	printf("arg2 %08lx\n",regs->gpregs[5]);
	printf("arg2 %08lx\n",regs->gpregs[6]);
	printf("arg2 %08lx\n",regs->gpregs[7]);
	printf("arg2 %08lx\n",regs->gpregs[8]);
#endif

	if (len > 4 * sizeof(uint32_t)) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	register_t saved = spin_lock_intsave(&bc->tx_lock);

	ssize_t ret = byte_chan_send(bc, buf, len);
	if (ret == 0 && len != 0)
		regs->gpregs[3] = EV_EAGAIN;
	else
		regs->gpregs[3] = 0;

	regs->gpregs[4] = ret;

	spin_unlock_intsave(&bc->tx_lock, saved);
}

static void hcall_byte_channel_receive(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];
	uint8_t *outbuf = (uint8_t *)&regs->gpregs[5];
	register_t saved;

	/* We don't care how big the caller's buffer is, but we only have four
	 * registers to hold the data, so just truncate the length to the max.
	 * The caller is supposed to be able to handle any count >= 0 anyway.
	 */
	size_t max_receive = min(regs->gpregs[4], 4 * sizeof(uint32_t));

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	saved = spin_lock_intsave(&bc->rx_lock);
	regs->gpregs[4] = byte_chan_receive(bc, outbuf, max_receive);
	spin_unlock_intsave(&bc->rx_lock, saved);

#ifdef CONFIG_LIBOS_64BIT
	uint64_t tmp[2];
	tmp[0] = regs->gpregs[5];
	tmp[1] = regs->gpregs[6];
	regs->gpregs[5] = tmp[0] >> 32;
	regs->gpregs[6] = (uint32_t)tmp[0];
	regs->gpregs[7] = tmp[1] >> 32;
	regs->gpregs[8] = (uint32_t)tmp[1];
#endif

	regs->gpregs[3] = 0;  /* success */
}

static void hcall_byte_channel_poll(trapframe_t *regs)
{
	register_t saved;
	guest_t *guest = get_gcpu()->guest;

	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	byte_chan_handle_t *bc = guest->handles[handle]->bc;
	if (!bc) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	saved = spin_lock_intsave(&bc->rx_lock);
	regs->gpregs[4] = queue_get_avail(bc->rx);
	spin_unlock_intsave(&bc->rx_lock, saved);

	saved = spin_lock_intsave(&bc->tx_lock);
	regs->gpregs[5] = queue_get_space(bc->tx);
	spin_unlock_intsave(&bc->tx_lock, saved);

	regs->gpregs[3] = 0;  /* success */
}
#else
#define hcall_byte_channel_send unimplemented
#define hcall_byte_channel_receive unimplemented
#define hcall_byte_channel_poll unimplemented
#endif

static void hcall_doorbell_send(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	ipi_doorbell_handle_t *db_handle;

	unsigned int handle = regs->gpregs[3];

	/* FIXME: race against handle closure */
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	db_handle = guest->handles[handle]->db;
	if (!db_handle) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	regs->gpregs[3] = (send_doorbells(db_handle->dbell) >= 0) ? 0 : EV_CONFIG;
}

static void hcall_partition_start(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	if (!guest) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	guest->entry = regs->gpregs[4];
	regs->gpregs[3] = start_guest(guest, regs->gpregs[5]) ?
	                  EV_INVALID_STATE : 0;
}

static void hcall_partition_stop(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	const char *who;
	int ret;

	if (!guest) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	// TSR[WRS] is reset to zero during a normal restart
	get_gcpu()->watchdog_tsr = 0;

	if ((int)regs->gpregs[3] == -1)
		who = "self";
	else 
		who = "manager";

	ret = stop_guest(guest, "stop", who);
	regs->gpregs[3] = ret ? EV_INVALID_STATE : 0;
}

static void hcall_system_reset(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;

	if (guest->privileged) {
		regs->gpregs[3] = system_reset();
	} else {
		regs->gpregs[3] = EV_EPERM;
	}
}

static void hcall_err_get_info(trapframe_t *regs)
{
	int handle = regs->gpregs[3];
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	queue_t *q;
	unsigned long *flag = NULL;
	unsigned long mask = 0;
	int peek = regs->gpregs[7];
	int bufsize = regs->gpregs[4];
	phys_addr_t addr;
	hv_error_t err;
	uint32_t *lock;

	if ((peek !=0 && peek != 1) || (bufsize < sizeof(hv_error_t))) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	q = guest->handles[handle]->error_queue;
	if (q == &global_event_queue) {
		if (guest != error_manager_guest) {
			regs->gpregs[3] = EV_EINVAL;
			return;
		}

		flag = &gcpu->crit_gdbell_pending;
		mask = GCPU_PEND_CRIT_INT;
		lock = &global_event_cons_lock;
	} else if (!q) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	} else {
		lock = &guest->error_queue_lock;
	}

	addr = ((phys_addr_t) regs->gpregs[5]) << 32 |
			regs->gpregs[6];

	register_t save = spin_lock_intsave(lock);
	regs->gpregs[3] = error_get(q, &err, flag, mask, peek);
	spin_unlock_intsave(lock, save);

	/* We return the length of data copied to guest space, ideally this
	 * should be same as the sizeof(hv_error_t). We have already ensured
	 * that the bufsize provided by guest isn't less than sizeof(hv_error_t).
	 */

	if (!regs->gpregs[3])
		regs->gpregs[4] = copy_to_gphys(guest->gphys, addr, &err, sizeof(hv_error_t), 1);
	else
		regs->gpregs[4] = 0;
}

#ifdef CONFIG_CLAIMABLE_DEVICES
static void hcall_claim_device(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned int handlenum = regs->gpregs[3];
	dev_owner_t *owner, *prev;
	guest_t *guest = gcpu->guest;
	handle_t *handle;
	claim_action_t *action;
	int ret = EV_EINVAL;

	if (handlenum >= MAX_HANDLES)
		goto out;

	handle = gcpu->guest->handles[handlenum];
	if (!handle)
		goto out;

	owner = handle->dev_owner;
	if (!owner)
		goto out;

again:
	prev = owner->hwnode->claimable_owner;
	if (!prev)
		goto out;

	spin_lock_int(&prev->guest->state_lock);

	/* Ensuring that the previous owner is stopped makes sure that
	 * device tree modifications don't collide (if the partition
	 * were starting), and avoids some other scenarios that are
	 * likely trouble for the guest even if the hv isn't harmed.
	 */
	if (prev->guest->state != guest_stopped) {
		ret = EV_INVALID_STATE;
		goto out_onelock;
	}

	/* Double-check that another partition didn't claim it while
	 * we waited for the lock.
	 */
	if (owner->hwnode->claimable_owner != prev) {
		spin_unlock_int(&prev->guest->state_lock);
		goto again;
	}

	/* Allowable nesting for state_lock is a non-stopped partition's
	 * lock inside a stopped partition's.  The calling partition
	 * might be stopping, but it won't be stopped until all cores
	 * have processed their stop gevents, which they haven't because
	 * we're still running this code.
	 *
	 * We need to hold our own state lock to protect against other
	 * device tree modifications.
	 */
	spin_lock(&guest->state_lock);
	
	ret = EV_ENOMEM;
	const char *hwstatus = dt_get_prop_string(owner->hwnode, "status");
	int hwstatus_ok = !hwstatus || !strncmp(hwstatus, "ok", 2);

	if (dt_set_prop_string(prev->gnode, "fsl,hv-claimable", "standby") < 0)
		goto out_twolocks;

	if (hwstatus_ok &&
	    dt_set_prop_string(prev->gnode, "status", "disabled") < 0)
		goto out_twolocks;

	action = owner->claim_actions;
	while (action) {
		ret = action->claim(action, owner, prev);
		if (ret)
			goto out_twolocks;
		
		action = action->next;
	}

	if (dt_set_prop_string(owner->gnode, "fsl,hv-claimable", "active") < 0)
		goto out_twolocks;

	if (hwstatus_ok &&
	    dt_set_prop_string(owner->gnode, "status", "okay") < 0)
		goto out_twolocks;

	owner->hwnode->claimable_owner = owner;
	ret = 0;

out_twolocks:
	spin_unlock(&guest->state_lock);
out_onelock:
	spin_unlock_int(&prev->guest->state_lock);
out:
	regs->gpregs[3] = ret;
}
#endif


static hcallfp_t fsl_hcall_table[] = {
	unimplemented,
	hcall_err_get_info,
	hcall_partition_get_dtprop,
	hcall_partition_set_dtprop,
	hcall_partition_restart,              /* 4 */
	hcall_partition_get_status,
	hcall_partition_start,
	hcall_partition_stop,
	hcall_partition_memcpy,               /* 8 */
	hcall_dma_enable,
	hcall_dma_disable,
	hcall_send_nmi,
	hcall_vmpic_get_msir,                 /* 12 */
	hcall_system_reset,
#ifdef CONFIG_PM
	hcall_get_core_state,
	hcall_enter_nap,
	hcall_exit_nap,                       /* 16 */
#else
	unimplemented,
	unimplemented,
	unimplemented,                        /* 16 */
#endif
#ifdef CONFIG_CLAIMABLE_DEVICES
	hcall_claim_device,
#else
	unimplemented,
#endif
#ifdef CONFIG_PAMU
	hcall_partition_stop_dma,
#else
	unimplemented,
#endif
};

static hcallfp_t epapr_hcall_table[] = {
	unimplemented,
	hcall_byte_channel_send,
	hcall_byte_channel_receive,
	hcall_byte_channel_poll,
	hcall_int_set_config,               /* 4 */
	hcall_int_get_config,
	hcall_int_set_mask,
	hcall_int_get_mask,
	unimplemented,                      /* 8 */
	hcall_int_iack,
	hcall_int_eoi,
	unimplemented,
	unimplemented,                      /* 12 */
	unimplemented,
	hcall_doorbell_send,
	unimplemented,
	unimplemented /* idle */,           /* 16 */
};

#define HCALL_GET_VENDOR_ID(hcall_token)   (((hcall_token) & 0x7fffffff) >> 16)
#define HCALL_GET_NUMBER(hcall_token)       ((hcall_token) & 0xffff)

void hcall(trapframe_t *regs)
{
	unsigned int token;
	unsigned int vendor_id;
	unsigned int hcall_number;


	set_stat(bm_stat_hcall, regs);

	if (unlikely(regs->srr1 & MSR_PR)) {
		printlog(LOGTYPE_EMU, LOGLEVEL_DEBUG,
		         "guest hcall from 0x%lx\n", regs->srr0);
		regs->exc = EXC_PROGRAM;
		mtspr(SPR_ESR, ESR_PPR);
		reflect_trap(regs);
	}

	token = regs->gpregs[11];   /* hcall token is in r11 */
	vendor_id = HCALL_GET_VENDOR_ID(token);
	hcall_number = HCALL_GET_NUMBER(token);

	/* FIXME: we support ePAPR vendor id == 0 for temporary backwards compatibility */
	if (vendor_id == EV_VENDOR_ID || !vendor_id) {
		if (unlikely(hcall_number >= sizeof(epapr_hcall_table) / sizeof(hcallfp_t))) {
			regs->gpregs[3] = EV_UNIMPLEMENTED;
			return;
		}
		epapr_hcall_table[hcall_number](regs);
	} else {
		if (unlikely(hcall_number >= sizeof(fsl_hcall_table) / sizeof(hcallfp_t))) {
			regs->gpregs[3] = EV_UNIMPLEMENTED;
			return;
		}
		fsl_hcall_table[hcall_number](regs);
	}
}

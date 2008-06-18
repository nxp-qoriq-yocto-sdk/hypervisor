
#include <libos/trapframe.h>
#include <libos/libos.h>
#include <libos/bitops.h>
#include <libos/interrupts.h>
#include <events.h>
#include <vpic.h>
#include <doorbell.h>
#include <gdb-stub.h>
#include <percpu.h>

static eventfp_t event_table[] = {
	&critdbell_to_gdbell_glue,  /* EV_ASSERT_VINT */
	&tlbivax_ipi,               /* EV_TLBIVAX */
};

/* Guest events are processed when returning to the guest, but
 * without regard for the MSR[EE/CE/ME] bits of the guest.
 */
static eventfp_t gevent_table[] = {
	&stop_core,                   /* GEV_STOP */
	&start_core,                  /* GEV_START */
	&restart_core,                /* GEV_RESTART */
	&start_wait_core,             /* GEV_START_WAIT */
#ifdef CONFIG_GDB_STUB
	&gdb_stub_event_handler,      /* GEV_GDB */
#else
	NULL,                         /* GEV_GDB */
#endif
};

void setevent(gcpu_t *gcpu, int event)
{
	/* set the event bit */
	atomic_or(&gcpu->cdbell_pending, (1 << event));
	smp_mbar();
	send_crit_doorbell(gcpu->cpu->coreid);

	// TODO optimization -- could check if it's on
	// the same cpu and just invoke
	// the event function
}

void setgevent(gcpu_t *gcpu, int event)
{
	/* set the event bit */
	atomic_or(&gcpu->gevent_pending, (1 << event));
	smp_mbar();
	gcpu->cpu->ret_user_hook = 1;
	smp_mbar();

	if (gcpu->cpu != cpu)
		send_crit_doorbell(gcpu->cpu->coreid);
}

void ret_to_guest(trapframe_t *frameptr)
{
	gcpu_t *gcpu = get_gcpu();

	assert(!(mfmsr() & MSR_CE));
	assert(cpu->ret_user_hook);

	cpu->ret_user_hook = 0;
	gcpu->waiting_for_gevent = 0;
	
	while (gcpu->gevent_pending) {
		/* get the next event */
		unsigned int bit = count_lsb_zeroes(gcpu->gevent_pending);
		assert(bit < sizeof(gevent_table) / sizeof(eventfp_t));

		/* clear the event */
		atomic_and(&gcpu->gevent_pending, ~(1 << bit));

		smp_mbar();

		/* invoke the function */
		cpu->ret_user_hook = 0;
		gevent_table[bit](frameptr);
	}

	assert(!(mfmsr() & MSR_CE));
}

void critical_doorbell_int(trapframe_t *frameptr)
{
	gcpu_t *gcpu = get_gcpu();
	assert(!(mfmsr() & MSR_CE));

	while (gcpu->cdbell_pending) {
		/* get the next event */
		int bit = count_lsb_zeroes(gcpu->cdbell_pending);
		assert(bit < sizeof(event_table) / sizeof(eventfp_t));

		/* clear the event */
		atomic_and(&gcpu->cdbell_pending, ~(1 << bit));

		smp_mbar();

		/* invoke the function */
		event_table[bit](frameptr);
	}

	if (cpu->ret_user_hook && gcpu->waiting_for_gevent)
		ret_to_guest(frameptr);
}

/** Wait for a guest event, discarding current state.
 *
 * This is used to wait for events such as GEV_START.
 * Any previous state, whether guest or hypervisor, on
 * this core will be lost.
 */
void wait_for_gevent(trapframe_t *regs)
{
	disable_critint();
	get_gcpu()->waiting_for_gevent = 1;
		
	regs->srr1 = mfmsr() | MSR_CE;
	regs->srr0 = (register_t)wait_for_gevent_loop;
	
	/* Reset the stack. */
	regs->gpregs[1] = (register_t)&cpu->kstack[KSTACK_SIZE - FRAMELEN];

	/* Terminate the callback chain. */
	cpu->kstack[KSTACK_SIZE - FRAMELEN] = 0;
	
	/* If a gevent has already been sent, send another now
	 * that critints are disabled.
	 */
	if (cpu->ret_user_hook)
		send_crit_doorbell(cpu->coreid);
}

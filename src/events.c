
#include <libos/trapframe.h>
#include <libos/libos.h>
#include <libos/bitops.h>
#include <interrupts.h>
#include <events.h>
#include <vpic.h>
#include <doorbell.h>

static eventfp_t event_table[] = {
	&critdbell_to_gdbell_glue,		/* EV_ASSERT_VINT */
};

void setevent(gcpu_t *gcpu, int event)
{
	/* set the event bit */
	atomic_or(&gcpu->cdbell_pending, (1 << event));

	send_crit_doorbell(gcpu->cpu->coreid);

	// TODO optimization -- could check if it's on
	// the same cpu and just invoke
	// the event function
}

void critical_doorbell_int(trapframe_t *frameptr)
{
	int bit;
	gcpu_t *gcpu = get_gcpu();

	while (gcpu->cdbell_pending) {
		/* get the next event */
		bit = count_lsb_zeroes(gcpu->cdbell_pending);

		/* clear the event */
		atomic_and(&gcpu->cdbell_pending, ~(1 << bit));

		smp_mbar();

		/* invoke the function */
		event_table[bit](frameptr);
	}
}


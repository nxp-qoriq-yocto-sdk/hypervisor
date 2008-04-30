
#include <libos/trapframe.h>
#include <libos/libos.h>
#include <libos/bitops.h>
#include <libos/interrupts.h>
#include <libos/mpic.h>
#include <errors.h>

extern void trap(trapframe_t *);

void critical_interrupt(trapframe_t *frameptr)
{
	do_mpic_critint();
}

void powerpc_mchk_interrupt(trapframe_t *frameptr)
{
#if 0
	printf("powerpc_mchk_interrupt: machine check interrupt!\n");
	dump_frame(framep);
#endif
}

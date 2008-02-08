/**
 * @file
 *
 *
 */

#ifndef DOORBELL_H
#define DOORBELL_H

#include <libos/percpu.h>
#include <libos/spr.h>

#define MSG_DBELL      0x00000000
#define MSG_DBELL_CRIT 0x08000000
#define MSG_GBELL      0x10000000
#define MSG_GBELL_CRIT 0x18000000
#define MSG_GBELL_MCHK 0x20000000

/** Send local guest doorbell.
 *
 *  Sends a guest doorbell to the current
 *  cpu.
 *
 */
static inline void send_local_guest_doorbell(void)
{
	unsigned long msg = MSG_GBELL |
	                    (get_gcpu()->guest->lpid << 14) |
	                    mfspr(SPR_GPIR);

	asm volatile("msgsnd %0" : : "r" (msg));
}

/** Send critical doorbell.
 *
 *  Always for hypervisor internal use only so
 *  the lpid is always 0.
 *
 */
static inline void send_crit_doorbell(int cpu)
{
	unsigned long msg = MSG_DBELL_CRIT | cpu;

	asm volatile("msgsnd %0" : : "r" (msg));

}

#endif

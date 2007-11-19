#ifndef DOORBELL_H
#define DOORBELL_H

#include <percpu.h>
#include <spr.h>

#define MSG_DBELL      0x00000000
#define MSG_DBELL_CRIT 0x10000000
#define MSG_GBELL      0x20000000
#define MSG_GBELL_CRIT 0x30000000
#define MSG_GBELL_MCHK 0x40000000

static inline void send_local_guest_doorbell(void)
{
	unsigned long msg = MSG_GBELL |
	                    (hcpu->gcpu->guest->lpid << 14) |
	                    mfspr(SPR_PIR);
	asm volatile("msgsnd %0" : : "r" (msg));
}

#endif

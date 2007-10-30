#ifndef FRAME_H
#define FRAME_H

#include "defs.h"
#include "uvtypes.h"
#include <stdint.h>

typedef struct {
	register_t backchain, lrsave;
	register_t gpregs[32];
	register_t lr, ctr;
	uint32_t cr, xer;
	register_t srr0, srr1, dear, esr;
	int exc;
} trapframe_t;

/* PPC ABI requires 16-byte-aligned stack frames. */
#define FRAMELEN roundup(sizeof(trapframe_t), 16)

#endif

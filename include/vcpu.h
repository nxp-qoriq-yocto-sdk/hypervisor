
#ifndef _VCPU_H
#define	_VCPU_H

/*
 * Per-cpu data structures
 */

#include "uvtypes.h"
#include "tlb.h"

#define CPUSAVE_LEN   8 
#define TLB1_SIZE 16

typedef struct {
    int            coreid;
    register_t     uvstack;
    register_t     normsave[CPUSAVE_LEN];
    register_t     critsave[CPUSAVE_LEN];
    register_t     machksave[CPUSAVE_LEN];
    register_t     dbgsave[CPUSAVE_LEN+2];  /* +2 because the dbg handler saves srr0/1 */
    tlb_entry_t tlb1[TLB1_SIZE];
} vcpu_t;

#define CPUSAVE_R28     0               /* where r28 gets saved */
#define CPUSAVE_R29     1               /* where r29 gets saved */
#define CPUSAVE_R30     2               /* where r30 gets saved */
#define CPUSAVE_R31     3               /* where r31 gets saved */
#define CPUSAVE_DEAR    4               /* where SPR_DEAR gets saved */
#define CPUSAVE_ESR     5               /* where SPR_ESR gets saved */
#define CPUSAVE_SRR0    6               /* where SRR0 gets saved */
#define CPUSAVE_SRR1    7               /* where SRR1 gets saved */

/* Note: there is an assumption in exceptions.S that SRR0 and
   SRR1 are at the end of this enumeration */


#endif /* !_VCPU_H */

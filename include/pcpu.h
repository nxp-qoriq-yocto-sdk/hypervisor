
#ifndef _PCPU_H
#define	_PCPU_H

/*
 * TODO: TLB
 */

#include "uvtypes.h"

#define CPUSAVE_LEN   8 

typedef struct {
    int            coreid;
    register_t     uvstack;
    register_t     normsave[CPUSAVE_LEN];
    register_t     critsave[CPUSAVE_LEN+2];   /* why +2 ? */
    register_t     machksave[CPUSAVE_LEN];
    register_t     dbgsave[CPUSAVE_LEN];
} pcpu_t;

#define CPUSAVE_R28     0               /* where r28 gets saved */
#define CPUSAVE_R29     1               /* where r29 gets saved */
#define CPUSAVE_R30     2               /* where r30 gets saved */
#define CPUSAVE_R31     3               /* where r31 gets saved */
#define CPUSAVE_DEAR    4               /* where SPR_DEAR gets saved */
#define CPUSAVE_ESR     5               /* where SPR_ESR gets saved */
#define CPUSAVE_SRR0    6               /* where SRR0 gets saved */
#define CPUSAVE_SRR1    7               /* where SRR1 gets saved */


#endif /* !_PCPU_H */

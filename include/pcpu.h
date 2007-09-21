
#ifndef _PCPU_H
#define	_PCPU_H

#include "uvtypes.h"

#define CRITICAL_SAVE_REGS 10

typedef struct {
    int            coreid;
    register_t     uvstack;
    register_t     critsave[CRITICAL_SAVE_REGS ];
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

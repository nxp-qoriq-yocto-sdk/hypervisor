
#include <uv.h>
#include <libos/console.h>
#include <mpic.h>
#include <libos/io.h>
#include <libos/spr.h>
#include <8578.h>

static inline void mpic_write(uint32_t reg, uint32_t val)
{
	out32(((uint32_t *)(CCSR_BASE+MPIC+reg)),val);
}

static inline register_t mpic_read(uint32_t reg)
{
	return in32((uint32_t *)(CCSR_BASE+MPIC+reg));
}

void mpic_init(unsigned long devtree_ptr)
{

/*
In addition, the following initialization sequence is recommended:
1. Write the vector, priority, and polarity values in each interrupt vector/priority register, leaving
their MSK (mask) bit set.
	-set all vectors to be the mpic offset
2. Clear CTPR (CTPR = 0x0000_0000).
3. Program the MPIC to mixed mode by setting GCR[M].
4. Clear the MSK bit in the vector/priority registers to be used.
5. Perform a software loop to clear all pending interrupts:
  -Load counter with FRR[NIRQ].
  -While counter > 0, read IACK and write EOI to guarantee all the IPR and ISR bits are cleared.
6. Set the processor core CTPR values to the desired values.
*/


}

void mpic_irq_mask(int irq)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	iivpr.msk = 1; /* mask */
	mpic_write(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR,iivpr.data);
}

void mpic_irq_unmask(int irq)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	iivpr.msk = 0; /* unmask */
	mpic_write(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR,iivpr.data);
}

void mpic_irq_set_vector(int irq, uint32_t vector)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	iivpr.vector = vector;
	mpic_write(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR,iivpr.data);
}

uint32_t mpic_irq_get_vector(int irq)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	return iivpr.vector;
}

void mpic_irq_set_priority(int irq, uint8_t priority)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	iivpr.priority = priority;
	mpic_write(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR,iivpr.data);
}

uint8_t mpic_irq_get_priority(int irq)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	return iivpr.priority;
}

void mpic_irq_set_polarity(int irq, uint8_t polarity)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	iivpr.P = polarity;
	mpic_write(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR,iivpr.data);
}

uint8_t mpic_irq_get_polarity(int irq)
{
	iivpr_t iivpr;
	iivpr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR);
	return iivpr.P;
}

static inline void mpic_irq_set_destcpu(int irq, uint8_t destcpu)
{
	uint32_t iidr = 1 << destcpu;
	mpic_write(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIVPR,iidr);
	iidr = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIDR);
}

static inline uint8_t mpic_irq_get_destcpu(int irq)
{
	uint32_t iidr;
	iidr = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IIDR);
	return iidr;
}

/*
 * sets the IILR (interrupt level reg)
 *
 * int type is defined as
 *   0x0 - normal interrupt
 *   0x1 - critical interrupt
 *   0x2 - machine check
 *
 */

void mpic_irq_set_inttype(int irq, uint8_t inttype)
{
	iilr_t iilr;
	iilr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IILR);
	iilr.inttgt = inttype;
	mpic_write(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IILR,iilr.data);
}

uint8_t mpic_irq_get_inttype(int irq)
{
	iilr_t iilr;
	iilr.data = mpic_read(MPIC_IRQ_BASE+(irq*IRQ_STRIDE)+IILR);
	return iilr.inttgt;
}


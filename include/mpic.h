
#ifndef MPIC_H
#define MPIC_H


void mpic_init(unsigned long devtree_ptr);

void mpic_irq_mask(int irq);
void mpic_irq_set_priority(int irq, uint8_t priority);
void mpic_irq_set_vector(int irq, uint32_t vector);
uint32_t mpic_irq_get_vector(int irq);
void mpic_irq_set_priority(int irq, uint8_t priority);
uint8_t mpic_irq_get_priority(int irq);
void mpic_irq_set_polarity(int irq, uint8_t polarity);
uint8_t mpic_irq_get_polarity(int irq);
static inline void mpic_irq_set_destcpu(int irq, uint8_t destcpu);
static inline uint8_t mpic_irq_get_destcpu(int irq);
void mpic_irq_set_inttype(int irq, uint8_t inttype);
uint8_t mpic_irq_get_inttype(int irq);

#endif

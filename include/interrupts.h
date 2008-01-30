#ifndef INTERRUPTS_H
#define INTERRUPTS_H

typedef int (*int_handler_t)(void *arg);
int register_irq_handler(int irq, int_handler_t handler, void *arg);

#endif

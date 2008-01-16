#ifndef INTERRUPTS_H
#define INTERRUPTS_H

typedef void (*int_handler_t)(uint32_t arg);

int32_t register_critical_handler(uint16_t irq, int_handler_t funcptr, uint32_t arg);

#endif

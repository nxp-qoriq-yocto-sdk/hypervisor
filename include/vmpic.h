#ifndef _VMPIC_H_
#define _VMPIC_H_

#include <libos/trapframe.h>
#include <percpu.h>

typedef struct pic_ops_t {
	void (*ops_set_priority)(int hwirq, uint8_t priority);
	void (*ops_set_cpu_dest)(int hwirq, uint8_t cpudest);
	void (*ops_set_polarity)(int hwirq, uint8_t polarity);
	void (*ops_irq_mask)(int hwirq);
	void (*ops_irq_unmask)(int hwirq);
	void (*ops_irq_set_inttype)(int hwirq, uint8_t type);
	void (*ops_set_ctpr) (uint8_t priority);
	int32_t (*ops_get_ctpr) (void);
	void (*ops_eoi)(int);
} pic_ops_t;

/*
 * generic handle to represent all interrupt (hwint,vint) types
 */

typedef struct interrupt_t {
	pic_ops_t *ops;
	int irq;
	handle_t user;
} interrupt_t;

int vmpic_alloc_vpic_handle(guest_t *guest);
void vmpic_global_init(void);
void vmpic_partition_init(guest_t *guest);
void fh_vmpic_set_int_config(trapframe_t *regs);
void fh_vmpic_get_int_config(trapframe_t *regs);
void fh_vmpic_set_mask(trapframe_t *regs);
void fh_vmpic_eoi(trapframe_t *regs);
void fh_vmpic_set_priority(trapframe_t *regs);
void fh_vmpic_get_priority(trapframe_t *regs);

#endif

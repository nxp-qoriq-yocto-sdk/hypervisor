#ifndef _VMPIC_H_
#define _VMPIC_H_

#include <libos/trapframe.h>
#include <percpu.h>

typedef struct pic_ops {
	void (*set_priority)(int hwirq, uint8_t priority);
	void (*set_cpu_dest)(int hwirq, uint8_t cpudest);
	void (*set_polarity)(int hwirq, uint8_t polarity);
	void (*irq_mask)(int hwirq);
	void (*irq_unmask)(int hwirq);
	void (*irq_set_inttype)(int hwirq, uint8_t type);
	void (*set_ctpr) (uint8_t priority);
	int32_t (*get_ctpr) (void);
	void (*eoi)(void);
	uint8_t (*get_priority)(int hwirq);
	uint8_t (*get_polarity)(int hwirq);
	uint8_t (*get_cpu_dest)(int hwirq);
	uint8_t (*irq_get_mask)(int hwirq);
	uint8_t (*irq_get_activity)(int hwirq);
} pic_ops_t;

/*
 * generic handle to represent all interrupt (hwint,vint) types
 */

typedef struct interrupt {
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
void fh_vmpic_iack(trapframe_t *regs);
void fh_vmpic_get_mask(trapframe_t *regs);
void fh_vmpic_get_activity(trapframe_t *regs);

#endif

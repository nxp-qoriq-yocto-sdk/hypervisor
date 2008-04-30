#ifndef _VMPIC_H_
#define _VMPIC_H_

#include <libos/trapframe.h>
#include <percpu.h>

/*
 * generic handle to represent all interrupt (hwint,vint) types
 */

typedef struct vmpic_interrupt {
	struct interrupt *irq;
	handle_t user;
	int handle;
} vmpic_interrupt_t;

int vmpic_alloc_handle(guest_t *guest, interrupt_t *irq);
void vmpic_global_init(void);
void vmpic_partition_init(guest_t *guest);
void fh_vmpic_set_int_config(trapframe_t *regs);
void fh_vmpic_get_int_config(trapframe_t *regs);
void fh_vmpic_set_mask(trapframe_t *regs);
void fh_vmpic_eoi(trapframe_t *regs);
void fh_vmpic_iack(trapframe_t *regs);
void fh_vmpic_get_mask(trapframe_t *regs);
void fh_vmpic_get_activity(trapframe_t *regs);

#endif

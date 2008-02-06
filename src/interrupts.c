
#include <libos/trapframe.h>
#include <libos/libos.h>
#include <libos/bitops.h>
#include <interrupts.h>

extern void trap(trapframe_t *);

struct crit_int_h {
	int_handler_t handler;
	void *arg;
	struct crit_int_h *next;
	int irq;
};

struct crit_int_h *hlist_head = NULL;
struct crit_int_h *hlist_tail = NULL;
static uint32_t link_handler_lock;

int register_irq_handler(int irq, int_handler_t funcptr, void *arg)
{
	struct crit_int_h *ptr;

	ptr = alloc(sizeof(struct crit_int_h),4);
	// FIXME-- check if alloc is out of space 

	ptr->handler = funcptr;
	ptr->arg = arg;
	ptr->next = NULL;

	register_t saved = spin_lock_critsave(&link_handler_lock);

	/* link it in */
	if (hlist_head == NULL) {
		hlist_head = ptr;
		hlist_tail = ptr;
	} else {
		hlist_tail->next = ptr;
		hlist_tail = ptr;
	}

	spin_unlock_critsave(&link_handler_lock, saved);
	return 0;
	
}

void critical_interrupt(trapframe_t *frameptr)
{
	struct crit_int_h *ptr;

	spin_lock(&link_handler_lock);

	for (ptr = hlist_head; ptr != NULL; ptr = ptr->next) {
		if (ptr->handler != NULL) {
			ptr->handler(ptr->arg);
		}
	}

	spin_unlock(&link_handler_lock);
}

void powerpc_mchk_interrupt(trapframe_t *frameptr)
{
#if 0
        printf("powerpc_mchk_interrupt: machine check interrupt!\n");
        dump_frame(framep);
#endif
}

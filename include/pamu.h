#ifndef __PAMU_H_
#define __PAMU_H_

#include <percpu.h>
typedef struct pamu_handle {
	unsigned long assigned_liodn;
	handle_t user;
} pamu_handle_t;

void pamu_global_init(void *fdt);
void pamu_partition_init(guest_t *guest);

int pamu_enable_liodn(unsigned int liodn);
int pamu_disable_liodn(unsigned int liodn);

#endif

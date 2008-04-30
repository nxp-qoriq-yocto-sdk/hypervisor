/** @file
 * ELF image support
 */

#include <percpu.h>
#include <libos/libos.h>

int is_elf(void *image);

int load_elf(guest_t *guest, void *image, unsigned long length,
	phys_addr_t target, phys_addr_t *entry);

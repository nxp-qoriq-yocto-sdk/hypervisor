#include <percpu.h>
#include <vpic.h>

typedef struct guest_recv_dbell_list {
	vint_desc_t guest_vint;
	struct guest_recv_dbell_list *next;
} guest_recv_dbell_list_t;

typedef struct ipi_doorbell {
	struct guest_recv_dbell_list *recv_head;
	uint32_t dbell_lock;
} ipi_doorbell_t;

typedef struct ipi_doorbell_handle {
	ipi_doorbell_t *dbell;
	handle_t user;
} ipi_doorbell_handle_t;

/* Prototypes for functions in ipi_doorbell.c */
void create_doorbells(void);
void send_dbell_partition_init(guest_t *guest);
void recv_dbell_partition_init(guest_t *guest);

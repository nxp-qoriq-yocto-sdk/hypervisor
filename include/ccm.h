#ifndef CCM_H
#define CCM_H

struct guest;
struct dt_node;

typedef struct csd_info {
	uint32_t law_id, csd_id, csd_lock;
} csd_info_t;

void ccm_init(void);
void add_cpus_to_csd(struct guest *guest, struct dt_node *node);
struct dt_node *get_pma_node(struct dt_node *node);

#endif

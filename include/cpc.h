#ifndef CPC_H
#define CPC_H

struct dt_node;
struct dt_prop;

void allocate_cpc_ways(struct dt_prop *prop, uint32_t tgt, uint32_t csdid, struct dt_node *node);

#endif

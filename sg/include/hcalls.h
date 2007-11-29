
#ifndef _HCALLS_H
#define _HCALLS_H

#define FH_CPU_WHOAMI           0x0
#define FH_LPAR_GET_STATUS      0x7

int32_t fh_cpu_whoami(uint32_t *cpu_index);

int32_t fh_lpar_get_status(int lpar_id, uint32_t *lpar_status,
        uint32_t *num_cpus, uint32_t *mem_size);

#endif /* _HCALLS_H */

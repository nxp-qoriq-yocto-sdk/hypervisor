
#include <libos/libos.h>
#include <libos/hcalls.h>

extern void init(void);

void start(void)
{
	int32_t rc;
	uint32_t i;
	uint32_t lpar_stat, num_cpus, mem_size;

	init();

	printf("Main...Start\n");

	rc = fh_cpu_whoami(&i);
	printf("whoami = %x\n",i);

	rc = fh_partition_get_status(5, &lpar_stat, &num_cpus, &mem_size);
	printf("partition_get_status: status= %d, num_cpus=%d, mem_size=%x\n",lpar_stat,num_cpus,mem_size);

	printf("Main..Done!\n");
}

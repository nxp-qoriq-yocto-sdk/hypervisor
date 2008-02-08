
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/trapframe.h>

extern void init(void);

void ext_int_handler(trapframe_t *frameptr)
{
	printf("ext int\n");

}

void start(void)
{
	int32_t rc;
	uint32_t i;
	uint32_t partition_stat, num_cpus, mem_size;

	init();

	printf("Main...Start\n");

	rc = fh_cpu_whoami(&i);
	printf("whoami = %x\n",i);

	rc = fh_partition_get_status(5, &partition_stat, &num_cpus, &mem_size);
	printf("partition_get_status: status= %d, num_cpus=%d, mem_size=%x\n",partition_stat,num_cpus,mem_size);

	printf("Main..Done!\n");
}


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
	unsigned int partition_stat;

	init();

	printf("Main...Start\n");

	rc = fh_cpu_whoami(&i);
	printf("whoami = %x\n",i);

	rc = fh_partition_get_status(5, &partition_stat);
	printf("partition_get_status: status= %d\n", partition_stat);

	printf("Main..Done!\n");
}

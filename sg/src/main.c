
#include "uv.h"
#include "hcalls.h"

int main(void)
{
	int32_t rc;
	uint32_t i;
	uint32_t lpar_stat, num_cpus, mem_size;

	printf("Main...Start\n");

	rc = fh_cpu_whoami(&i);
	printf("whoami = %x\n",i);

	rc = fh_lpar_get_status(5, &lpar_stat, &num_cpus, &mem_size);
	printf("lpar_get_status: status= %d, num_cpus=%d, mem_size=%x\n",lpar_stat,num_cpus,mem_size);

	printf("Main..Done!\n");
}

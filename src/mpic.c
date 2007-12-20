
#include <uv.h>
#include <libos/console.h>
#include <mpic.h>
#include <libos/io.h>

#define CCSRBAR_VA              0x01000000
#define MPIC ((uint32_t *)(CCSRBAR_VA+0x40000))
//#define MPIC ((uint32_t *)(CCSRBAR_VA+0))

void mpic_init(unsigned long devtree_ptr)
{
	uint32_t x;

	x = in32(MPIC);

	printf("mpic %08x\n",x);

}

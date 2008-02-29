
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/mpic.h>
#include <libos/8578.h>
#include <libos/io.h>
#include <libos/spr.h>
#include <libos/trapframe.h>

extern void init(void);

int extint_cnt = 0;;
void ext_int_handler(trapframe_t *frameptr)
{
	uint8_t c;

	extint_cnt++;

	printf("ext int\n");

	c = in8((uint8_t *)(CCSRBAR_VA+0x11d500));
}


void start(void)
{
	uint32_t status;
	char *str;
	uint32_t channel;
	uint32_t x;
	uint8_t c;

	init();

	printf("vmpic test\n");

	/* enable interrupts at the UART */
	out8((uint8_t *)(CCSRBAR_VA+0x11d501),0x1);

	fh_vmpic_set_int_config(0,1,15,0);
	fh_vmpic_set_mask(0, 0);
	fh_vmpic_set_priority(0, 0);

	/* enable external interrupts at the core */
	x = mfmsr();
	x |= (MSR_EE);
	mtmsr(x);

	printf("press keys to generate an ext int\n");
	while (1) {
		c = in8((uint8_t *)(CCSRBAR_VA+0x11d502));
		if (extint_cnt > 1) {
			break;
		}
	}

	printf("done\n");


}



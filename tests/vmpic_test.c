
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/mpic.h>
#include <libos/8578.h>
#include <libos/io.h>
#include <libos/spr.h>

extern void init(void);

extern int extint_cnt;

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

	/* enable interrupts at the PIC */
	mpic_irq_set_inttype(DUART2_IRQ,TYPE_NORM);
	mpic_irq_set_priority(DUART2_IRQ,15);
	mpic_irq_unmask(DUART2_IRQ);

	mpic_irq_set_ctpr(0x0);

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



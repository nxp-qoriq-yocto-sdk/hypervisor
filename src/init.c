#include <libos/console.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/uart.h>
#include <libos/mpic.h>
#include <libos/8578.h>   // FIXME-- remove when UART is moved
#include <libos/ns16550.h>
#include <libos/interrupts.h>
 
#include <hv.h>
#include <percpu.h>
#include <paging.h>
#include <byte_chan.h>
#include <vmpic.h>
#include <bcmux.h>
#include <devtree.h>
#include <gdb-stub.h>
#include <ipi_doorbell.h>
#include <shell.h>

#include <limits.h>

extern cpu_t cpu0;

static gcpu_t noguest = {
	.cpu = &cpu0,   /* link back to cpu */
};

extern uint8_t init_stack_top;

cpu_t cpu0 = {
	.kstack = &init_stack_top - FRAMELEN,
	.client.gcpu = &noguest,
};

static void core_init(void);
static void release_secondary_cores(void);
static void partition_init(void);
void init_guest(void);

#define UART_OFFSET		0x11c500

void *fdt;
static physaddr_t mem_end;
unsigned long CCSRBAR_VA;
void *temp_mapping[2];

void start(unsigned long devtree_ptr)
{
	valloc_init(1024 * 1024, PHYSBASE);
	CCSRBAR_VA = (unsigned long)valloc(16 * 1024 * 1024, 16 * 1024 * 1024);
	temp_mapping[0] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);
	temp_mapping[1] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);

	fdt = (void *)(devtree_ptr + PHYSBASE);
	mem_end = find_end_of_mem();
	core_init();

	unsigned long heap = (unsigned long)fdt + fdt_totalsize(fdt);
	heap = (heap + 15) & ~15;

	alloc_init(heap, mem_end + PHYSBASE);
	mpic_init((unsigned long)fdt);

#ifdef CONFIG_LIBOS_NS16550
	create_ns16550();
#endif
	open_stdout();
	printf("mem_end %llx\n", mem_end);

	printf("=======================================\n");
	printf("Freescale Ultravisor 0.1\n");

	vmpic_global_init();

#ifdef CONFIG_SHELL
	shell_init();
#endif

	/* byte channel initialization */
#ifdef CONFIG_BYTE_CHAN
	create_byte_channels();
#ifdef CONFIG_BCMUX
	create_muxes();
#endif
	connect_global_byte_channels();
#endif

#ifdef CONFIG_IPI_DOORBELL
	create_doorbells();
#endif

#ifdef CONFIG_GDB_STUB
	gdb_stub_init();
#endif

	enable_critint();

	// FIXME pamu init

	/* Main device tree must be const after this point. */
	release_secondary_cores();
	partition_init();
}

void secondary_init(void)
{
	core_init();
	mpic_reset_core();
	enable_critint();
	partition_init();
}

static void release_secondary_cores(void)
{
	int node = fdt_subnode_offset(fdt, 0, "cpus");
	int depth = 0;

	if (node < 0) {
		printf("Missing /cpus node\n");
		goto fail;
	}

	while ((node = fdt_next_node(fdt, node, &depth)) >= 0) {
		int len;
		const char *status;
		
		if (node < 0)
			break;
		if (depth > 1)
			continue;
		if (depth < 1)
			return;
		
		status = fdt_getprop(fdt, node, "status", &len);
		if (!status) {
			if (len == -FDT_ERR_NOTFOUND)
				continue;

			node = len;
			goto fail_one;
		}

		if (len != strlen("disabled") + 1 || strcmp(status, "disabled"))
			continue;

		const char *enable = fdt_getprop(fdt, node, "enable-method", &len);
		if (!status) {
			printf("Missing enable-method on disabled cpu node\n");
			node = len;
			goto fail_one;
		}

		if (len != strlen("spin-table") + 1 || strcmp(enable, "spin-table")) {
			printf("Unknown enable-method \"%s\"; not enabling\n", enable);
			continue;
		}

		const uint32_t *reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg) {
			printf("Missing reg property in cpu node\n");
			node = len;
			goto fail_one;
		}

		if (len != 4) {
			printf("Bad length %d for cpu reg property; core not released\n",
			       len);
			return;
		}

		const uint64_t *table = fdt_getprop(fdt, node, "cpu-release-addr", &len);
		if (!table) {
			printf("Missing cpu-release-addr property in cpu node\n");
			node = len;
			goto fail_one;
		}

		printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
		         "starting cpu %u, table %llx\n", *reg, *table);

		tlb1_set_entry(TEMPTLB1, (unsigned long)temp_mapping[0],
		               (*table) & ~(PAGE_SIZE - 1),
		               TLB_TSIZE_4K, TLB_MAS2_IO,
		               TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

		char *table_va = temp_mapping[0];
		table_va += *table & (PAGE_SIZE - 1);

		cpu_t *cpu = alloc(sizeof(cpu_t), __alignof__(cpu_t));
		if (!cpu)
			goto nomem;

		cpu->kstack = alloc(KSTACK_SIZE, 16);
		if (!cpu->kstack)
			goto nomem;

		cpu->kstack += KSTACK_SIZE - FRAMELEN;

		cpu->client.gcpu = alloc(sizeof(gcpu_t), __alignof__(gcpu_t));
		if (!cpu->client.gcpu)
			goto nomem;

		cpu->client.gcpu->cpu = cpu;  /* link back to cpu */

		if (start_secondary_spin_table((void *)table_va, *reg, cpu))
			printf("couldn't spin up CPU%u\n", *reg);

next_core:
		;
	}

fail:
	printf("error %d (%s) reading CPU nodes, "
	       "secondary cores may not be released.\n",
	       node, fdt_strerror(node));

	return;
 
nomem:
	printf("out of memory reading CPU nodes, "
	       "secondary cores may not be released.\n");

	return;

fail_one:
	printf("error %d (%s) reading CPU node, "
	       "this core may not be released.\n",
	       node, fdt_strerror(node));

	goto next_core;
}

static void partition_init(void)
{
	init_guest();
	BUG();
}

static void core_init(void)
{
	/* set up a TLB entry for CCSR space */
	tlb1_init();

	/* PIR was set by firmware-- record in cpu_t struct */
	cpu->coreid = mfspr(SPR_PIR);

	/* dec init sequence 
	 *  -disable DEC interrupts
	 *  -disable DEC auto reload
	 *  -set DEC to zero 
	 *  -clear any pending DEC interrupts */
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_DIE);
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_ARE);
	mtspr(SPR_DEC, 0x0);
	mtspr(SPR_TSR, TSR_DIS);

	mtspr(SPR_EHCSR,
	      EHCSR_EXTGS | EHCSR_DTLBGS | EHCSR_ITLBGS |
	      EHCSR_DSIGS | EHCSR_ISIGS | EHCSR_DUVD);

	mtspr(SPR_HID0, HID0_EMCP | HID0_DPM | HID0_TBEN);
}

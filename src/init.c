#include <libos/console.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/uart.h>
#include <libos/mpic.h>
#include <libos/8578.h>   // FIXME-- remove when UART is moved
#include <libos/ns16550.h>
 
#include <hv.h>
#include <percpu.h>
#include <paging.h>
#include <interrupts.h>
#include <byte_chan.h>

#include <libfdt.h>
#include <limits.h>

static gcpu_t noguest = {
	// FIXME 64-bit
	.tlb1_free = { ~0UL, ~0UL },
};

extern uint8_t init_stack_top;

cpu_t cpu0 = {
	.kstack = &init_stack_top - FRAMELEN,
	.client.gcpu = &noguest,
};

static void core_init(void);
static void release_secondary_cores(void);
static void partition_init(void);
void start_guest(void);

#define UART_OFFSET		0x11c500

void *fdt;
static physaddr_t mem_end;

// FIXME: memory holes
static void find_end_of_mem(void)
{
	int memnode = fdt_subnode_offset(fdt, 0, "memory");
	if (memnode < 0) {
		printf("error %d (%s) opening /memory\n", memnode,
		       fdt_strerror(memnode));

		return;
	}

	int len;
	const uint32_t *memreg = fdt_getprop(fdt, memnode, "reg", &len);
	if (!memreg) {
		printf("error %d (%s) reading /memory/reg\n", memnode,
		       fdt_strerror(memnode));

		return;
	}

	uint32_t naddr, nsize;
	int ret = get_addr_format(fdt, memnode, &naddr, &nsize);
	if (ret < 0) {
		printf("error %d (%s) getting address format for /memory\n",
		       ret, fdt_strerror(ret));

		return;
	}

	if (naddr < 1 || naddr > 2 || nsize < 1 || nsize > 2) {
		printf("bad address format %u/%u for /memory\n", naddr, nsize);
		return;
	}

	const uint32_t *reg = memreg;
	while (reg + naddr + nsize <= memreg + len / 4) {
		physaddr_t addr = *reg++;
		if (naddr == 2) {
			addr <<= 32;
			addr |= *reg++;
		}

		physaddr_t size = *reg++;
		if (nsize == 2) {
			size <<= 32;
			size |= *reg++;
		}

		addr += size - 1;
		if (addr > mem_end)
			mem_end = addr;
	}
}

void start(unsigned long devtree_ptr)
{
	fdt = (void *)(devtree_ptr + PHYSBASE);
	find_end_of_mem();
	core_init();

	unsigned long heap = (unsigned long)fdt + fdt_totalsize(fdt);
	heap = (heap + 15) & ~15;

	alloc_init(heap, mem_end + PHYSBASE);

	chardev_t *console = ns16550_init((uint8_t *)CCSRBAR_VA + UART_OFFSET,
	                                  0, 0, 16);
	console_init(console);

	printf("mem_end %llx\n", mem_end);

	printf("=======================================\n");
	printf("Freescale Ultravisor 0.1\n");

	mpic_init((unsigned long)fdt);

// FIXME-- need mpic_irq_setup func
	mpic_irq_set_inttype(0x24, TYPE_CRIT);
	mpic_irq_set_priority(0x24, 15);
	mpic_irq_unmask(0x24);

	/* byte channel initialization */
	byte_chan_global_init();

	enable_critint();

	// pamu init

	release_secondary_cores();
	partition_init();
}

static void secondary_init(void)
{
	core_init();
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

	while ((node = fdt_get_next_node(fdt, node, &depth, 0)) >= 0) {
		int len;
		const char *status = fdt_getprop(fdt, node, "status", &len);
		if (!status) {
			if (len == -FDT_ERR_NOTFOUND)
				continue;

			node = len;
			goto fail;
		}

		if (len != strlen("disabled") + 1 || strcmp(status, "disabled"))
			continue;

		const char *enable = fdt_getprop(fdt, node, "enable-method", &len);
		if (!status) {
			printf("Missing enable-method on disabled cpu node\n");
			node = len;
			goto fail;
		}

		if (len != strlen("spin-table") + 1 || strcmp(enable, "spin-table")) {
			printf("Unknown enable-method \"%s\"; not enabling\n", enable);
			continue;
		}

		const uint32_t *reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg) {
			printf("Missing reg property in cpu node\n");
			node = len;
			goto fail;
		}

		if (len != 4) {
			printf("Bad length %d for cpu reg property; core not released\n",
			       len);
			return;
		}

		const uint32_t *table = fdt_getprop(fdt, node, "cpu-release-addr", &len);
		if (!table) {
			printf("Missing cpu-release-addr property in cpu node\n");
			node = len;
			goto fail;
		}

		printf("starting cpu %u, table %x\n", *reg, *table);

		tlb1_set_entry(BASE_TLB_ENTRY + 1, CCSRBAR_VA - PAGE_SIZE,
		               (*table) & ~(PAGE_SIZE - 1),
		               PAGE_SIZE, TLB_MAS2_IO,
		               TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

		uintptr_t table_va = *table & (PAGE_SIZE - 1);
		table_va |= CCSRBAR_VA - PAGE_SIZE;

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

		// FIXME 64-bit
		cpu->client.gcpu->tlb1_free[0] = ~0UL;
		cpu->client.gcpu->tlb1_free[1] = ~0UL;

		if (start_secondary_spin_table((void *)table_va, *reg, cpu,
		                               secondary_init, NULL))
			printf("couldn't spin up CPU%u\n", *reg);

next_core:
		;
	}

	if (node != -FDT_ERR_NOTFOUND) {
		printf("error getting child\n");
		goto fail;
	}

	return;

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
	// -init/alloc partition data structure
	// -identify partition node in device tree for
	//  this partition, do partition init
	//    -configure all interrupts-- irq#,cpu#
	//    -add entries to PAMU table
	// -create guest device tree

	start_guest();
}

static void core_init(void)
{
	/* set up a TLB entry for CCSR space */
	tlb1_init();

	mtspr(SPR_EHCSR,
	      EHCSR_EXTGS | EHCSR_DTLBGS | EHCSR_ITLBGS |
	      EHCSR_DSIGS | EHCSR_ISIGS | EHCSR_DUVD);
}


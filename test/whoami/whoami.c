
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/bitops.h>
#include <libfdt.h>

#define PAGE_SIZE 4096

void init(unsigned long devtree_ptr);

int irq;
extern void *fdt;
extern int start_secondary_spin_table(struct boot_spin_table *table, int num,
				      cpu_t *cpu);

void *temp_mapping[2];

unsigned long devtree;

void ext_int_handler(trapframe_t *frameptr)
{
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

		const char *enable =
		    fdt_getprop(fdt, node, "enable-method", &len);
		if (!status) {
			printf("Missing enable-method on disabled cpu node\n");
			node = len;
			goto fail_one;
		}

		if (len != strlen("spin-table") + 1
		    || strcmp(enable, "spin-table")) {
			printf("Unknown enable-method \"%s\"; not enabling\n",
			       enable);
			continue;
		}

		const uint32_t *reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg) {
			printf("Missing reg property in cpu node\n");
			node = len;
			goto fail_one;
		}

		if (len != 4) {
			printf
			    ("Bad length %d for cpu reg property; core not released\n",
			     len);
			return;
		}

		const uint64_t *table =
		    fdt_getprop(fdt, node, "cpu-release-addr", &len);
		if (!table) {
			printf
			    ("Missing cpu-release-addr property in cpu node\n");
			node = len;
			goto fail_one;
		}

		printf(" > started cpu %u, table %llx PASSED\n", *reg, *table);

		tlb1_set_entry(1, (unsigned long)temp_mapping[0],
			       (*table) & ~(PAGE_SIZE - 1),
			       TLB_TSIZE_4K, TLB_MAS2_IO,
			       TLB_MAS3_KERN, 0, 0, 0);

		char *table_va = temp_mapping[0];
		table_va += *table & (PAGE_SIZE - 1);
		cpu_t *cpu = alloc_type(cpu_t);
		if (!cpu)
			goto nomem;

		cpu->kstack = alloc(KSTACK_SIZE, 16);
		if (!cpu->kstack)
			goto nomem;

		cpu->kstack += KSTACK_SIZE - FRAMELEN;

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
	       "this core may not be released.\n", node, fdt_strerror(node));

	goto next_core;
}
void dump_dev_tree(void)
{
#ifdef DEBUG
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("   node = %s\n", s);
	}
	printf("------\n");
#endif
}

#define CPUCNT 4
int cpus_complete[CPUCNT];

void secondary_entry(void)
{
	uint32_t cpu_index;
	uint32_t pir = mfspr(SPR_PIR);

	fh_cpu_whoami(&cpu_index);
	printf(" > secondary cpu start-- PIR=%d, fh_whoami=%d: ", pir, cpu_index);
	if (cpu_index == pir) {
		cpus_complete[pir] = 1;
		printf("PASSED\n");
	} else {
		printf("FAILED\n");
	}
}

extern void (*secondary_startp)(void);

void start(unsigned long devtree_ptr)
{
	int i;
	int done = 0;
	uint32_t cpu_index;
	uint32_t pir = mfspr(SPR_PIR);

	devtree = devtree_ptr;

	secondary_startp = secondary_entry;

	init(devtree);

	dump_dev_tree();

	enable_extint();
	temp_mapping[0] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);
	temp_mapping[1] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);

	printf("whoami test\n");
	release_secondary_cores();
	fh_cpu_whoami(&cpu_index);
	printf(" > boot cpu start-- PIR=%d, fh_whoami=%d: ", pir, cpu_index);
	if (cpu_index == pir) {
		cpus_complete[pir] = 1;
		printf("PASSED\n");
	} else {
		printf("FAILED\n");
	}

	while (done != CPUCNT) {
		done = 0;
		for (i=0; i < CPUCNT; i++) {
			done += cpus_complete[i];
		}
	}

	printf("Test Complete\n");

}

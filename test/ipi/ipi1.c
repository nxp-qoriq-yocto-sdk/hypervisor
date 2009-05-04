/*
 * Copyright (C) 2008 - 2009 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <libos/alloc.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>
#include <hvtest.h>

#undef DEBUG

static volatile int extint_cnt;
static const uint32_t *handle_p_int;
 

/* reads data from shared memory region on interrupt */
static void rd_shm(void)
{
	int ret, off = -1;
	phys_addr_t addr = 0;
	void *vaddr;
	int len;
	char buf[20];
	ret = fdt_node_offset_by_compatible(fdt, off, "fsl,share-mem-test");
	if (ret == -FDT_ERR_NOTFOUND) {
		printf("fdt_node_offset failed, NOT FOUND\n");
		return;
	}
	if (ret < 0) {
		printf("fdt_node_offset failed = %d\n", ret);
		return;
	}
	off = ret;
	const uint32_t *reg = fdt_getprop(fdt, off, "reg", &len);
	if (!reg)
		printf("get prop failed for reg in share =%d\n", len);
	/* assuming address_cells = 2 and size_cells = 1 */
	addr |= ((uint64_t) *reg) << 32;
	reg++;
	addr |= *reg;
#ifdef DEBUG
	printf("shared memory addr = %llx\n", addr);
#endif
	vaddr = valloc(4 * 1024, 4 * 1024);
	if (!vaddr) {
		printf("valloc failed \n");
		return;
	}
	tlb1_set_entry(1, (unsigned long)vaddr, addr, TLB_TSIZE_4K, TLB_MAS2_IO, TLB_MAS3_KERN, 0, 0, 0);
	memcpy(buf, vaddr, strlen("hello") + 1);
#ifdef DEBUG
	printf("read '%s' from shared memory on interrupt \n", buf);
	printf("expected 'hello', got '%s'\n", buf);
#endif
	if (!strcmp(buf, "hello"))
		printf(" > received expected value in shared memory: PASSED\n");
	else
		printf(" > got wrong value in sh mem: FAILED\n");
}

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int irq;

	if (coreint)
		irq = mfspr(SPR_EPR);
	else
		fh_vmpic_iack(&irq);
	
	if (irq != *handle_p_int) {
		printf("Unknown extirq %d\n", irq);
	} else {
		extint_cnt++;
		printf(" > got an external interrupt: PASSED\n");
		rd_shm();
	}

	fh_vmpic_eoi(irq);
}

void ext_doorbell_handler(trapframe_t *frameptr)
{
	printf("Doorbell\n");
}

void ext_critical_doorbell_handler(trapframe_t *frameptr)
{
	printf("Critical doorbell\n");
}

static int test_init(void)
{
	int len;
	int off = -1, ret;

	ret = fdt_check_header(fdt);
	if (ret)
		return -1;
	ret = fdt_node_offset_by_compatible(fdt, off, "fsl,hv-doorbell-receive-handle");
	if (ret == -FDT_ERR_NOTFOUND)
		return ret;
	if (ret < 0)
		return ret;
	off = ret;
	handle_p_int = fdt_getprop(fdt, off, "interrupts", &len);

	/* VMPIC config */
	fh_vmpic_set_int_config(*handle_p_int, 1, 15, 0x00000001);
	fh_vmpic_set_mask(*handle_p_int, 0);
	 /*VMPIC*/ enable_critint();
	enable_extint();

	return 0;
}

static void dump_dev_tree(void)
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

void libos_client_entry(unsigned long devtree_ptr)
{
	int rc;

	init(devtree_ptr);

	printf("inter-partition doorbell test #2\n");

	dump_dev_tree();

	rc = test_init();
	if (rc) {
		printf("error: test init failed\n");
	}

	while (1) {
		if (extint_cnt) {
			break;
		}
	}

	printf("Test Complete\n");
}

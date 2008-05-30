
#include <libos/libos.h>
#include <libos/bitops.h>

#include <percpu.h>

#include <errors.h>
#include <devtree.h>

#define QCSP_SDEST_SHIFT 24
#define QCSP_LPID_SHIFT 16

static int get_coreid(int cpu_num, const uint32_t *cpus, int len)
{
	int i, num, base, oldnum = 0;

	for (i = 0; i < len / 4; i += 2) {
		base = cpus[i];
		num = cpus[i + 1];
		if (cpu_num < (num + oldnum)) {
			if (base == 0)
				return (base + cpu_num);
			else
				return (base + (cpu_num - oldnum));
		}
		oldnum += num;
	}
	return -1;
}

static const uint32_t *get_qman_base(void)
{
	int ret, off = -1;
	const uint32_t *ptr;

	ret = fdt_node_offset_by_compatible(fdt, off, "fsl,8578-qman");
	if (ret < 0)
		return NULL;

	ptr = fdt_getprop(fdt, ret, "reg", &ret);
	if (!ptr)
		return NULL;

	return &ptr[0];
}

void data_path_partition_init(guest_t *guest, const uint32_t *cpus, int len)
{
	int ret, off = -1, coreid;
	const uint32_t *portal_index, *cpu_phandle, *cpu_num, *qman_base;
	uint32_t *ptr, *qcsp_reg;
	int lpid = guest->lpid;

	/*Qman CCSR register base*/
	qman_base = get_qman_base();
	if (!qman_base)
		return;

	ptr = (uint32_t *)(CCSRBAR_VA + *qman_base);

	while (1) {
		ret = fdt_node_offset_by_compatible(guest->devtree, off,
							"fsl,qman-portal");
		if (ret < 0)
			break;

		off = ret;

		portal_index = fdt_getprop(guest->devtree, off, "cell-index", &ret);
		if (!portal_index)
			break;

		cpu_phandle = fdt_getprop(guest->devtree, off, "fsl,stashing-dest",
						&ret);
		if (!cpu_phandle)
			break;

		ret = fdt_node_offset_by_phandle(guest->devtree, *cpu_phandle);
		if (ret < 0)
			break;

		cpu_num = fdt_getprop(guest->devtree, ret, "reg", &ret);
		if (!cpu_num)
			break;

		coreid = get_coreid(*cpu_num, cpus, len);
		if (ret < 0)
			break;

		if ((*portal_index >= 0) && (*portal_index <= 9))
			qcsp_reg = ptr + (16 * (*portal_index));
		else
			break;

		int val;
		val = in32(qcsp_reg);
		/* LIODN may be set already */
		val |= (coreid << QCSP_SDEST_SHIFT) | (lpid << QCSP_LPID_SHIFT);
		out32(qcsp_reg, val);
		val = in32(qcsp_reg);
		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
				"data_path_partition_init val set as = %x\n", val);
	}
}

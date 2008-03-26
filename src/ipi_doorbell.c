#include <libos/libos.h>
#include <libos/bitops.h>

#include <ipi_doorbell.h>
#include <errors.h>
#include <devtree.h>

#include <string.h>
#include <vpic.h>
#include <vmpic.h>

ipi_doorbell_t *doorbell_alloc()
{
	ipi_doorbell_t *ret;

	ret = alloc_type(ipi_doorbell_t);
	if (!ret)
		return NULL;

	return ret;
}

void create_doorbells(void)
{
	int off = -1, ret;
	ipi_doorbell_t *dbell;

	while (1) {
		ret =
		    fdt_node_offset_by_compatible(fdt, off, "fsl,hv-doorbell");
		if (ret < 0)
			break;
		dbell = doorbell_alloc();
		if (!dbell) {
			printf
			    ("doorbell_global_init: failed to create doorbell.\n");
			return;
		}

		off = ret;
		ret =
		    fdt_setprop(fdt, off, "fsl,hv-internal-doorbell-ptr",
				&dbell, sizeof(dbell));
		if (ret < 0)
			break;
	}
	if (ret == -FDT_ERR_NOTFOUND)
		return;

	printf("create_doorbells: libfdt error %d (%s).\n",
	       ret, fdt_strerror(ret));

}

int doorbell_attach_guest(ipi_doorbell_t *dbell, guest_t *guest)
{
	struct ipi_doorbell_handle *db_handle = NULL;

	db_handle = alloc_type(struct ipi_doorbell_handle);
	db_handle->user.db = db_handle;

	int ghandle = alloc_guest_handle(guest, &db_handle->user);
	if (ghandle < 0)
		return ERR_NOMEM;

	db_handle->dbell = dbell;

	return ghandle;
}

void send_dbell_partition_init(guest_t *guest)
{
	int off = -1, ret;
	const char *endpoint;
	ipi_doorbell_t *dbell;

	while (1) {
		ret = fdt_node_offset_by_compatible(guest->devtree, off,
							"fsl,doorbell-send");
		if (ret == -FDT_ERR_NOTFOUND)
			return;
		if (ret < 0)
			break;

		off = ret;

		endpoint =
		    fdt_getprop(guest->devtree, off, "fsl,endpoint", &ret);
		if (!endpoint)
			break;
		ret = lookup_alias(fdt, endpoint);
		if (ret < 0)
			break;

		/*Get the pointer corresponding to hv-internal-doorbell-ptr */
		dbell = ptr_from_node(fdt, ret, "doorbell");
		if (!dbell) {
			printf("send_doorbell_partition_init: no pointer\n");
			continue;
		}

		int32_t ghandle =
		    doorbell_attach_guest(dbell, guest);
		if (ghandle < 0) {
			printf("send_doorbell_partition_init: cannot attach\n");
			return;
		}

		printf("send_dbell_partition_init : guest handle =%d\n",
			ghandle);

		ret = fdt_setprop(guest->devtree, off, "reg", &ghandle, 4);
		if (ret < 0)
			break;
	}

}

void recv_dbell_partition_init(guest_t *guest)
{
	int off = -1, ret;
	const char *endpoint;
	guest_recv_dbell_list_t *dbell_node;
	register_t saved;
	uint32_t irq[2], irq1;
	ipi_doorbell_t *dbell;

	while (1) {
		ret = fdt_node_offset_by_compatible(guest->devtree, off,
							"fsl,doorbell-receive");
		if (ret == -FDT_ERR_NOTFOUND)
			return;
		if (ret < 0)
			break;

		off = ret;

		endpoint =
		    fdt_getprop(guest->devtree, off, "fsl,endpoint", &ret);
		if (!endpoint)
			break;
		ret = lookup_alias(fdt, endpoint);
		if (ret < 0)
			break;

		/*Get the pointer corresponding to hv-internal-doorbell-ptr */
		dbell = ptr_from_node(fdt, ret, "doorbell");
		if (!dbell) {
			printf("recv_doorbell_partition_init: no pointer\n");
			continue;
		}

		dbell_node = alloc_type(guest_recv_dbell_list_t);
		if (!dbell_node)
			return;

		irq[0] = vmpic_alloc_vpic_handle(guest);
		irq[1] = 2;
		irq1 = guest->handles[irq[0]]->intr->irq;
		printf("irq1 value to be assigned to vint->irq =%d\n", irq1);

		dbell_node->guest_vint.vpic_irq = irq1;
		dbell_node->guest_vint.guest = guest;
		dbell_node->next = NULL;

		saved = spin_lock_critsave(&dbell->dbell_lock);
		if (dbell->recv_head == NULL) {
			dbell->recv_head = dbell_node;
		} else {
			dbell_node->next = dbell->recv_head;
			dbell->recv_head = dbell_node;
		}
		spin_unlock_critsave(&dbell->dbell_lock, saved);

		ret =
		    fdt_setprop(guest->devtree, off, "interrupts", irq,
				sizeof(irq));
		if (ret < 0)
			break;
	}
}

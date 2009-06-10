/* Copyright (C) 2009 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>

#include <asm/atomic.h>
#include <asm/io.h>

static irqreturn_t virt_pcie_msi_isr(int irq, void *data)
{
	printk("virt_pcie_msi_isr invoked\n");
	return IRQ_HANDLED;
}

static int __devinit virt_pcie_msi_probe(struct pci_dev *pdev,
	const struct pci_device_id *pci_id)
{
	int retval;
	void __iomem *caddr;

	printk("virt_pcie_msi device probe\n");

	retval = pci_enable_device(pdev);
	if (retval)
		goto err;

	retval = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	printk("express, retval = %d\n", retval);

	retval = pci_find_capability(pdev, PCI_CAP_ID_MSI);
	printk("msi, retval = %d\n", retval);

	retval = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	printk("msix, retval = %d\n", retval);

	retval = pci_enable_msi(pdev);
	if (retval) {
		dev_err(&pdev->dev, "msi init failed\n");
		goto err;
	}

	retval = pci_request_regions(pdev, "virt_pcie_msi");
	if (retval)
		goto err;

	caddr = pci_iomap(pdev, 0, 0);
	if (caddr == NULL) {
		dev_err(&pdev->dev, "can't remap io space\n");
		goto err;
	}

	retval = request_irq(pdev->irq, virt_pcie_msi_isr,
			0, "virt_pcie_msi", NULL);
	if (retval) {
		dev_err(&pdev->dev, "can't establish ISR\n");
		goto err;
	}

	iowrite32(1, caddr);

	return 0;
err:
	return retval;
}

static struct pci_device_id virt_pcie_msi_pci_tbl[] __devinitdata = {
	{ .vendor = 0x4711, .device = 0x1174 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, virt_pcie_msi_pci_tbl);

static struct pci_driver virt_pcie_msi_pci_driver = {
	.name = "virt_pcie_msi",
	.id_table = virt_pcie_msi_pci_tbl,
	.probe = virt_pcie_msi_probe,
};

static int __init virt_pcie_msi_init(void)
{
	int retval;

	retval = pci_register_driver(&virt_pcie_msi_pci_driver);
	if (retval) {
		printk(KERN_ERR "virt_pcie_msi: can't register pci driver\n");
		return retval;
	}

	printk(KERN_INFO "Virtutech PCIe MSI driver\n");

	return 0;
}

static void __exit virt_pcie_msi_exit(void)
{
	pci_unregister_driver(&virt_pcie_msi_pci_driver);
}

module_init(virt_pcie_msi_init);
module_exit(virt_pcie_msi_exit);

MODULE_AUTHOR("ashish.kalra@freescale.com");
MODULE_DESCRIPTION("Dummy PCIe endpoint device with MSI support");
MODULE_LICENSE("GPL");

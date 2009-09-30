/** @file
 * NS16550 glue code to libos driver
 */
/* Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
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

#include <libos/libos.h>
#include <libos/interrupts.h>
#include <libos/ns16550.h>

#include <errors.h>
#include <devtree.h>

struct chardev;

static int ns16550_probe(driver_t *drv, device_t *dev);

static driver_t __driver ns16550 = {
	.compatible = "ns16550",
	.probe = ns16550_probe
};

static int ns16550_probe(driver_t *drv, device_t *dev)
{
	struct chardev *cd;
	interrupt_t *irq = NULL;
	dt_prop_t *prop;
	dt_node_t *node = to_container(dev, dt_node_t, dev);
	dev_owner_t *owner = dt_owned_by(node, NULL);
	uint64_t freq = 0;
	uint32_t baud = 115200;

	if (dev->num_regs < 1 || !dev->regs[0].virt)
		return ERR_INVALID;

	if (dev->num_irqs >= 1)
		irq = dev->irqs[0];

	prop = dt_get_prop(node, "clock-frequency", 0);
	if (prop && prop->len == 4) {
		freq = *(const uint32_t *)prop->data;
	} else if (prop && prop->len == 8) {
		freq = *(const uint64_t *)prop->data;
	} else {
		printlog(LOGTYPE_DEV, LOGLEVEL_ERROR,
		         "%s: bad/missing clock-frequency in %s\n",
		         __func__, node->name);
	}

	prop = dt_get_prop(node, "current-speed", 0);
	if (prop) {
		if (prop->len == 4)
			baud = *(const uint32_t *)prop->data;
		else
			printlog(LOGTYPE_DEV, LOGLEVEL_ERROR,
			         "%s: bad current-speed property in %s\n",
			         __func__, node->name);
	}

	if (owner) {
		prop = dt_get_prop(owner->cfgnode, "baud", 0);
		if (prop) {
			if (prop->len == 4)
				baud = *(const uint32_t *)prop->data;
			else
				printlog(LOGTYPE_DEV, LOGLEVEL_ERROR,
				         "%s: bad baud property in %s\n",
				         __func__, owner->cfgnode->name);
		}
	} else {
		printlog(LOGTYPE_DEV, LOGLEVEL_ERROR,
		         "%s: %s not owned by hypervisor?\n",
		         __func__, node->name);
	}

	cd = ns16550_init(dev->regs[0].virt, irq, freq, 16, baud);

	dev->driver = &ns16550;
	dev->chardev = cd;
	return 0;
}

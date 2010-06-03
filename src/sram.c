/** @file
 * SOC SRAM
 */
/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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

#include <libos/platform_error.h>

#include <percpu.h>
#include <error_log.h>
#include <error_mgmt.h>

static const char *sram_err_policy;

static int sram_probe(driver_t *drv, device_t *dev);

static driver_t __driver sram = {
	.compatible = "fsl,soc-sram-error",
	.probe = sram_probe
};

static int sram_error_isr(void *arg)
{
	hv_error_t err = { };
	device_t *dev = arg;
	dt_node_t *sram_node;

	sram_node = to_container(dev, dt_node_t, dev);
	strncpy(err.domain, get_domain_str(error_misc), sizeof(err.domain));
	strncpy(err.error, get_error_str(error_misc, internal_ram_multi_bit_ecc), sizeof(err.error));
	dt_get_path(NULL, sram_node, err.hdev_tree_path, sizeof(err.hdev_tree_path));
	error_policy_action(&err, error_misc, sram_err_policy);

	return 0;
}

static int sram_probe(driver_t *drv, device_t *dev)
{
	printlog(LOGTYPE_MISC, LOGLEVEL_DEBUG, "SOC SRAM probe routine\n");

	if (dev->num_irqs >= 1) {
		interrupt_t *irq = dev->irqs[0];
		if (irq && irq->ops->register_irq) {
			sram_err_policy = get_error_policy(error_misc, internal_ram_multi_bit_ecc);
			if (strcmp(sram_err_policy, "disable"))
				irq->ops->register_irq(irq, sram_error_isr, dev, TYPE_MCHK);
		}
	}

	dev->driver = &sram;

	return 0;
}

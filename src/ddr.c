/** @file
 * Memory controller
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
#include <errors.h>
#include <error_log.h>
#include <error_mgmt.h>

static int ddr_probe(driver_t *drv, device_t *dev);

static driver_t __driver ddr = {
	.compatible = "fsl,p4080-memory-controller",
	.probe = ddr_probe
};

static int ddr_error_isr(void *arg)
{
	hv_error_t err = { };
	device_t *dev = arg;
	interrupt_t *irq;

	irq = dev->irqs[0];
	irq->ops->disable(irq);

	strncpy(err.domain, get_domain_str(error_ddr), sizeof(err.domain));
	strcpy(err.error, get_domain_str(error_ddr));

	error_log(&hv_global_event_queue, &err, &hv_queue_prod_lock);

	return 0;
}

static int ddr_probe(driver_t *drv, device_t *dev)
{
	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_DDR, LOGLEVEL_ERROR,
				"DDR reg initialization failed\n");
		return ERR_INVALID;
	}

	if (dev->num_irqs >= 1) {
		interrupt_t *irq = dev->irqs[0];
		if (irq && irq->ops->register_irq)
			irq->ops->register_irq(irq, ddr_error_isr, dev, TYPE_MCHK);
	}

	dev->driver = &ddr;

	return 0;
}

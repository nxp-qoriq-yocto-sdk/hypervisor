/** @file
 * Global Utilities driver code
 */

/*
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
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

#include <libos/printlog.h>
#include <libos/io.h>

#include <guts.h>
#include <devtree.h>
#include <errors.h>

#define GUTS_RSTCR 0xB0       /* offset in guts dev config reset control reg */
#define RSTCR_RESET_REQ 0x2

static uint32_t *guts_rstcr;

static int guts_devconfig_probe(driver_t *drv, device_t *dev);

static driver_t __driver guts_devconfig = {
	.compatible = "fsl,qoriq-device-config-1.0",
	.probe = guts_devconfig_probe
};

static int guts_devconfig_probe(driver_t *drv, device_t *dev)
{
	dt_prop_t *prop;
	dt_node_t *node = to_container(dev, dt_node_t, dev);

	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				"guts device config initialization failed\n");
		return ERR_INVALID;
	}

	guts_rstcr = (uint32_t *) ((uintptr_t)dev->regs[0].virt + GUTS_RSTCR);

	dev->driver = &guts_devconfig;

	return 0;
}

int system_reset(void)
{
	if (!guts_rstcr) {
		printlog(LOGTYPE_GUTS, LOGLEVEL_ERROR,
		        "%s: reset control reg not found\n", __func__);
		return ENODEV;
	}

	out32(guts_rstcr, RSTCR_RESET_REQ);

	// FIXME: timeout here if the reset doesn't happen
	// and return an error
	while (1);
}

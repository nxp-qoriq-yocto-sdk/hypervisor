/** @file
 * Global Utilities driver code
 */

/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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

static uint32_t *guts_rstcr;  /* reset control register */
static uint32_t *guts_crstr0;  /* core reset status register */
static uint32_t *guts_rstrqmr;  /* reset request mask register */

static int guts_devconfig_probe(driver_t *drv, device_t *dev);

static driver_t __driver guts_devconfig = {
	.compatible = "fsl,qoriq-device-config-1.0",
	.probe = guts_devconfig_probe
};

static int guts_devconfig_probe(driver_t *drv, device_t *dev)
{
	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				"guts device config initialization failed\n");
		return ERR_INVALID;
	}

	guts_rstcr = (uint32_t *) ((uintptr_t)dev->regs[0].virt + GUTS_RSTCR);
	guts_crstr0 = (uint32_t *) ((uintptr_t)dev->regs[0].virt + GUTS_CRSTR0);
	guts_rstrqmr = (uint32_t *) ((uintptr_t)dev->regs[0].virt + GUTS_RSTRQMR);

	dev->driver = &guts_devconfig;

	return 0;
}

/**
 * returns the system hardware reset status
 */
int get_sys_reset_status(void)
{
	if (!guts_crstr0) {
		printlog(LOGTYPE_GUTS, LOGLEVEL_ERROR,
		        "%s: reset status reg not found\n", __func__);
		return -EV_ENODEV;
	}

	uint32_t val = in32(guts_crstr0);

	// FIXME: simics always returns 0
	// for CRSTR0, so for now consider
	// that POR
	if (val & CRSTR0_RST_PORST || val == 0) {
		return SYS_RESET_STATUS_POR;
	} else if (val & CRSTR0_RST_HRST) {
		return SYS_RESET_STATUS_HARD;
	} else {
		return -EV_EIO;
	}
}

int system_reset(void) __attribute__ ((weak));
int system_reset(void)
{
	if (!guts_rstcr) {
		printlog(LOGTYPE_GUTS, LOGLEVEL_ERROR,
		        "%s: reset control reg not found\n", __func__);
		return EV_ENODEV;
	}

	out32(guts_rstcr, RSTCR_RESET_REQ);

	// FIXME: timeout here if the reset doesn't happen
	// and return an error
	while (1);
}

int set_reset_mask(uint32_t mask)
{
	uint32_t val;

	if (!guts_rstrqmr) {
		printlog(LOGTYPE_GUTS, LOGLEVEL_ERROR,
			 "%s: reset request mask reg not found\n", __func__);
		return -EV_ENODEV;
	}

	val = in32(guts_rstrqmr);

	out32(guts_rstrqmr, val | mask);

	return 0;
}

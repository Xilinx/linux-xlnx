/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx SDI FMC (Fidus SDI FMC) mezzanine lookup.
 *
 * Used by both the SDI Tx (DRM) and SDI Rx (V4L2) subsystem drivers so that an
 * endpoint can wait for the shared mezzanine to be configured before it brings
 * its GT up.  This is driver-model glue only, so it carries no dependency on
 * the mezzanine driver: that driver can be built in, built as a module or left
 * out entirely without either endpoint having to know.
 *
 * Copyright (C) 2019 - 2020 Xilinx, Inc.  All rights reserved.
 * Copyright (c) 2023 - 2026 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Author: Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>
 */

#ifndef _LINUX_XLNX_SDI_FMC_H_
#define _LINUX_XLNX_SDI_FMC_H_

#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

/* Locate the FMC node an SDI endpoint refers to. */
static inline struct device_node *xlnx_sdi_fmc_find_node(struct device *dev)
{
	return of_parse_phandle(dev->of_node, "xlnx,fmc", 0);
}

/**
 * xlnx_sdi_fmc_init - wait for this endpoint's SDI FMC mezzanine
 * @dev: the SDI Tx or Rx device, whose node may carry an "xlnx,fmc" phandle
 *
 * The mezzanine conditions the physical SDI signal for both directions and is
 * programmed by its own driver.  Resolve the optional phandle and report
 * whether that has happened yet, so an endpoint can hold off bringing its GT
 * up until the card is ready.  Boards that describe no mezzanine, including
 * Tx-only and Rx-only designs, simply succeed.
 *
 * Return: 0 when the mezzanine is configured or none is described, or
 * -EPROBE_DEFER while it is still being brought up.
 */
static inline int xlnx_sdi_fmc_init(struct device *dev)
{
	struct platform_device *pdev;
	struct device_node *np;
	bool bound;

	np = xlnx_sdi_fmc_find_node(dev);
	if (!np)
		return 0;	/* No mezzanine described - nothing to do. */

	/*
	 * A node with no platform device never had one created for it, which
	 * for a node on the platform bus means it is missing its compatible.
	 * Say so: the alternative symptom is an endpoint that defers forever
	 * with no indication why.
	 */
	pdev = of_find_device_by_node(np);
	if (!pdev) {
		int ret = dev_err_probe(dev, -EPROBE_DEFER,
					"%pOF has no driver; is compatible = \"xlnx,sdi-fmc\" missing?\n",
					np);

		of_node_put(np);
		return ret;
	}
	of_node_put(np);

	/* device_is_bound() reads the driver klist, which needs the lock. */
	device_lock(&pdev->dev);
	bound = device_is_bound(&pdev->dev);
	device_unlock(&pdev->dev);
	put_device(&pdev->dev);

	if (!bound)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for the SDI FMC to be configured\n");

	return 0;
}

#endif /* _LINUX_XLNX_SDI_FMC_H_ */

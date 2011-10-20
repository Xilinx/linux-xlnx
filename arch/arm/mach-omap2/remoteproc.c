/*
 * Remote processor machine-specific module for OMAP4
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)    "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>

#include <plat/omap_device.h>
#include <plat/omap_hwmod.h>
#include <plat/remoteproc.h>
#include <plat/iommu.h>

/*
 * Temporarily define the CMA base address explicitly.
 *
 * This will go away as soon as we have the IOMMU-based generic
 * DMA API in place.
 */
#define OMAP_RPROC_CMA_BASE	(0xa9800000)

/*
 * These data structures define platform-specific information
 * needed for each supported remote processor.
 *
 * At this point we only support the remote dual M3 "Ducati" imaging
 * subsystem (aka "ipu"), but later on we'll also add support for the
 * DSP ("Tesla").
 */
static struct omap_rproc_pdata omap4_rproc_data[] = {
	{
		.name		= "ipu_c0",
		.firmware	= "ducati-m3-core0.xem3",
		.mbox_name	= "mailbox-1",
		.oh_name	= "ipu_c0",
	},
};

static struct omap_iommu_arch_data omap4_rproc_iommu[] = {
	{ .name = "ducati" },
};

static struct omap_device_pm_latency omap_rproc_latency[] = {
	{
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func = omap_device_enable_hwmods,
		.flags = OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

static struct platform_device omap4_ducati = {
	.name	= "omap-rproc",
	.id	= 1, /* reserve 0 for tesla. we respect. */
};

static struct platform_device *omap4_rproc_devs[] __initdata = {
	&omap4_ducati,
};

void __init omap_rproc_reserve_cma(void)
{
	int ret;

	/* reserve CMA memory for OMAP4's M3 "ducati" remote processor */
	ret = dma_declare_contiguous(&omap4_ducati.dev,
					CONFIG_OMAP_DUCATI_CMA_SIZE,
					OMAP_RPROC_CMA_BASE, 0);
	if (ret)
		pr_err("dma_declare_contiguous failed %d\n", ret);
}

static int __init omap_rproc_init(void)
{
	struct omap_hwmod *oh[2];
	struct omap_device *od;
	int i, ret = 0, oh_count;

	/* names like ipu_cx/dsp_cx might show up on other OMAPs, too */
	if (!cpu_is_omap44xx())
		return 0;

	/* build the remote proc devices */
	for (i = 0; i < ARRAY_SIZE(omap4_rproc_data); i++) {
		const char *oh_name = omap4_rproc_data[i].oh_name;
		const char *oh_name_opt = omap4_rproc_data[i].oh_name_opt;
		struct platform_device *pdev = omap4_rproc_devs[i];
		oh_count = 0;

		oh[0] = omap_hwmod_lookup(oh_name);
		if (!oh[0]) {
			pr_err("could not look up %s\n", oh_name);
			continue;
		}
		oh_count++;

		/*
		 * ipu might have a secondary hwmod entry (for configurations
		 * where we want both M3 cores to be represented by a single
		 * device).
		 */
		if (oh_name_opt) {
			oh[1] = omap_hwmod_lookup(oh_name_opt);
			if (!oh[1]) {
				pr_err("could not look up %s\n", oh_name_opt);
				continue;
			}
			oh_count++;
		}

		omap4_rproc_data[i].device_enable = omap_device_enable;
		omap4_rproc_data[i].device_shutdown = omap_device_shutdown;

		device_initialize(&pdev->dev);

		/* Set dev_name early to allow dev_xxx in omap_device_alloc */
		dev_set_name(&pdev->dev, "%s.%d", pdev->name,  pdev->id);

		od = omap_device_alloc(pdev, oh, oh_count,
					omap_rproc_latency,
					ARRAY_SIZE(omap_rproc_latency));
		if (!od) {
			dev_err(&pdev->dev, "omap_device_alloc failed\n");
			put_device(&pdev->dev);
			ret = PTR_ERR(od);
			continue;
		}

		ret = platform_device_add_data(pdev,
					&omap4_rproc_data[i],
					sizeof(struct omap_rproc_pdata));
		if (ret) {
			dev_err(&pdev->dev, "can't add pdata\n");
			omap_device_delete(od);
			put_device(&pdev->dev);
			continue;
		}

		/* attach the remote processor to its iommu device */
		pdev->dev.archdata.iommu = &omap4_rproc_iommu[i];

		ret = omap_device_register(pdev);
		if (ret) {
			dev_err(&pdev->dev, "omap_device_register failed\n");
			omap_device_delete(od);
			put_device(&pdev->dev);
			continue;
		}
	}

	return ret;
}
device_initcall(omap_rproc_init);

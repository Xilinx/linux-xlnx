/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * DOC: VC4 HVS module.
 *
 * The HVS is the piece of hardware that does translation, scaling,
 * colorspace conversion, and compositing of pixels stored in
 * framebuffers into a FIFO of pixels going out to the Pixel Valve
 * (CRTC).  It operates at the system clock rate (the system audio
 * clock gate, specifically), which is much higher than the pixel
 * clock rate.
 *
 * There is a single global HVS, with multiple output FIFOs that can
 * be consumed by the PVs.  This file just manages the resources for
 * the HVS, while the vc4_crtc.c code actually drives HVS setup for
 * each CRTC.
 */

#include "linux/component.h"
#include "vc4_drv.h"
#include "vc4_regs.h"

#define HVS_REG(reg) { reg, #reg }
static const struct {
	u32 reg;
	const char *name;
} hvs_regs[] = {
	HVS_REG(SCALER_DISPCTRL),
	HVS_REG(SCALER_DISPSTAT),
	HVS_REG(SCALER_DISPID),
	HVS_REG(SCALER_DISPECTRL),
	HVS_REG(SCALER_DISPPROF),
	HVS_REG(SCALER_DISPDITHER),
	HVS_REG(SCALER_DISPEOLN),
	HVS_REG(SCALER_DISPLIST0),
	HVS_REG(SCALER_DISPLIST1),
	HVS_REG(SCALER_DISPLIST2),
	HVS_REG(SCALER_DISPLSTAT),
	HVS_REG(SCALER_DISPLACT0),
	HVS_REG(SCALER_DISPLACT1),
	HVS_REG(SCALER_DISPLACT2),
	HVS_REG(SCALER_DISPCTRL0),
	HVS_REG(SCALER_DISPBKGND0),
	HVS_REG(SCALER_DISPSTAT0),
	HVS_REG(SCALER_DISPBASE0),
	HVS_REG(SCALER_DISPCTRL1),
	HVS_REG(SCALER_DISPBKGND1),
	HVS_REG(SCALER_DISPSTAT1),
	HVS_REG(SCALER_DISPBASE1),
	HVS_REG(SCALER_DISPCTRL2),
	HVS_REG(SCALER_DISPBKGND2),
	HVS_REG(SCALER_DISPSTAT2),
	HVS_REG(SCALER_DISPBASE2),
	HVS_REG(SCALER_DISPALPHA2),
};

void vc4_hvs_dump_state(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(hvs_regs); i++) {
		DRM_INFO("0x%04x (%s): 0x%08x\n",
			 hvs_regs[i].reg, hvs_regs[i].name,
			 HVS_READ(hvs_regs[i].reg));
	}

	DRM_INFO("HVS ctx:\n");
	for (i = 0; i < 64; i += 4) {
		DRM_INFO("0x%08x (%s): 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 i * 4, i < HVS_BOOTLOADER_DLIST_END ? "B" : "D",
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 0),
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 1),
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 2),
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 3));
	}
}

#ifdef CONFIG_DEBUG_FS
int vc4_hvs_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(hvs_regs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   hvs_regs[i].name, hvs_regs[i].reg,
			   HVS_READ(hvs_regs[i].reg));
	}

	return 0;
}
#endif

/* The filter kernel is composed of dwords each containing 3 9-bit
 * signed integers packed next to each other.
 */
#define VC4_INT_TO_COEFF(coeff) (coeff & 0x1ff)
#define VC4_PPF_FILTER_WORD(c0, c1, c2)				\
	((((c0) & 0x1ff) << 0) |				\
	 (((c1) & 0x1ff) << 9) |				\
	 (((c2) & 0x1ff) << 18))

/* The whole filter kernel is arranged as the coefficients 0-16 going
 * up, then a pad, then 17-31 going down and reversed within the
 * dwords.  This means that a linear phase kernel (where it's
 * symmetrical at the boundary between 15 and 16) has the last 5
 * dwords matching the first 5, but reversed.
 */
#define VC4_LINEAR_PHASE_KERNEL(c0, c1, c2, c3, c4, c5, c6, c7, c8,	\
				c9, c10, c11, c12, c13, c14, c15)	\
	{VC4_PPF_FILTER_WORD(c0, c1, c2),				\
	 VC4_PPF_FILTER_WORD(c3, c4, c5),				\
	 VC4_PPF_FILTER_WORD(c6, c7, c8),				\
	 VC4_PPF_FILTER_WORD(c9, c10, c11),				\
	 VC4_PPF_FILTER_WORD(c12, c13, c14),				\
	 VC4_PPF_FILTER_WORD(c15, c15, 0)}

#define VC4_LINEAR_PHASE_KERNEL_DWORDS 6
#define VC4_KERNEL_DWORDS (VC4_LINEAR_PHASE_KERNEL_DWORDS * 2 - 1)

/* Recommended B=1/3, C=1/3 filter choice from Mitchell/Netravali.
 * http://www.cs.utexas.edu/~fussell/courses/cs384g/lectures/mitchell/Mitchell.pdf
 */
static const u32 mitchell_netravali_1_3_1_3_kernel[] =
	VC4_LINEAR_PHASE_KERNEL(0, -2, -6, -8, -10, -8, -3, 2, 18,
				50, 82, 119, 155, 187, 213, 227);

static int vc4_hvs_upload_linear_kernel(struct vc4_hvs *hvs,
					struct drm_mm_node *space,
					const u32 *kernel)
{
	int ret, i;
	u32 __iomem *dst_kernel;

	ret = drm_mm_insert_node(&hvs->dlist_mm, space, VC4_KERNEL_DWORDS, 1,
				 0);
	if (ret) {
		DRM_ERROR("Failed to allocate space for filter kernel: %d\n",
			  ret);
		return ret;
	}

	dst_kernel = hvs->dlist + space->start;

	for (i = 0; i < VC4_KERNEL_DWORDS; i++) {
		if (i < VC4_LINEAR_PHASE_KERNEL_DWORDS)
			writel(kernel[i], &dst_kernel[i]);
		else {
			writel(kernel[VC4_KERNEL_DWORDS - i - 1],
			       &dst_kernel[i]);
		}
	}

	return 0;
}

static int vc4_hvs_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = drm->dev_private;
	struct vc4_hvs *hvs = NULL;
	int ret;

	hvs = devm_kzalloc(&pdev->dev, sizeof(*hvs), GFP_KERNEL);
	if (!hvs)
		return -ENOMEM;

	hvs->pdev = pdev;

	hvs->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(hvs->regs))
		return PTR_ERR(hvs->regs);

	hvs->dlist = hvs->regs + SCALER_DLIST_START;

	spin_lock_init(&hvs->mm_lock);

	/* Set up the HVS display list memory manager.  We never
	 * overwrite the setup from the bootloader (just 128b out of
	 * our 16K), since we don't want to scramble the screen when
	 * transitioning from the firmware's boot setup to runtime.
	 */
	drm_mm_init(&hvs->dlist_mm,
		    HVS_BOOTLOADER_DLIST_END,
		    (SCALER_DLIST_SIZE >> 2) - HVS_BOOTLOADER_DLIST_END);

	/* Set up the HVS LBM memory manager.  We could have some more
	 * complicated data structure that allowed reuse of LBM areas
	 * between planes when they don't overlap on the screen, but
	 * for now we just allocate globally.
	 */
	drm_mm_init(&hvs->lbm_mm, 0, 96 * 1024);

	/* Upload filter kernels.  We only have the one for now, so we
	 * keep it around for the lifetime of the driver.
	 */
	ret = vc4_hvs_upload_linear_kernel(hvs,
					   &hvs->mitchell_netravali_filter,
					   mitchell_netravali_1_3_1_3_kernel);
	if (ret)
		return ret;

	vc4->hvs = hvs;
	return 0;
}

static void vc4_hvs_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = drm->dev_private;

	if (vc4->hvs->mitchell_netravali_filter.allocated)
		drm_mm_remove_node(&vc4->hvs->mitchell_netravali_filter);

	drm_mm_takedown(&vc4->hvs->dlist_mm);
	drm_mm_takedown(&vc4->hvs->lbm_mm);

	vc4->hvs = NULL;
}

static const struct component_ops vc4_hvs_ops = {
	.bind   = vc4_hvs_bind,
	.unbind = vc4_hvs_unbind,
};

static int vc4_hvs_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_hvs_ops);
}

static int vc4_hvs_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_hvs_ops);
	return 0;
}

static const struct of_device_id vc4_hvs_dt_match[] = {
	{ .compatible = "brcm,bcm2835-hvs" },
	{}
};

struct platform_driver vc4_hvs_driver = {
	.probe = vc4_hvs_dev_probe,
	.remove = vc4_hvs_dev_remove,
	.driver = {
		.name = "vc4_hvs",
		.of_match_table = vc4_hvs_dt_match,
	},
};

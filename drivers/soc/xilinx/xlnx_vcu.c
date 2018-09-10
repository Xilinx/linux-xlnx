// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx VCU Init
 *
 * Copyright (C) 2016 - 2017 Xilinx, Inc.
 *
 * Contacts   Dhaval Shah <dshah@xilinx.com>
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <soc/xilinx/xlnx_vcu.h>

/* Address map for different registers implemented in the VCU LogiCORE IP. */
#define VCU_ECODER_ENABLE		0x00
#define VCU_DECODER_ENABLE		0x04
#define VCU_MEMORY_DEPTH		0x08
#define VCU_ENC_COLOR_DEPTH		0x0c
#define VCU_ENC_VERTICAL_RANGE		0x10
#define VCU_ENC_FRAME_SIZE_X		0x14
#define VCU_ENC_FRAME_SIZE_Y		0x18
#define VCU_ENC_COLOR_FORMAT		0x1c
#define VCU_ENC_FPS			0x20
#define VCU_MCU_CLK			0x24
#define VCU_CORE_CLK			0x28
#define VCU_PLL_CLK			0x34
#define VCU_ENC_VIDEO_STANDARD		0x38
#define VCU_STATUS			0x3c
#define VCU_DEC_VIDEO_STANDARD		0x4c
#define VCU_DEC_FRAME_SIZE_X		0x50
#define VCU_DEC_FRAME_SIZE_Y		0x54
#define VCU_DEC_FPS			0x58
#define VCU_BUFFER_B_FRAME		0x5c
#define VCU_WPP_EN			0x60
#define VCU_PLL_CLK_DEC			0x64
#define VCU_NUM_CORE			0x6c
#define VCU_GASKET_INIT			0x74
#define VCU_GASKET_VALUE		0x03

#define MHZ				1000000
#define FRAC				100

/**
 * struct xvcu_priv - Xilinx VCU private data
 * @dev: Platform device
 * @pll_ref: PLL ref clock source
 * @core_enc: Core encoder clock
 * @core_dec: Core decoder clock
 * @mcu_enc: MCU encoder clock
 * @mcu_dec: MCU decoder clock
 * @logicore_reg_ba: logicore reg base address
 * @vcu_slcr_ba: vcu_slcr Register base address
 */
struct xvcu_priv {
	struct device *dev;
	struct clk *pll_ref;
	struct clk *core_enc;
	struct clk *core_dec;
	struct clk *mcu_enc;
	struct clk *mcu_dec;
	void __iomem *logicore_reg_ba;
	void __iomem *vcu_slcr_ba;
};

/**
 * xvcu_read - Read from the VCU register space
 * @iomem:	vcu reg space base address
 * @offset:	vcu reg offset from base
 *
 * Return:	Returns 32bit value from VCU register specified
 *
 */
static inline u32 xvcu_read(void __iomem *iomem, u32 offset)
{
	return ioread32(iomem + offset);
}

/**
 * xvcu_write - Write to the VCU register space
 * @iomem:	vcu reg space base address
 * @offset:	vcu reg offset from base
 * @value:	Value to write
 */
static inline void xvcu_write(void __iomem *iomem, u32 offset, u32 value)
{
	iowrite32(value, iomem + offset);
}

/**
 * xvcu_get_color_depth - read the color depth register
 * @xvcu:	Pointer to the xvcu_device structure
 *
 * Return:	Returns 32bit value
 *
 */
u32 xvcu_get_color_depth(struct xvcu_device *xvcu)
{
	return xvcu_read(xvcu->logicore_reg_ba, VCU_ENC_COLOR_DEPTH);
}
EXPORT_SYMBOL_GPL(xvcu_get_color_depth);

/**
 * xvcu_get_memory_depth - read the memory depth register
 * @xvcu:	Pointer to the xvcu_device structure
 *
 * Return:	Returns 32bit value
 *
 */
u32 xvcu_get_memory_depth(struct xvcu_device *xvcu)
{
	return xvcu_read(xvcu->logicore_reg_ba, VCU_MEMORY_DEPTH);
}
EXPORT_SYMBOL_GPL(xvcu_get_memory_depth);

/**
 * xvcu_get_clock_frequency - provide the core clock frequency
 * @xvcu:	Pointer to the xvcu_device structure
 *
 * Return:	Returns 32bit value
 *
 */
u32 xvcu_get_clock_frequency(struct xvcu_device *xvcu)
{
	return xvcu_read(xvcu->logicore_reg_ba, VCU_CORE_CLK) * MHZ;
}
EXPORT_SYMBOL_GPL(xvcu_get_clock_frequency);

/**
 * xvcu_get_num_cores - read the number of core register
 * @xvcu:	Pointer to the xvcu_device structure
 *
 * Return:	Returns 32bit value
 *
 */
u32 xvcu_get_num_cores(struct xvcu_device *xvcu)
{
	return xvcu_read(xvcu->logicore_reg_ba, VCU_NUM_CORE);
}
EXPORT_SYMBOL_GPL(xvcu_get_num_cores);

/**
 * xvcu_set_vcu_pll - Set the VCU PLL
 * @xvcu:	Pointer to the xvcu_device structure
 *
 * Programming the VCU PLL based on the user configuration
 * (ref clock freq, core clock freq, mcu clock freq).
 * Core clock frequency has higher priority than mcu clock frequency
 *
 * Return:	Returns status, either success or error+reason
 */
static int xvcu_set_vcu_pll(struct xvcu_priv *xvcu)
{
	u32 refclk, coreclk, mcuclk, inte, deci;
	int ret;

	inte = xvcu_read(xvcu->logicore_reg_ba, VCU_PLL_CLK);
	deci = xvcu_read(xvcu->logicore_reg_ba, VCU_PLL_CLK_DEC);
	coreclk = xvcu_read(xvcu->logicore_reg_ba, VCU_CORE_CLK) * MHZ;
	mcuclk = xvcu_read(xvcu->logicore_reg_ba, VCU_MCU_CLK) * MHZ;
	if (!mcuclk || !coreclk) {
		dev_err(xvcu->dev, "Invalid mcu and core clock data\n");
		return -EINVAL;
	}

	refclk = (inte * MHZ) + (deci * (MHZ / FRAC));
	dev_dbg(xvcu->dev, "Ref clock from logicoreIP is %uHz\n", refclk);
	dev_dbg(xvcu->dev, "Core clock from logicoreIP is %uHz\n", coreclk);
	dev_dbg(xvcu->dev, "Mcu clock from logicoreIP is %uHz\n", mcuclk);

	ret = clk_set_rate(xvcu->pll_ref, refclk);
	if (ret)
		dev_warn(xvcu->dev, "failed to set logicoreIP refclk rate %d\n"
			 , ret);

	ret = clk_prepare_enable(xvcu->pll_ref);
	if (ret) {
		dev_err(xvcu->dev, "failed to enable pll_ref clock source %d\n",
			ret);
		return ret;
	}

	ret = clk_set_rate(xvcu->mcu_enc, mcuclk);
	if (ret)
		dev_warn(xvcu->dev, "failed to set logicoreIP mcu clk rate %d\n",
			 ret);

	ret = clk_prepare_enable(xvcu->mcu_enc);
	if (ret) {
		dev_err(xvcu->dev, "failed to enable mcu_enc %d\n", ret);
		goto error_mcu_enc;
	}

	ret = clk_set_rate(xvcu->mcu_dec, mcuclk);
	if (ret)
		dev_warn(xvcu->dev, "failed to set logicoreIP mcu clk rate %d\n",
			 ret);

	ret = clk_prepare_enable(xvcu->mcu_dec);
	if (ret) {
		dev_err(xvcu->dev, "failed to enable mcu_dec %d\n", ret);
		goto error_mcu_dec;
	}

	ret = clk_set_rate(xvcu->core_enc, coreclk);
	if (ret)
		dev_warn(xvcu->dev, "failed to set logicoreIP core clk rate %d\n",
			 ret);

	ret = clk_prepare_enable(xvcu->core_enc);
	if (ret) {
		dev_err(xvcu->dev, "failed to enable core_enc %d\n", ret);
		goto error_core_enc;
	}

	ret = clk_set_rate(xvcu->core_dec, coreclk);
	if (ret)
		dev_warn(xvcu->dev, "failed to set logicoreIP core clk rate %d\n",
			 ret);

	ret = clk_prepare_enable(xvcu->core_dec);
	if (ret) {
		dev_err(xvcu->dev, "failed to enable core_dec %d\n", ret);
		goto error_core_dec;
	}

	return 0;

error_core_dec:
	clk_disable_unprepare(xvcu->core_enc);
error_core_enc:
	clk_disable_unprepare(xvcu->mcu_dec);
error_mcu_dec:
	clk_disable_unprepare(xvcu->mcu_enc);
error_mcu_enc:
	clk_disable_unprepare(xvcu->pll_ref);

	return ret;
}

/**
 * xvcu_probe - Probe existence of the logicoreIP
 *			and initialize PLL
 *
 * @pdev:	Pointer to the platform_device structure
 *
 * Return:	Returns 0 on success
 *		Negative error code otherwise
 */
static int xvcu_probe(struct platform_device *pdev)
{
	struct xvcu_priv *xvcu;
	struct xvcu_device *xvcu_core = dev_get_drvdata(pdev->dev.parent);
	int ret;

	xvcu = devm_kzalloc(&pdev->dev, sizeof(*xvcu), GFP_KERNEL);
	if (!xvcu)
		return -ENOMEM;

	xvcu->dev = &pdev->dev;
	xvcu->vcu_slcr_ba = xvcu_core->vcu_slcr_ba;
	xvcu->logicore_reg_ba = xvcu_core->logicore_reg_ba;

	xvcu->pll_ref = devm_clk_get(pdev->dev.parent, "pll_ref");
	if (IS_ERR(xvcu->pll_ref)) {
		dev_err(&pdev->dev, "Could not get pll_ref clock\n");
		return PTR_ERR(xvcu->pll_ref);
	}

	xvcu->core_enc = devm_clk_get(pdev->dev.parent, "vcu_core_enc");
	if (IS_ERR(xvcu->core_enc)) {
		dev_err(&pdev->dev, "Could not get core_enc clock\n");
		return PTR_ERR(xvcu->core_enc);
	}

	xvcu->core_dec = devm_clk_get(pdev->dev.parent, "vcu_core_dec");
	if (IS_ERR(xvcu->core_dec)) {
		dev_err(&pdev->dev, "Could not get vcu_core_dec clock\n");
		return PTR_ERR(xvcu->core_dec);
	}

	xvcu->mcu_enc = devm_clk_get(pdev->dev.parent, "vcu_mcu_enc");
	if (IS_ERR(xvcu->mcu_enc)) {
		dev_err(&pdev->dev, "Could not get mcu_enc clock\n");
		return PTR_ERR(xvcu->mcu_enc);
	}

	xvcu->mcu_dec = devm_clk_get(pdev->dev.parent, "vcu_mcu_dec");
	if (IS_ERR(xvcu->mcu_dec)) {
		dev_err(&pdev->dev, "Could not get mcu_dec clock\n");
		return PTR_ERR(xvcu->mcu_dec);
	}

	/* Do the PLL Settings based on the ref clk,core and mcu clk freq */
	ret = xvcu_set_vcu_pll(xvcu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set the pll\n");
		return ret;
	}

	dev_set_drvdata(&pdev->dev, xvcu);

	ret = devm_of_platform_populate(pdev->dev.parent);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register allegro codecs\n");
		return ret;
	}

	dev_info(&pdev->dev, "%s: Probed successfully\n", __func__);

	return ret;
}

/**
 * xvcu_remove - Depopulate the child nodes, Insert gasket isolation
 *			and disable the clock
 * @pdev:	Pointer to the platform_device structure
 *
 * Return:	Returns 0 on success
 *		Negative error code otherwise
 */
static int xvcu_remove(struct platform_device *pdev)
{
	struct xvcu_priv *xvcu;

	xvcu = platform_get_drvdata(pdev);
	if (!xvcu)
		return -ENODEV;

	clk_disable_unprepare(xvcu->core_enc);
	devm_clk_put(pdev->dev.parent, xvcu->core_enc);

	clk_disable_unprepare(xvcu->core_dec);
	devm_clk_put(pdev->dev.parent, xvcu->core_dec);

	clk_disable_unprepare(xvcu->mcu_enc);
	devm_clk_put(pdev->dev.parent, xvcu->mcu_enc);

	clk_disable_unprepare(xvcu->mcu_dec);
	devm_clk_put(pdev->dev.parent, xvcu->mcu_dec);

	clk_disable_unprepare(xvcu->pll_ref);
	devm_clk_put(pdev->dev.parent, xvcu->pll_ref);

	return 0;
}

static struct platform_driver xvcu_driver = {
	.driver = {
		.name           = "xilinx-vcu",
	},
	.probe                  = xvcu_probe,
	.remove                 = xvcu_remove,
};

module_platform_driver(xvcu_driver);

MODULE_AUTHOR("Dhaval Shah <dshah@xilinx.com>");
MODULE_DESCRIPTION("Xilinx VCU init Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:xilinx-vcu");

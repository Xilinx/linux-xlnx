/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 */

#ifndef __XLNX_VCU_H
#define __XLNX_VCU_H

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
#define VCU_PLL_BYPASS			0x2c
#define VCU_ENC_CLK			0x30
#define VCU_PLL_CLK			0x34
#define VCU_ENC_VIDEO_STANDARD		0x38
#define VCU_STATUS			0x3c
#define VCU_AXI_ENC_CLK			0x40
#define VCU_AXI_DEC_CLK			0x44
#define VCU_AXI_MCU_CLK			0x48
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

/**
 * struct xvcu_device - Xilinx VCU init device structure
 * @dev: Platform device
 * @pll_ref: pll ref clock source
 * @reset_gpio: vcu reset gpio
 * @aclk: axi clock source
 * @logicore_reg_ba: logicore reg base address
 * @vcu_slcr_ba: vcu_slcr Register base address
 * @pll: handle for the VCU PLL
 * @pll_post: handle for the VCU PLL post divider
 * @clk_data: clocks provided by the vcu clock provider
 */
struct xvcu_device {
	struct device *dev;
	struct clk *pll_ref;
	struct clk *aclk;
	struct gpio_desc *reset_gpio;
	struct regmap *logicore_reg_ba;
	void __iomem *vcu_slcr_ba;
	struct clk_hw *pll;
	struct clk_hw *pll_post;
	struct clk_hw_onecell_data *clk_data;
};

u32 xvcu_get_color_depth(struct xvcu_device *xvcu);
u32 xvcu_get_memory_depth(struct xvcu_device *xvcu);
u32 xvcu_get_clock_frequency(struct xvcu_device *xvcu);
u32 xvcu_get_num_cores(struct xvcu_device *xvcu);
#endif /* __XLNX_VCU_H */

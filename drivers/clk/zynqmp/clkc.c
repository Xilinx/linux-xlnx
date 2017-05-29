/*
 * Zynq UltraScale+ MPSoC clock controller
 *
 *  Copyright (C) 2016 Xilinx
 *
 * Based on drivers/clk/zynq/clkc.c
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/zynqmp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/soc/xilinx/zynqmp/pm.h>

static const resource_size_t zynqmp_crf_apb_clkc_base = 0xfd1a0020;
static const resource_size_t zynqmp_crl_apb_clkc_base = 0xff5e0020;
static const resource_size_t zynqmp_iou_clkc_base = 0xff180000;

/* Full power domain clocks */
#define CRF_APB_APLL_CTRL		(zynqmp_crf_apb_clkc_base + 0x00)
#define CRF_APB_DPLL_CTRL		(zynqmp_crf_apb_clkc_base + 0x0c)
#define CRF_APB_VPLL_CTRL		(zynqmp_crf_apb_clkc_base + 0x18)
#define CRF_APB_PLL_STATUS		(zynqmp_crf_apb_clkc_base + 0x24)
#define CRF_APB_APLL_TO_LPD_CTRL	(zynqmp_crf_apb_clkc_base + 0x28)
#define CRF_APB_DPLL_TO_LPD_CTRL	(zynqmp_crf_apb_clkc_base + 0x2c)
#define CRF_APB_VPLL_TO_LPD_CTRL	(zynqmp_crf_apb_clkc_base + 0x30)
/* Peripheral clocks */
#define CRF_APB_ACPU_CTRL		(zynqmp_crf_apb_clkc_base + 0x40)
#define CRF_APB_DBG_TRACE_CTRL		(zynqmp_crf_apb_clkc_base + 0x44)
#define CRF_APB_DBG_FPD_CTRL		(zynqmp_crf_apb_clkc_base + 0x48)
#define CRF_APB_DP_VIDEO_REF_CTRL	(zynqmp_crf_apb_clkc_base + 0x50)
#define CRF_APB_DP_AUDIO_REF_CTRL	(zynqmp_crf_apb_clkc_base + 0x54)
#define CRF_APB_DP_STC_REF_CTRL		(zynqmp_crf_apb_clkc_base + 0x5c)
#define CRF_APB_DDR_CTRL		(zynqmp_crf_apb_clkc_base + 0x60)
#define CRF_APB_GPU_REF_CTRL		(zynqmp_crf_apb_clkc_base + 0x64)
#define CRF_APB_SATA_REF_CTRL		(zynqmp_crf_apb_clkc_base + 0x80)
#define CRF_APB_PCIE_REF_CTRL		(zynqmp_crf_apb_clkc_base + 0x94)
#define CRF_APB_GDMA_REF_CTRL		(zynqmp_crf_apb_clkc_base + 0x98)
#define CRF_APB_DPDMA_REF_CTRL		(zynqmp_crf_apb_clkc_base + 0x9c)
#define CRF_APB_TOPSW_MAIN_CTRL		(zynqmp_crf_apb_clkc_base + 0xa0)
#define CRF_APB_TOPSW_LSBUS_CTRL	(zynqmp_crf_apb_clkc_base + 0xa4)
#define CRF_APB_GTGREF0_REF_CTRL	(zynqmp_crf_apb_clkc_base + 0xa8)
#define CRF_APB_DBG_TSTMP_CTRL		(zynqmp_crf_apb_clkc_base + 0xd8)

/* Low power domain clocks */
#define CRL_APB_IOPLL_CTRL		(zynqmp_crl_apb_clkc_base + 0x00)
#define CRL_APB_RPLL_CTRL		(zynqmp_crl_apb_clkc_base + 0x10)
#define CRL_APB_PLL_STATUS		(zynqmp_crl_apb_clkc_base + 0x20)
#define CRL_APB_IOPLL_TO_FPD_CTRL	(zynqmp_crl_apb_clkc_base + 0x24)
#define CRL_APB_RPLL_TO_FPD_CTRL	(zynqmp_crl_apb_clkc_base + 0x28)
/* Peripheral clocks */
#define CRL_APB_USB3_DUAL_REF_CTRL	(zynqmp_crl_apb_clkc_base + 0x2c)
#define CRL_APB_GEM0_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x30)
#define CRL_APB_GEM1_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x34)
#define CRL_APB_GEM2_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x38)
#define CRL_APB_GEM3_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x3c)
#define CRL_APB_USB0_BUS_REF_CTRL	(zynqmp_crl_apb_clkc_base + 0x40)
#define CRL_APB_USB1_BUS_REF_CTRL	(zynqmp_crl_apb_clkc_base + 0x44)
#define CRL_APB_QSPI_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x48)
#define CRL_APB_SDIO0_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x4c)
#define CRL_APB_SDIO1_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x50)
#define CRL_APB_UART0_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x54)
#define CRL_APB_UART1_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x58)
#define CRL_APB_SPI0_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x5c)
#define CRL_APB_SPI1_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x60)
#define CRL_APB_CAN0_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x64)
#define CRL_APB_CAN1_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x68)
#define CRL_APB_CPU_R5_CTRL		(zynqmp_crl_apb_clkc_base + 0x70)
#define CRL_APB_IOU_SWITCH_CTRL		(zynqmp_crl_apb_clkc_base + 0x7c)
#define CRL_APB_CSU_PLL_CTRL		(zynqmp_crl_apb_clkc_base + 0x80)
#define CRL_APB_PCAP_CTRL		(zynqmp_crl_apb_clkc_base + 0x84)
#define CRL_APB_LPD_SWITCH_CTRL		(zynqmp_crl_apb_clkc_base + 0x88)
#define CRL_APB_LPD_LSBUS_CTRL		(zynqmp_crl_apb_clkc_base + 0x8c)
#define CRL_APB_DBG_LPD_CTRL		(zynqmp_crl_apb_clkc_base + 0x90)
#define CRL_APB_NAND_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x94)
#define CRL_APB_ADMA_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x98)
#define CRL_APB_PL0_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0xa0)
#define CRL_APB_PL1_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0xa4)
#define CRL_APB_PL2_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0xa8)
#define CRL_APB_PL3_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0xac)
#define CRL_APB_PL0_THR_CNT		(zynqmp_crl_apb_clkc_base + 0xb4)
#define CRL_APB_PL1_THR_CNT		(zynqmp_crl_apb_clkc_base + 0xbc)
#define CRL_APB_PL2_THR_CNT		(zynqmp_crl_apb_clkc_base + 0xc4)
#define CRL_APB_PL3_THR_CNT		(zynqmp_crl_apb_clkc_base + 0xdc)
#define CRL_APB_GEM_TSU_REF_CTRL	(zynqmp_crl_apb_clkc_base + 0xe0)
#define CRL_APB_DLL_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0xe4)
#define CRL_APB_AMS_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0xe8)
#define CRL_APB_I2C0_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x100)
#define CRL_APB_I2C1_REF_CTRL		(zynqmp_crl_apb_clkc_base + 0x104)
#define CRL_APB_TIMESTAMP_REF_CTRL	(zynqmp_crl_apb_clkc_base + 0x108)
#define IOU_SLCR_GEM_CLK_CTRL		(zynqmp_iou_clkc_base + 0x308)
#define IOU_SLCR_CAN_MIO_CTRL		(zynqmp_iou_clkc_base + 0x304)
#define IOU_SLCR_WDT_CLK_SEL		(zynqmp_iou_clkc_base + 0x300)

#define NUM_MIO_PINS	77

enum zynqmp_clk {
	iopll, rpll,
	apll, dpll, vpll,
	iopll_to_fpd, rpll_to_fpd, apll_to_lpd, dpll_to_lpd, vpll_to_lpd,
	acpu, acpu_half,
	dbg_fpd, dbg_lpd, dbg_trace, dbg_tstmp,
	dp_video_ref, dp_audio_ref,
	dp_stc_ref, gdma_ref, dpdma_ref,
	ddr_ref, sata_ref, pcie_ref,
	gpu_ref, gpu_pp0_ref, gpu_pp1_ref,
	topsw_main, topsw_lsbus,
	gtgref0_ref,
	lpd_switch, lpd_lsbus,
	usb0_bus_ref, usb1_bus_ref, usb3_dual_ref, usb0, usb1,
	cpu_r5, cpu_r5_core,
	csu_spb, csu_pll, pcap,
	iou_switch,
	gem_tsu_ref, gem_tsu,
	gem0_ref, gem1_ref, gem2_ref, gem3_ref,
	gem0_rx, gem1_rx, gem2_rx, gem3_rx,
	qspi_ref,
	sdio0_ref, sdio1_ref,
	uart0_ref, uart1_ref,
	spi0_ref, spi1_ref,
	nand_ref,
	i2c0_ref, i2c1_ref, can0_ref, can1_ref, can0, can1,
	dll_ref,
	adma_ref,
	timestamp_ref,
	ams_ref,
	pl0, pl1, pl2, pl3,
	wdt,
	clk_max,
};

static struct clk *clks[clk_max];
static struct clk_onecell_data clk_data;


static const char *can0_mio_mux2_parents[] __initconst = {"can0_ref",
							"can0_mio_mux"};
static const char *can1_mio_mux2_parents[] __initconst = {"can1_ref",
							"can1_mio_mux"};
static const char *usb0_mio_mux_parents[] __initconst = {"usb0_bus_ref",
							"usb0_mio_ulpi_clk"};
static const char *usb1_mio_mux_parents[] __initconst = {"usb1_bus_ref",
							"usb1_mio_ulpi_clk"};
static const char *swdt_ext_clk_input_names[] __initconst = {"swdt0_ext_clk",
							"swdt1_ext_clk"};
static const char *gem0_tx_mux_parents[] __initconst = {"gem0_ref_div1",
						"dummy_name"};
static const char *gem1_tx_mux_parents[] __initconst = {"gem1_ref_div1",
						"dummy_name"};
static const char *gem2_tx_mux_parents[] __initconst = {"gem2_ref_div1",
						"dummy_name"};
static const char *gem3_tx_mux_parents[] __initconst = {"gem3_ref_div1",
						"dummy_name"};
static const char *gem0_emio_input_names[] __initconst = {"gem0_emio_clk"};
static const char *gem1_emio_input_names[] __initconst = {"gem1_emio_clk"};
static const char *gem2_emio_input_names[] __initconst = {"gem2_emio_clk"};
static const char *gem3_emio_input_names[] __initconst = {"gem3_emio_clk"};

static const char *timestamp_ref_parents[8];
static const char *pll_src_mux_parents[8];
static const char *input_clks[5];
static const char *clk_output_name[clk_max];
static const char *acpu_parents[4];
static const char *ddr_parents[2];
static const char *wdt_ext_clk_mux_parents[3];
static const char *periph_parents[clk_max][4];
static const char *gem_tsu_mux_parents[4];
static const char *can_mio_mux_parents[NUM_MIO_PINS];
static const char *dll_ref_parents[2];
static const char *dummy_nm = "dummy_name";
/**
 * zynqmp_clk_register_pl_clk - Register a PL clock with the clock framework
 * @pl_clk:		Sequence number of the clock
 * @clk_name:		Clock name
 * @pl_clk_ctrl_reg:	Control register address
 * @parents:		Source clocks
 *
 */
static void __init zynqmp_clk_register_pl_clk(enum zynqmp_clk pl_clk,
		const char *clk_name, resource_size_t *pl_clk_ctrl_reg,
		const char **parents)
{
	struct clk *clk;
	char *mux_name;
	char *div0_name;
	char *div1_name;

	mux_name = kasprintf(GFP_KERNEL, "%s_mux", clk_name);
	if (!mux_name)
		goto err_mux_name;
	div0_name = kasprintf(GFP_KERNEL, "%s_div0", clk_name);
	if (!div0_name)
		goto err_div0_name;
	div1_name = kasprintf(GFP_KERNEL, "%s_div1", clk_name);
	if (!div1_name)
		goto err_div1_name;

	clk = zynqmp_clk_register_mux(NULL, mux_name, parents, 4,
			CLK_SET_RATE_NO_REPARENT, pl_clk_ctrl_reg, 0, 3, 0);

	clk = zynqmp_clk_register_divider(NULL, div0_name, mux_name, 0,
			pl_clk_ctrl_reg, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clk = zynqmp_clk_register_divider(NULL, div1_name, div0_name,
			CLK_SET_RATE_PARENT, pl_clk_ctrl_reg, 16, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clks[pl_clk] = zynqmp_clk_register_gate(NULL, clk_name, div1_name,
			CLK_SET_RATE_PARENT, pl_clk_ctrl_reg, 24, 0);

	kfree(mux_name);
	kfree(div0_name);
	kfree(div1_name);

	return;

err_div1_name:
	kfree(div0_name);
err_div0_name:
	kfree(mux_name);
err_mux_name:
	clks[pl_clk] = ERR_PTR(-ENOMEM);
}

/**
 * zynqmp_clk_register_pll_clk - Register a PLL clock with the clock framework
 * @pll_clk:		Sequence number of the clock
 * @clk_name:		Clock name
 * @flags:		pass the flag values
 * @clk_ctrl_reg:	Control register address
 * @status_reg:		PLL status register address
 * @lock_index:		bit index of the pll in the status register
 *
 * Return:		Error code on failure
 */
static int __init zynqmp_clk_register_pll_clk(enum zynqmp_clk pll_clk,
			const char *clk_name, unsigned long flags,
			resource_size_t *clk_ctrl_reg,
			resource_size_t *status_reg, u8 lock_index)
{
	struct clk *clk;
	char *clk_int_name;
	char *pre_src_mux_name;
	char *post_src_mux_name;
	char *int_half_name;
	char *int_mux_name;
	const char *int_mux_parents[2];
	const char *bypass_parents[2];

	pll_src_mux_parents[0] = input_clks[0];
	pll_src_mux_parents[1] = input_clks[0];
	pll_src_mux_parents[2] = input_clks[0];
	pll_src_mux_parents[3] = input_clks[0];
	pll_src_mux_parents[4] = input_clks[1];
	pll_src_mux_parents[5] = input_clks[2];
	pll_src_mux_parents[6] = input_clks[3];
	pll_src_mux_parents[7] = input_clks[4];

	clk_int_name = kasprintf(GFP_KERNEL, "%s_int", clk_name);
	if (!clk_int_name)
		goto err_clk_int_name;
	pre_src_mux_name = kasprintf(GFP_KERNEL, "%s_pre_src_mux", clk_name);
	if (!pre_src_mux_name)
		goto err_pre_src_mux_name;
	post_src_mux_name = kasprintf(GFP_KERNEL, "%s_post_src_mux", clk_name);
	if (!post_src_mux_name)
		goto err_post_src_mux_name;
	int_half_name = kasprintf(GFP_KERNEL, "%s_int_half", clk_name);
	if (!int_half_name)
		goto err_int_half_name;
	int_mux_name = kasprintf(GFP_KERNEL, "%s_int_mux", clk_name);
	if (!int_mux_name)
		goto err_int_mux_name;

	int_mux_parents[0] = clk_int_name;
	int_mux_parents[1] = int_half_name;

	bypass_parents[0] = int_mux_name;
	bypass_parents[1] = post_src_mux_name;

	clks[pll_clk] = clk_register_zynqmp_pll(clk_int_name, pre_src_mux_name,
			flags | CLK_SET_RATE_NO_REPARENT,
			clk_ctrl_reg, status_reg, lock_index);

	clk = zynqmp_clk_register_mux(NULL, pre_src_mux_name,
			pll_src_mux_parents, 8,	0, clk_ctrl_reg, 20, 3, 0);

	clk = clk_register_fixed_factor(NULL, int_half_name, clk_int_name,
			CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT, 1, 2);

	clk = zynqmp_clk_register_mux(NULL, int_mux_name, int_mux_parents, 2,
			CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
			clk_ctrl_reg, 16, 1, 0);

	clk = zynqmp_clk_register_mux(NULL, post_src_mux_name,
			pll_src_mux_parents, 8,	0, clk_ctrl_reg, 24, 3, 0);

	clk = zynqmp_clk_register_mux(NULL, clk_name, bypass_parents,
			2, CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
			clk_ctrl_reg, 3, 1, 0);

	kfree(clk_int_name);
	kfree(pre_src_mux_name);
	kfree(post_src_mux_name);
	kfree(int_half_name);
	kfree(int_mux_name);

	return 0;

err_int_mux_name:
	kfree(int_half_name);
err_int_half_name:
	kfree(post_src_mux_name);
err_post_src_mux_name:
	kfree(pre_src_mux_name);
err_pre_src_mux_name:
	kfree(clk_int_name);
err_clk_int_name:
	clks[pll_clk] = ERR_PTR(-ENOMEM);
	return -ENOMEM;
}

/**
 * zynqmp_clk_register_periph_clk - Register a peripheral clock
 *
 * @periph_clk:			Sequence number of the clock
 * @clk_name:			Clock name
 * @clk_ctrl_reg:		Control register address
 * @parents:			Source clocks
 * @gated:			0 = no gate registered gate flag value otherwise
 * @two_divisors:		1 = two divisors, 0 = 1 divisor
 * @clk_bit_idx:		Clock gate control bit index
 *
 * Return:			Error code on failure
 */
static int __init zynqmp_clk_register_periph_clk(
		unsigned long flags,
		enum zynqmp_clk periph_clk,
		const char *clk_name, resource_size_t clk_ctrl_reg,
		const char **parents, unsigned int gated,
		unsigned int two_divisors, u8 clk_bit_idx)
{
	struct clk *clk;
	char *mux_name;
	char *div0_name;
	char *div1_name = NULL;
	char *parent_div_name;

	flags |= CLK_SET_RATE_NO_REPARENT;

	mux_name = kasprintf(GFP_KERNEL, "%s_mux", clk_name);
	if (!mux_name)
		goto err_mux_name;
	div0_name = kasprintf(GFP_KERNEL, "%s_div0", clk_name);
	if (!div0_name)
		goto err_div0_name;
	if (two_divisors) {
		div1_name = kasprintf(GFP_KERNEL, "%s_div1", clk_name);
		if (!div1_name)
			goto err_div1_name;
	}

	clk = zynqmp_clk_register_mux(NULL, mux_name, parents, 4,
			flags, (resource_size_t *)clk_ctrl_reg, 0, 3, 0);
	if (!clk)
		goto err_div1_name;

	clk = zynqmp_clk_register_divider(NULL, div0_name, mux_name, flags,
				(resource_size_t *)clk_ctrl_reg, 8, 6,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	if (!clk)
		goto err_div1_name;

	parent_div_name = div0_name;
	if (two_divisors) {
		clk = zynqmp_clk_register_divider(NULL, div1_name, div0_name,
				flags, (resource_size_t *)clk_ctrl_reg, 16, 6,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
		parent_div_name = div1_name;
	}

	if (gated)
		clks[periph_clk] = zynqmp_clk_register_gate(NULL, clk_name,
					parent_div_name,
					CLK_SET_RATE_PARENT | gated,
					(resource_size_t *)clk_ctrl_reg,
					clk_bit_idx, 0);
	else
		clks[periph_clk] = clk;

	parent_div_name = NULL;
	kfree(mux_name);
	kfree(div0_name);
	if (two_divisors)
		kfree(div1_name);
	return 0;

err_div1_name:
	kfree(div0_name);
err_div0_name:
	kfree(mux_name);
err_mux_name:
	pr_err("%s: clock output name not in DT\n", __func__);
	clks[periph_clk] = ERR_PTR(-ENOMEM);
	return -ENOMEM;
}

/**
 * zynqmp_clk_get_parents - Assign source clocks for the given clock
 * @clk_output_name:	Array of clock names
 * @parents:		Requested source clocks array
 * @pll_0:		Source  PLL sequence number
 * @pll_1:		Source  PLL sequence number
 * @pll_2:		Source  PLL sequence number
 *
 * Return:		Error code on failure
 */
static inline void zynqmp_clk_get_parents(const char **clk_output_name,
				const char **parents, enum zynqmp_clk pll_0,
				enum zynqmp_clk pll_1, enum zynqmp_clk pll_2)
{
	parents[0] = clk_output_name[pll_0];
	parents[1] = "dummy_name";
	parents[2] = clk_output_name[pll_1];
	parents[3] = clk_output_name[pll_2];
}

/**
 * zynqmp_clk_setup -  Setup the clock framework and register clocks
 * @np:		Device node
 *
 * Return:	Error code on failure
 */
static void __init zynqmp_clk_setup(struct device_node *np)
{
	int i;
	u32 tmp;
	struct clk *clk;
	char *clk_name;
	int idx;

	idx = of_property_match_string(np, "clock-names", "pss_ref_clk");
	if (idx < 0) {
		pr_err("pss_ref_clk not provided\n");
		return;
	}
	input_clks[0] =	of_clk_get_parent_name(np, idx);

	idx = of_property_match_string(np, "clock-names", "video_clk");
	if (idx < 0) {
		pr_err("video_clk not provided\n");
		return;
	}
	input_clks[1] =	of_clk_get_parent_name(np, idx);

	idx = of_property_match_string(np, "clock-names", "pss_alt_ref_clk");
	if (idx < 0) {
		pr_err("pss_alt_ref_clk not provided\n");
		return;
	}
	input_clks[2] =	of_clk_get_parent_name(np, idx);

	idx = of_property_match_string(np, "clock-names", "aux_ref_clk");
	if (idx < 0) {
		pr_err("aux_ref_clk not provided\n");
		return;
	}
	input_clks[3] =	of_clk_get_parent_name(np, idx);

	idx = of_property_match_string(np, "clock-names", "gt_crx_ref_clk");
	if (idx < 0) {
		pr_err("aux_ref_clk not provided\n");
		return;
	}
	input_clks[4] =	of_clk_get_parent_name(np, idx);

	/* get clock output names from DT */
	for (i = 0; i < clk_max; i++) {
		if (of_property_read_string_index(np, "clock-output-names",
				  i, &clk_output_name[i])) {
			pr_err("%s: clock output name not in DT\n", __func__);
			BUG();
		}
	}
	/* APU clocks */
	acpu_parents[0] = clk_output_name[apll];
	acpu_parents[1] = dummy_nm;
	acpu_parents[2] = clk_output_name[dpll];
	acpu_parents[3] = clk_output_name[vpll];

	/* PLL clocks */
	zynqmp_clk_register_pll_clk(apll, clk_output_name[apll],
			CLK_IGNORE_UNUSED,
			(resource_size_t *)CRF_APB_APLL_CTRL,
			(resource_size_t *)CRF_APB_PLL_STATUS, 0);

	zynqmp_clk_register_pll_clk(dpll, clk_output_name[dpll], 0,
			(resource_size_t *)CRF_APB_DPLL_CTRL,
			(resource_size_t *)CRF_APB_PLL_STATUS, 1);

	zynqmp_clk_register_pll_clk(vpll, clk_output_name[vpll],
			CLK_IGNORE_UNUSED,
			(resource_size_t *)CRF_APB_VPLL_CTRL,
			(resource_size_t *)CRF_APB_PLL_STATUS, 2);

	zynqmp_clk_register_pll_clk(iopll, clk_output_name[iopll], 0,
			(resource_size_t *)CRL_APB_IOPLL_CTRL,
			(resource_size_t *)CRL_APB_PLL_STATUS, 0);

	zynqmp_clk_register_pll_clk(rpll, clk_output_name[rpll], 0,
			(resource_size_t *)CRL_APB_RPLL_CTRL,
			(resource_size_t *)CRL_APB_PLL_STATUS, 1);

	/* Domain crossing PLL clock dividers */
	clks[apll_to_lpd] = zynqmp_clk_register_divider(NULL, "apll_to_lpd",
			clk_output_name[apll], 0,
			(resource_size_t *)CRF_APB_APLL_TO_LPD_CTRL, 8,
			6, CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clks[dpll_to_lpd] = zynqmp_clk_register_divider(NULL, "dpll_to_lpd",
			clk_output_name[dpll], 0,
			(resource_size_t *)CRF_APB_DPLL_TO_LPD_CTRL, 8,
			6, CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clks[vpll_to_lpd] = zynqmp_clk_register_divider(NULL, "vpll_to_lpd",
			clk_output_name[vpll], 0,
			(resource_size_t *)CRF_APB_VPLL_TO_LPD_CTRL, 8,
			6,  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clks[iopll_to_fpd] = zynqmp_clk_register_divider(NULL, "iopll_to_fpd",
			clk_output_name[iopll], 0,
			(resource_size_t *)CRL_APB_IOPLL_TO_FPD_CTRL,
			8, 6, CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clks[rpll_to_fpd] = zynqmp_clk_register_divider(NULL, "rpll_to_fpd",
			clk_output_name[rpll], CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_RPLL_TO_FPD_CTRL, 8,
			6, CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clk = zynqmp_clk_register_mux(NULL, "acpu_mux", acpu_parents, 4,
			CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRF_APB_ACPU_CTRL, 0, 3, 0);

	clk = zynqmp_clk_register_divider(NULL, "acpu_div0", "acpu_mux", 0,
			(resource_size_t *)CRF_APB_ACPU_CTRL, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clks[acpu] = zynqmp_clk_register_gate(NULL, clk_output_name[acpu],
			"acpu_div0", CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			(resource_size_t *)CRF_APB_ACPU_CTRL, 24, 0);

	clk_prepare_enable(clks[acpu]);

	clk = clk_register_fixed_factor(NULL, "acpu_half_div", "acpu_div0", 0,
			1, 2);

	clks[acpu_half] = zynqmp_clk_register_gate(NULL,
			clk_output_name[acpu_half], "acpu_half_div",
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			(resource_size_t *)CRF_APB_ACPU_CTRL, 25, 0);

	/* Timers */
	/* The first parent clock source will be changed in the future.
	 * Currently, using the acpu clock as the parent based on the
	 * assumption that it comes from APB.
	 */
	wdt_ext_clk_mux_parents[0] = clk_output_name[topsw_lsbus];
	for (i = 0; i < ARRAY_SIZE(swdt_ext_clk_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				swdt_ext_clk_input_names[i]);
		if (idx >= 0)
			wdt_ext_clk_mux_parents[i + 1] =
				of_clk_get_parent_name(np, idx);
		else
			wdt_ext_clk_mux_parents[i + 1] = dummy_nm;
	}
	clks[wdt] = zynqmp_clk_register_mux(NULL, clk_output_name[wdt],
			wdt_ext_clk_mux_parents, 2,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_WDT_CLK_SEL, 0, 1, 0);

	/* DDR clocks */
	ddr_parents[0] = clk_output_name[dpll];
	ddr_parents[1] = clk_output_name[vpll];

	clk = zynqmp_clk_register_mux(NULL, "ddr_mux", ddr_parents, 2,
			CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRF_APB_DDR_CTRL, 0, 3, 0);

	clks[ddr_ref] = zynqmp_clk_register_divider(NULL,
			clk_output_name[ddr_ref],
			"ddr_mux", 0, (resource_size_t *)CRF_APB_DDR_CTRL, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clk_prepare_enable(clks[ddr_ref]);

	/* Peripheral clock parents */
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dbg_trace],
					iopll_to_fpd, dpll, apll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dbg_fpd],
					iopll_to_fpd, dpll, apll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dbg_lpd],
					rpll, iopll, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dbg_tstmp],
					iopll_to_fpd, dpll, apll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dp_video_ref],
					vpll, dpll, rpll_to_fpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dp_audio_ref],
					vpll, dpll, rpll_to_fpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dp_stc_ref],
					vpll, dpll, rpll_to_fpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gpu_ref],
					iopll_to_fpd, vpll, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[sata_ref],
					iopll_to_fpd, apll, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[pcie_ref],
					iopll_to_fpd, rpll_to_fpd, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gdma_ref],
					apll, vpll, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[dpdma_ref],
					apll, vpll, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[topsw_main],
					apll, vpll, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[topsw_lsbus],
					apll, iopll_to_fpd, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gtgref0_ref],
					iopll_to_fpd, apll, dpll);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[usb3_dual_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[usb0_bus_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[usb1_bus_ref],
					iopll, apll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gem0_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gem1_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gem2_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gem3_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[qspi_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[sdio0_ref],
					iopll, rpll, vpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[sdio1_ref],
					iopll, rpll, vpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[uart0_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[uart1_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[spi0_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[spi1_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[can0_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[can1_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[cpu_r5],
					rpll, iopll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[iou_switch],
					rpll, iopll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[csu_pll],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[pcap],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[lpd_switch],
					rpll, iopll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[lpd_lsbus],
					rpll, iopll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[nand_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[adma_ref],
					rpll, iopll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[gem_tsu_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[ams_ref],
					rpll, iopll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[i2c0_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[i2c1_ref],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[pl0],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[pl1],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[pl2],
					iopll, rpll, dpll_to_lpd);
	zynqmp_clk_get_parents(clk_output_name, periph_parents[pl3],
					iopll, rpll, dpll_to_lpd);

	/* PL clocks */
	zynqmp_clk_register_pl_clk(pl0, clk_output_name[pl0],
			(resource_size_t *)CRL_APB_PL0_REF_CTRL,
			periph_parents[pl0]);
	zynqmp_clk_register_pl_clk(pl1, clk_output_name[pl1],
			(resource_size_t *)CRL_APB_PL1_REF_CTRL,
			periph_parents[pl1]);
	zynqmp_clk_register_pl_clk(pl2, clk_output_name[pl2],
			(resource_size_t *)CRL_APB_PL2_REF_CTRL,
			periph_parents[pl2]);
	zynqmp_clk_register_pl_clk(pl3, clk_output_name[pl3],
			(resource_size_t *)CRL_APB_PL3_REF_CTRL,
			periph_parents[pl3]);

	/* Peripheral clock */
	zynqmp_clk_register_periph_clk(0, dbg_trace, clk_output_name[dbg_trace],
			CRF_APB_DBG_TRACE_CTRL, periph_parents[dbg_trace], 1,
			0, 24);

	zynqmp_clk_get_parents(clk_output_name, periph_parents[dbg_fpd],
					iopll_to_fpd, dpll, apll);
	zynqmp_clk_register_periph_clk(0, dbg_fpd, clk_output_name[dbg_fpd],
			CRF_APB_DBG_FPD_CTRL, periph_parents[dbg_fpd], 1, 0,
			24);

	zynqmp_clk_register_periph_clk(0, dbg_lpd, clk_output_name[dbg_lpd],
			CRL_APB_DBG_LPD_CTRL, periph_parents[dbg_lpd], 1, 0,
			24);

	zynqmp_clk_register_periph_clk(0, dbg_tstmp, clk_output_name[dbg_tstmp],
			CRF_APB_DBG_TSTMP_CTRL, periph_parents[dbg_tstmp], 0,
			0, 0);

	zynqmp_clk_register_periph_clk(CLK_SET_RATE_PARENT | CLK_FRAC,
				       dp_video_ref,
				       clk_output_name[dp_video_ref],
				       CRF_APB_DP_VIDEO_REF_CTRL,
				       periph_parents[dp_video_ref], 1, 1, 24);

	zynqmp_clk_register_periph_clk(CLK_SET_RATE_PARENT | CLK_FRAC,
				       dp_audio_ref,
				       clk_output_name[dp_audio_ref],
				       CRF_APB_DP_AUDIO_REF_CTRL,
				       periph_parents[dp_audio_ref], 1, 1, 24);

	zynqmp_clk_register_periph_clk(0, dp_stc_ref,
			clk_output_name[dp_stc_ref], CRF_APB_DP_STC_REF_CTRL,
			periph_parents[dp_stc_ref], 1, 1, 24);

	zynqmp_clk_register_periph_clk(0, gpu_ref, clk_output_name[gpu_ref],
			CRF_APB_GPU_REF_CTRL, periph_parents[gpu_ref], 1, 0,
			24);
	clks[gpu_pp0_ref] = zynqmp_clk_register_gate(NULL,
			clk_output_name[gpu_pp0_ref], "gpu_ref_div0",
			CLK_SET_RATE_PARENT,
			(resource_size_t *)CRF_APB_GPU_REF_CTRL, 25, 0);
	clks[gpu_pp1_ref] = zynqmp_clk_register_gate(NULL,
			clk_output_name[gpu_pp1_ref], "gpu_ref_div0",
			CLK_SET_RATE_PARENT,
			(resource_size_t *)CRF_APB_GPU_REF_CTRL, 26, 0);

	zynqmp_clk_register_periph_clk(0, sata_ref, clk_output_name[sata_ref],
			CRF_APB_SATA_REF_CTRL, periph_parents[sata_ref], 1, 0,
			24);

	zynqmp_clk_register_periph_clk(0, pcie_ref, clk_output_name[pcie_ref],
			CRF_APB_PCIE_REF_CTRL, periph_parents[pcie_ref], 1, 0,
			24);

	zynqmp_clk_register_periph_clk(0, gdma_ref, clk_output_name[gdma_ref],
			CRF_APB_GDMA_REF_CTRL, periph_parents[gdma_ref], 1, 0,
			24);

	zynqmp_clk_register_periph_clk(0, dpdma_ref, clk_output_name[dpdma_ref],
			CRF_APB_DPDMA_REF_CTRL, periph_parents[dpdma_ref], 1, 0,
			24);

	zynqmp_clk_register_periph_clk(0, topsw_main,
			clk_output_name[topsw_main],
			CRF_APB_TOPSW_MAIN_CTRL, periph_parents[topsw_main],
			CLK_IGNORE_UNUSED, 0, 24);

	zynqmp_clk_register_periph_clk(0, topsw_lsbus,
			clk_output_name[topsw_lsbus], CRF_APB_TOPSW_LSBUS_CTRL,
			periph_parents[topsw_lsbus], CLK_IGNORE_UNUSED, 0, 24);

	zynqmp_clk_register_periph_clk(0, gtgref0_ref,
			clk_output_name[gtgref0_ref], CRF_APB_GTGREF0_REF_CTRL,
			periph_parents[gtgref0_ref], 1, 0, 24);

	zynqmp_clk_register_periph_clk(0, usb3_dual_ref,
			clk_output_name[usb3_dual_ref],
			CRL_APB_USB3_DUAL_REF_CTRL,
			periph_parents[usb3_dual_ref], 1, 1, 25);

	zynqmp_clk_register_periph_clk(0, usb0_bus_ref,
			clk_output_name[usb0_bus_ref],
			CRL_APB_USB0_BUS_REF_CTRL,
			periph_parents[usb0_bus_ref], 1, 1, 25);

	clks[usb0] = zynqmp_clk_register_mux(NULL, clk_output_name[usb0],
			usb0_mio_mux_parents, 2,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRL_APB_USB0_BUS_REF_CTRL, 2, 1, 0);

	zynqmp_clk_register_periph_clk(0, usb1_bus_ref,
			clk_output_name[usb1_bus_ref],
			CRL_APB_USB1_BUS_REF_CTRL,
			periph_parents[usb1_bus_ref], 1, 1, 25);
	clks[usb1] = zynqmp_clk_register_mux(NULL, clk_output_name[usb1],
			usb1_mio_mux_parents, 2,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRL_APB_USB1_BUS_REF_CTRL, 2, 1, 0);

	/* Ethernet clocks */
	for (i = 0; i < ARRAY_SIZE(gem0_emio_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				gem0_emio_input_names[i]);
		if (idx >= 0)
			gem0_tx_mux_parents[i + 1] = of_clk_get_parent_name(np,
					idx);
	}
	clk = zynqmp_clk_register_mux(NULL, "gem0_ref_mux",
			periph_parents[gem0_ref], 4, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRL_APB_GEM0_REF_CTRL, 0, 3, 0);
	clk = zynqmp_clk_register_divider(NULL, "gem0_ref_div0", "gem0_ref_mux",
			0, (resource_size_t *)CRL_APB_GEM0_REF_CTRL, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clk = zynqmp_clk_register_divider(NULL, "gem0_ref_div1",
			"gem0_ref_div0", 0,
			(resource_size_t *)CRL_APB_GEM0_REF_CTRL, 16, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);

	clk = zynqmp_clk_register_mux(NULL, "gem0_tx_mux", gem0_tx_mux_parents,
			2, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_GEM_CLK_CTRL, 1, 1, 0);
	clks[gem0_rx] = zynqmp_clk_register_gate(NULL, clk_output_name[gem0_rx],
			"gem0_tx_mux",
			CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM0_REF_CTRL, 26, 0);
	clks[gem0_ref] = zynqmp_clk_register_gate(NULL,
			clk_output_name[gem0_ref],
			"gem0_ref_div1", CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM0_REF_CTRL, 25, 0);


	for (i = 0; i < ARRAY_SIZE(gem1_emio_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				gem1_emio_input_names[i]);
		if (idx >= 0)
			gem1_tx_mux_parents[i + 1] = of_clk_get_parent_name(np,
					idx);
	}

	clk = zynqmp_clk_register_mux(NULL, "gem1_ref_mux",
			periph_parents[gem1_ref],
			4, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRL_APB_GEM1_REF_CTRL, 0, 3, 0);
	clk = zynqmp_clk_register_divider(NULL, "gem1_ref_div0", "gem1_ref_mux",
			0, (resource_size_t *)CRL_APB_GEM1_REF_CTRL, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clk = zynqmp_clk_register_divider(NULL, "gem1_ref_div1",
			"gem1_ref_div0", 0,
			(resource_size_t *)CRL_APB_GEM1_REF_CTRL, 16, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clk = zynqmp_clk_register_mux(NULL, "gem1_tx_mux", gem1_tx_mux_parents,
			2, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_GEM_CLK_CTRL, 6, 1,	0);
	clks[gem1_rx] = zynqmp_clk_register_gate(NULL, clk_output_name[gem1_rx],
			"gem1_tx_mux", CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM1_REF_CTRL, 26, 0);
	clks[gem1_ref] = zynqmp_clk_register_gate(NULL,
			clk_output_name[gem1_ref], "gem1_ref_div1",
			CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM1_REF_CTRL, 25, 0);


	for (i = 0; i < ARRAY_SIZE(gem2_emio_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				gem2_emio_input_names[i]);
		if (idx >= 0)
			gem2_tx_mux_parents[i + 1] = of_clk_get_parent_name(np,
					idx);
	}
	clk = zynqmp_clk_register_mux(NULL, "gem2_ref_mux",
			periph_parents[gem2_ref], 4, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRL_APB_GEM2_REF_CTRL, 0, 3, 0);
	clk = zynqmp_clk_register_divider(NULL, "gem2_ref_div0",
			"gem2_ref_mux", 0,
			(resource_size_t *)CRL_APB_GEM2_REF_CTRL, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clk = zynqmp_clk_register_divider(NULL, "gem2_ref_div1",
			"gem2_ref_div0", 0,
			(resource_size_t *)CRL_APB_GEM2_REF_CTRL, 16, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clk = zynqmp_clk_register_mux(NULL, "gem2_tx_mux", gem2_tx_mux_parents,
			2, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_GEM_CLK_CTRL, 11, 1, 0);
	clks[gem2_rx] = zynqmp_clk_register_gate(NULL, clk_output_name[gem2_rx],
			"gem2_tx_mux", CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM2_REF_CTRL, 26, 0);
	clks[gem2_ref] = zynqmp_clk_register_gate(NULL,
			clk_output_name[gem2_ref], "gem2_ref_div1",
			CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM2_REF_CTRL, 25, 0);


	for (i = 0; i < ARRAY_SIZE(gem3_emio_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				gem3_emio_input_names[i]);
		if (idx >= 0)
			gem3_tx_mux_parents[i + 1] = of_clk_get_parent_name(np,
					idx);
	}

	clk = zynqmp_clk_register_mux(NULL, "gem3_ref_mux",
			periph_parents[gem3_ref], 4, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRL_APB_GEM3_REF_CTRL, 0,
			3, 0);
	clk = zynqmp_clk_register_divider(NULL, "gem3_ref_div0",
			"gem3_ref_mux", 0,
			(resource_size_t *)CRL_APB_GEM3_REF_CTRL, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clk = zynqmp_clk_register_divider(NULL, "gem3_ref_div1",
			"gem3_ref_div0", CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM3_REF_CTRL, 16, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clk = zynqmp_clk_register_mux(NULL, "gem3_tx_mux", gem3_tx_mux_parents,
			2, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_GEM_CLK_CTRL, 16, 1, 0);
	clks[gem3_rx] = zynqmp_clk_register_gate(NULL, clk_output_name[gem3_rx],
			"gem3_tx_mux", CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM3_REF_CTRL, 26, 0);
	clks[gem3_ref] = zynqmp_clk_register_gate(NULL,
			clk_output_name[gem3_ref], "gem3_ref_div1",
			CLK_SET_RATE_PARENT,
			(resource_size_t *)CRL_APB_GEM3_REF_CTRL, 25, 0);

	gem_tsu_mux_parents[0] = clk_output_name[gem_tsu_ref];
	gem_tsu_mux_parents[1] = clk_output_name[gem_tsu_ref];
	gem_tsu_mux_parents[2] = "mio_clk_26";
	gem_tsu_mux_parents[3] = "mio_clk_50_or_51";

	zynqmp_clk_register_periph_clk(0, gem_tsu_ref,
			clk_output_name[gem_tsu_ref], CRL_APB_GEM_TSU_REF_CTRL,
			periph_parents[gem_tsu_ref], 1, 1, 24);

	clks[gem_tsu] = zynqmp_clk_register_mux(NULL, clk_output_name[gem_tsu],
			gem_tsu_mux_parents, 2,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_GEM_CLK_CTRL, 20, 2, 0);

	zynqmp_clk_register_periph_clk(0, qspi_ref, clk_output_name[qspi_ref],
			CRL_APB_QSPI_REF_CTRL, periph_parents[qspi_ref], 1, 1,
			24);

	zynqmp_clk_register_periph_clk(0, sdio0_ref, clk_output_name[sdio0_ref],
			CRL_APB_SDIO0_REF_CTRL, periph_parents[sdio0_ref], 1,
			1, 24);

	zynqmp_clk_register_periph_clk(0, sdio1_ref, clk_output_name[sdio1_ref],
			CRL_APB_SDIO1_REF_CTRL, periph_parents[sdio1_ref], 1,
			1, 24);

	zynqmp_clk_register_periph_clk(0, uart0_ref, clk_output_name[uart0_ref],
			CRL_APB_UART0_REF_CTRL, periph_parents[uart0_ref], 1,
			1, 24);

	zynqmp_clk_register_periph_clk(0, uart1_ref, clk_output_name[uart1_ref],
			CRL_APB_UART1_REF_CTRL, periph_parents[uart1_ref], 1,
			1, 24);

	zynqmp_clk_register_periph_clk(0, spi0_ref, clk_output_name[spi0_ref],
			CRL_APB_SPI0_REF_CTRL, periph_parents[spi0_ref], 1, 1,
			24);

	zynqmp_clk_register_periph_clk(0, spi1_ref, clk_output_name[spi1_ref],
			CRL_APB_SPI1_REF_CTRL, periph_parents[spi1_ref], 1, 1,
			24);

	tmp = strlen("mio_clk_00x");
	clk_name = kmalloc(tmp, GFP_KERNEL);
	for (i = 0; i < NUM_MIO_PINS; i++) {
		int idx;

		snprintf(clk_name, tmp, "mio_clk_%2.2d", i);
		idx = of_property_match_string(np, "clock-names", clk_name);
		if (idx >= 0)
			can_mio_mux_parents[i] = of_clk_get_parent_name(np,
						idx);
		else
			can_mio_mux_parents[i] = dummy_nm;
	}
	kfree(clk_name);
	zynqmp_clk_register_periph_clk(0, can0_ref, clk_output_name[can0_ref],
			CRL_APB_CAN0_REF_CTRL, periph_parents[can0_ref], 1, 1,
			24);
	clk = zynqmp_clk_register_mux(NULL, "can0_mio_mux",
			can_mio_mux_parents, NUM_MIO_PINS,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_CAN_MIO_CTRL, 0, 7, 0);
	clks[can0] = zynqmp_clk_register_mux(NULL, clk_output_name[can0],
			can0_mio_mux2_parents, 2,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_CAN_MIO_CTRL, 7, 1, 0);

	zynqmp_clk_register_periph_clk(0, can1_ref, clk_output_name[can1_ref],
			CRL_APB_CAN1_REF_CTRL, periph_parents[can1_ref], 1, 1,
			24);
	clk = zynqmp_clk_register_mux(NULL, "can1_mio_mux",
			can_mio_mux_parents, NUM_MIO_PINS,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_CAN_MIO_CTRL, 15, 7, 0);
	clks[can1] = zynqmp_clk_register_mux(NULL, clk_output_name[can1],
			can1_mio_mux2_parents, 2,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)IOU_SLCR_CAN_MIO_CTRL, 22, 1, 0);

	zynqmp_clk_register_periph_clk(0, cpu_r5, clk_output_name[cpu_r5],
			CRL_APB_CPU_R5_CTRL, periph_parents[cpu_r5],
			CLK_IGNORE_UNUSED, 0, 24);
	clk = zynqmp_clk_register_gate(NULL, "cpu_r5_core_gate", "cpu_r5_div0",
			CLK_IGNORE_UNUSED,
			(resource_size_t *)CRL_APB_CPU_R5_CTRL, 25, 0);

	zynqmp_clk_register_periph_clk(0, iou_switch,
			clk_output_name[iou_switch], CRL_APB_IOU_SWITCH_CTRL,
			periph_parents[iou_switch], CLK_IGNORE_UNUSED, 0, 24);

	zynqmp_clk_register_periph_clk(0, csu_pll, clk_output_name[csu_pll],
			CRL_APB_CSU_PLL_CTRL, periph_parents[csu_pll], 1, 0,
			24);

	zynqmp_clk_register_periph_clk(0, pcap, clk_output_name[pcap],
			CRL_APB_PCAP_CTRL, periph_parents[pcap], 1, 0, 24);

	zynqmp_clk_register_periph_clk(0, lpd_switch,
			clk_output_name[lpd_switch], CRL_APB_LPD_SWITCH_CTRL,
			periph_parents[lpd_switch], CLK_IGNORE_UNUSED, 0, 24);

	zynqmp_clk_register_periph_clk(0, lpd_lsbus, clk_output_name[lpd_lsbus],
			CRL_APB_LPD_LSBUS_CTRL, periph_parents[lpd_lsbus],
			CLK_IGNORE_UNUSED, 0, 24);

	zynqmp_clk_register_periph_clk(0, nand_ref, clk_output_name[nand_ref],
			CRL_APB_NAND_REF_CTRL, periph_parents[nand_ref], 1, 1,
			24);

	zynqmp_clk_register_periph_clk(0, adma_ref, clk_output_name[adma_ref],
			CRL_APB_ADMA_REF_CTRL, periph_parents[adma_ref], 1, 0,
			24);

	dll_ref_parents[0] = clk_output_name[iopll];
	dll_ref_parents[1] = clk_output_name[rpll];
	clks[dll_ref] = zynqmp_clk_register_mux(NULL, clk_output_name[dll_ref],
				dll_ref_parents, 2,
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				(resource_size_t *)CRL_APB_DLL_REF_CTRL,
				0, 3, 0);

	zynqmp_clk_register_periph_clk(0, ams_ref, clk_output_name[ams_ref],
			CRL_APB_AMS_REF_CTRL, periph_parents[ams_ref], 1, 1,
			24);

	zynqmp_clk_register_periph_clk(0, i2c0_ref, clk_output_name[i2c0_ref],
			CRL_APB_I2C0_REF_CTRL, periph_parents[i2c0_ref], 1,
			1, 24);

	zynqmp_clk_register_periph_clk(0, i2c1_ref, clk_output_name[i2c1_ref],
			CRL_APB_I2C1_REF_CTRL, periph_parents[i2c1_ref], 1,
			1, 24);

	timestamp_ref_parents[0] = clk_output_name[rpll];
	timestamp_ref_parents[1] = dummy_nm;
	timestamp_ref_parents[2] = clk_output_name[iopll];
	timestamp_ref_parents[3] = clk_output_name[dpll_to_lpd];
	timestamp_ref_parents[4] = input_clks[0];
	timestamp_ref_parents[5] = input_clks[0];
	timestamp_ref_parents[6] = input_clks[0];
	timestamp_ref_parents[7] = input_clks[0];
	clk = zynqmp_clk_register_mux(NULL, "timestamp_ref_mux",
			timestamp_ref_parents, 8, CLK_SET_RATE_NO_REPARENT,
			(resource_size_t *)CRL_APB_TIMESTAMP_REF_CTRL, 0, 3, 0);
	clk = zynqmp_clk_register_divider(NULL, "timestamp_ref_div0",
			"timestamp_ref_mux", 0,
			(resource_size_t *)CRL_APB_TIMESTAMP_REF_CTRL,
			8, 6, CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO);
	clks[timestamp_ref] = zynqmp_clk_register_gate(NULL,
			clk_output_name[timestamp_ref], "timestamp_ref_div0",
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			(resource_size_t *)CRL_APB_TIMESTAMP_REF_CTRL, 24, 0);

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		if (IS_ERR(clks[i])) {
			pr_err("Zynq Ultrascale+ MPSoC clk %d: register failed with %ld\n",
			       i, PTR_ERR(clks[i]));
			BUG();
		}
	}

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

static int __init zynqmp_clock_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp-clkc");
	if (!np) {
		pr_err("%s: clkc node not found\n", __func__);
		of_node_put(np);
		return 0;
	}

	zynqmp_clk_setup(np);
	return 0;
}
arch_initcall(zynqmp_clock_init);

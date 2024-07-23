// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 *
 * References:
 *
 * https://docs.amd.com/r/en-US/Vitis_Libraries/vision/index.html
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#include "xilinx-gamma-correction.h"
#include "xilinx-isp-params.h"
#include "xilinx-vip.h"

/*
 * XISP_COMMON_CONFIG_REG
 * 0-15: Width, 16-31: Height
 *
 * XISP_PIPELINE_CONFIG_INFO_REG (READ REGISTER)
 * 0: IN_TYPE, 1: IN_BW_MODE, 4: OUT_TYPE, 5: OUT_BW_MODE,
 * 8: NPC, 16-20: NUM_STREAMS
 *
 * XISP_MAX_SUPPORTED_SIZE_REG (READ REGISTER)
 * 0-15: MAX_WIDTH, 16-31: MAX_HEIGHT
 *
 * XISP_FUNCS_AVAILABLE_REG (Compile time bypass) (READ REGISTER)
 * 0: HDR_MODE, 1: HDR_EN, 2: RGBIR_EN, 3: AEC_EN, 4: BLC_EN,
 * 5: BPC_EN, 6: DEGAMMA_EN, 7: LSC_EN, 8: GAIN_EN, 9: DEMOSAIC_EN,
 * 10: AWB_EN, 11: CCM_EN, 12: TM_EN, 13: TM_TYPE, 14: GAMMA_EN,
 * 15: 3DLUT_EN, 16: CSC_EN, 17: BAYER_STATS_EN, 18: LUMA_STATS_EN,
 * 19: RGB_STATS_EN, 20: CLAHE_EN, 21: MEDIAN_EN, 22: RESIZE_EN
 *
 * XISP_FUNCS_BYPASSABLE_REG (Compile time bypass) (READ REGISTER)
 * 0: ISP_BYPASS_EN, 1: HDR_BYPASS_EN, 2: RGBIR_BYPASS_EN,
 * 3: AEC_BYPASS_EN, 4: BLC_BYPASS_EN, 5: BPC_BYPASS_EN,
 * 6: DEGAMMA_BYPASS_EN, 7: LSC_BYPASS_EN, 8: GAIN_BYPASS_EN,
 * 9: DEMOSAIC_BYPASS_EN, 10: AWB_BYPASS_EN, 11: CCM_BYPASS_EN,
 * 12: TM_BYPASS_EN, 14: GAMMA_BYPASS_EN, 15: 3DLUT_BYPASS_EN,
 * 16: CSC_BYPASS_EN, 17: BAYER_STATS_BYPASS_EN, 18: LUMA_STATS_BYPASS_EN,
 * 19: RGB_STATS_BYPASS_EN, 20: CLAHE_BYPASS_EN, 21: MEDIAN_BYPASS_EN,
 * 22: RESIZE_BYPASS_EN
 *
 * XISP_FUNCS_BYPASS_CONFIG_REG (Runtime bypass) (WRITE REGISTER)
 * 0: BYPASS_ISP, 1: BYPASS_HDR, 2: BYPASS_RGBIR, 3: BYPASS_AEC,
 * 4: BYPASS_BLC, 5: BYPASS_BPC, 6: BYPASS_DEGAMMA, 7: BYPASS_LSC,
 * 8: BYPASS_GAIN, 9: BYPASS_DEMOSAIC, 10: BYPASS_AWB, 11: BYPASS_CCM,
 * 12: BYPASS_TM, 14: BYPASS_GAMMA, 15: BYPASS_3DLUT, 16: BYPASS_CSC,
 * 17: BYPASS_BAYER_STATS, 18: BYPASS_LUMA_STATS, 19: BYPASS_RGB_STATS,
 * 20: BYPASS_CLAHE, 21: BYPASS_MEDIAN, 22: BYPASS_RESIZE
 *
 * XISP_AEC_CONFIG_REG
 * 0-15: threshold_aec, 16-31: Reserved
 *
 * XISP_BLC_CONFIG_1_REG
 * 0-31: mult_factor
 *
 * XISP_BLC_CONFIG_2_REG
 * 0-31: black_level
 *
 * XISP_AWB_CONFIG_REG
 * 0-15: threshold_awb, 16-31: Reserved
 *
 * XISP_DEGAMMA_CONFIG_BASE
 * 0-63: Base address in the device's register space for DeGamma params
 */
#define XISP_COMMON_CONFIG_REG		(0x10UL)
#define XISP_PIPELINE_CONFIG_INFO_REG		(0x80UL)
#define XISP_MAX_SUPPORTED_SIZE_REG		(0x90UL)
#define XISP_FUNCS_AVAILABLE_REG			(0xa0UL)
#define XISP_FUNCS_BYPASSABLE_REG		(0xb0UL)
#define XISP_FUNCS_BYPASS_CONFIG_REG		(0xc0UL)
#define XISP_AEC_CONFIG_REG			(0x18UL)
#define XISP_BLC_CONFIG_1_REG			(0x28UL)
#define XISP_BLC_CONFIG_2_REG			(0x30UL)
#define XISP_AWB_CONFIG_REG				(0x20UL)
#define XISP_DEGAMMA_CONFIG_BASE		(0x1000UL)
#define XISP_AP_CTRL_REG		(0x0)
#define XISP_WIDTH_REG			(0x10)
#define XISP_HEIGHT_REG			(0x18)
#define XISP_MODE_REG			(0x20)
#define XISP_INPUT_BAYER_FORMAT_REG	(0x28)
#define XISP_RGAIN_REG			(0x30)
#define XISP_BGAIN_REG			(0x38)
#define XISP_PAWB_REG			(0x54)
#define XISP_GAMMA_RED_REG		(0x800)
#define XISP_GAMMA_BLUE_REG		(0x900)
#define XISP_GAMMA_GREEN_REG		(0xA00)

#define XISP_MAX_HEIGHT			(4320)
#define XISP_MAX_WIDTH			(8192)
#define XISP_MIN_HEIGHT			(64)
#define XISP_MIN_WIDTH			(64)
#define XISP_GAMMA_LUT_LEN		(64)
#define XISP_MIN_VALUE                       (0)
#define XISP_MAX_VALUE                       (65535)
#define XISP_NO_OF_PADS			(2)

#define XISP_RESET_DEASSERT		(0)
#define XISP_RESET_ASSERT		(1)
#define XISP_START			BIT(0)
#define XISP_AUTO_RESTART		BIT(7)
#define XISP_STREAM_ON			(XISP_AUTO_RESTART | XISP_START)
#define XILINX_ISP_VERSION_1					BIT(0)
#define XILINX_ISP_VERSION_2					BIT(1)
#define XISP_AEC_THRESHOLD_DEFAULT	(20)
#define XGET_BIT(bitmask, reg)    (FIELD_GET(BIT(bitmask), (reg)))
#define XISP_SET_CFG(INDEX, REG, VAL)	\
	do {	\
		u32 _index = (INDEX); \
		bool en = XGET_BIT(_index, xisp->module_en); \
		bool byp_en = XGET_BIT(_index, xisp->module_bypass_en); \
		bool byp = XGET_BIT(_index, xisp->module_bypass); \
		\
		if (en)	\
			if (!byp_en || !(byp_en && byp)) \
				xvip_write(&xisp->xvip, (REG), (VAL)); \
	} while (0)
#define XISP_BLC_MULTIPLICATION_FACTOR_MAX	(0x80000000UL)
#define XISP_BLC_MULTIPLICATION_FACTOR_DEFALUT	(65535)
#define XISP_BLC_BLACK_LEVEL_DEFALUT		(32)
#define XISP_AWB_THRESHOLD_DEFAULT		(512)

enum xisp_bayer_format {
	XISP_RGGB = 0,
	XISP_GRBG,
	XISP_GBRG,
	XISP_BGGR,
};

/* enumerations for pipeline_config_info index */
enum xisp_pipeline_config_info_index {
	XISP_IN_TYPE_INDEX = 0,
	XISP_IN_BW_MODE_INDEX = 1,
	XISP_OUT_TYPE_INDEX = 4,
	XISP_OUT_BW_MODE_INDEX = 5,
	XISP_NPC_INDEX = 8,
	XISP_NUM_STREAMS_INDEX = 12
};

/* enumerations for max_supported_size index */
enum xisp_max_supported_size_index {
	XISP_MAX_WIDTH_INDEX = 0,
	XISP_MAX_HEIGHT_INDEX = 16
};

/* enumerations for functions_bypassable index */
enum xisp_functions_bypassable_index {
	XISP_AEC_INDEX = 3,
	XISP_BLC_INDEX = 4,
	XISP_BPC_INDEX = 5,
	XISP_DEGAMMA_INDEX = 6,
	XISP_AWB_INDEX = 10
};

/**
 * struct xilinx_isp_feature - dt or IP property structure
 * @flags: Bitmask of properties enabled in IP or dt
 */
struct xilinx_isp_feature {
	u32 flags;
};

/*
 * struct xisp_dev - Xilinx ISP pipeline device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: V4L2 media bus formats
 * @ctrl_handler: V4L2 Control Handler
 * @bayer_fmt: IP or Hardware specific video format
 * @rst_gpio: GPIO reset line to bring ISP pipeline out of reset
 * @config: Pointer to Framebuffer Feature config struct
 * @npads: number of pads
 * @max_width: Maximum width supported by this instance
 * @max_height: Maximum height supported by this instance
 * @width: Current frame width
 * @height: Current frame height
 * @ip_max_res: Maximum resolution supported by this instance
 * @module_conf: Expected module configuration
 * @module_bypass: Track for modules bypassed or not from user
 * @module_bypass_en: Track if modules can be bypassed or not
 * @module_en: Track which module is enabled
 * @mult_factor: Expected multiplication factor
 * @black_level: Expected black level
 * @rgain: Expected red gain
 * @bgain: Expected blue gain
 * @mode_reg: Track if AWB is enabled or not
 * @pawb: Expected threshold
 * @red_lut: Pointer to the gamma coefficient as per the Red Gamma control
 * @green_lut: Pointer to the gamma coefficient as per the Green Gamma control
 * @blue_lut: Pointer to the gamma coefficient as per the Blue Gamma control
 * @gamma_table: Pointer to the table containing various gamma values
 * @threshold_aec: Expected threshold for auto exposure correction
 * @threshold_awb: Expected threshold for auto white balance
 * @degamma_select: Expected degamma array values
 * @degamma_lut: Pointer to the degamma coefficient as per the degamma control
 */
struct xisp_dev {
	struct xvip_device xvip;
	struct media_pad pads[XISP_NO_OF_PADS];
	struct v4l2_mbus_framefmt formats[XISP_NO_OF_PADS];
	struct v4l2_ctrl_handler ctrl_handler;
	enum xisp_bayer_format bayer_fmt;
	struct gpio_desc *rst_gpio;
	const struct xilinx_isp_feature *config;
	u32 ip_max_res;
	u32 module_conf;
	u32 module_bypass;
	u32 module_bypass_en;
	u32 module_en;
	u32 mult_factor;
	u32 black_level;
	u16 npads;
	u16 width;
	u16 height;
	u16 max_width;
	u16 max_height;
	u16 rgain;
	u16 bgain;
	bool mode_reg;
	u16 pawb;
	u8 degamma_select;
	const u32 *red_lut;
	const u32 *green_lut;
	const u32 *blue_lut;
	const u32 **gamma_table;
	u16 threshold_aec;
	u16 threshold_awb;
	const u32 (*degamma_lut)[XISP_DEGAMMA_KNEE_POINTS][XISP_DEGAMMA_PARAMS];
};

static const struct xilinx_isp_feature xlnx_isp_cfg_v10 = {
	.flags = XILINX_ISP_VERSION_1,
};

static const struct xilinx_isp_feature xlnx_isp_cfg_v20 = {
	.flags = XILINX_ISP_VERSION_2,
};

static const struct of_device_id xisp_of_id_table[] = {
	{.compatible = "xlnx,isppipeline-1.0", .data = (void *)&xlnx_isp_cfg_v10},
	{.compatible = "xlnx,isppipeline-2.0", .data = (void *)&xlnx_isp_cfg_v20},
	{ }
};

static inline struct xisp_dev *to_xisp(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xisp_dev, xvip.subdev);
}

/*
 * xisp_set_lut_entries - Write to a field in ISP pipeline registers
 *
 * @xisp:	The xisp_dev
 * @lut:	The value to write
 * @lut_base:	The field to write to
 *
 * This function allows writing to gamma lut array.
 */

static void xisp_set_lut_entries(struct xisp_dev *xisp, const u32 *lut, const u32 lut_base)
{
	int itr;
	u32 lut_offset;

	lut_offset = lut_base;

	for (itr = 0; itr < XISP_GAMMA_LUT_LEN; itr = itr + 1) {
		xvip_write(&xisp->xvip, lut_offset, lut[itr]);
		lut_offset += 4;
	}
}

static void select_gamma(u32 value, const u32 **coeff, const u32 **xgamma_curves)
{
	*coeff = *(xgamma_curves + value - 1);
}

/**
 * xisp_module_bypass - Modify a specific bit in a given value
 * @in_val: The input value
 * @position: The bit position to modify
 * @bit_val: The new value of the bit (true or false)
 *
 * This function takes an input value and modifies a specific bit at the given
 * position to the new value specified by bit_val. The modified value is then
 * returned.
 *
 * Return: The modified value with the bit at 'position' set to 'bit_val'
 */
static int xisp_module_bypass(u32 in_val, u32 position, bool bit_val)
{
	u32 mask = 1 << position;

	return ((in_val & ~mask) | (bit_val << position));
}

/**
 * xisp_set_degamma_entries - Set degamma entries for the XISP device.
 * @xisp: Pointer to the xisp_dev structure representing the device.
 * @degamma_base: Base address in the device's register space where degamma entries will be written.
 * @degamma: Pointer to a 3-dimensional array containing the degamma values to be written.
 *           The dimensions of this array are defined by XISP_DEGAMMA_COLOR_ID,
 *           XISP_DEGAMMA_KNEE_POINTS, and XISP_DEGAMMA_PARAMS.
 *
 * This function writes the degamma values from the provided 3D array to the device's registers.
 * The degamma values are written sequentially starting from the address specified by degamma_base.
 * Each value is written to an offset that increments by 4 bytes (32 bits) for each subsequent
 * value.
 *
 * The loops iterate through:
 * - i: Color ID, ranging from 0 to XISP_DEGAMMA_COLOR_ID - 1
 * - j: Knee points, ranging from 0 to XISP_DEGAMMA_KNEE_POINTS - 1
 * - k: Parameters, ranging from 0 to XISP_DEGAMMA_PARAMS - 1
 *
 * Inside the innermost loop, the function calls xvip_write() to write each degamma value to the
 * device's register at the current offset, and then increments the offset by 4 bytes.
 */
static void xisp_set_degamma_entries(struct xisp_dev *xisp, const u32 degamma_base,
				     const u32 (*degamma)[XISP_DEGAMMA_KNEE_POINTS]
							 [XISP_DEGAMMA_PARAMS])
{
	int idx;
	u32 degamma_offset;
	int total_elements = XISP_DEGAMMA_COLOR_ID * XISP_DEGAMMA_KNEE_POINTS * XISP_DEGAMMA_PARAMS;
	const u32 *degamma_ptr = &degamma[0][0][0];

	degamma_offset = degamma_base;

	for (idx = 0; idx < total_elements; idx++)
		xvip_write(&xisp->xvip, degamma_offset + (idx * 4), degamma_ptr[idx]);
}

static int xisp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xisp_dev *xisp =
		container_of(ctrl->handler,
			     struct xisp_dev, ctrl_handler);
	bool degamma_enabled, degamma_bypass_enabled, degamma_bypass;

	switch (ctrl->id) {
	case V4L2_CID_XILINX_ISP_AEC_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_AEC_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_AEC_THRESHOLD:
		xisp->threshold_aec = ctrl->val;
		XISP_SET_CFG(XISP_AEC_INDEX, XISP_AEC_CONFIG_REG, xisp->threshold_aec);
		break;
	case V4L2_CID_XILINX_ISP_AWB_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_AWB_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_AWB_THRESHOLD:
		xisp->threshold_awb = ctrl->val;
		XISP_SET_CFG(XISP_AWB_INDEX, XISP_AWB_CONFIG_REG, xisp->threshold_awb);
		break;
	case V4L2_CID_XILINX_ISP_BLC_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_BLC_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_MULTI_FACTOR:
		xisp->mult_factor = ctrl->val;
		XISP_SET_CFG(XISP_BLC_INDEX, XISP_BLC_CONFIG_1_REG, xisp->mult_factor);
		break;
	case V4L2_CID_BLACK_LEVEL:
		xisp->black_level = ctrl->val;
		XISP_SET_CFG(XISP_BLC_INDEX, XISP_BLC_CONFIG_2_REG, xisp->black_level);
		break;
	case V4L2_CID_XILINX_ISP_BPC_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_BPC_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_DEGAMMA_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_DEGAMMA_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_DEGAMMA_PARAMS:
		xisp->degamma_select = ctrl->val;
		degamma_enabled = XGET_BIT(XISP_DEGAMMA_INDEX, xisp->module_en);
		degamma_bypass_enabled = XGET_BIT(XISP_DEGAMMA_INDEX, xisp->module_bypass_en);
		degamma_bypass = XGET_BIT(XISP_DEGAMMA_INDEX, xisp->module_bypass);

		if (degamma_enabled)
			if (!degamma_bypass_enabled || !(degamma_bypass_enabled &&
							 degamma_bypass)) {
				xisp->degamma_lut = xisp_degamma_choices[xisp->degamma_select];
				xisp_set_degamma_entries(xisp, XISP_DEGAMMA_CONFIG_BASE,
							 xisp->degamma_lut);
			}
		break;
	case V4L2_CID_XILINX_ISP_RED_GAIN:
		xisp->rgain = ctrl->val;
		xvip_write(&xisp->xvip, XISP_RGAIN_REG, xisp->rgain);
		break;
	case V4L2_CID_XILINX_ISP_BLUE_GAIN:
		xisp->bgain = ctrl->val;
		xvip_write(&xisp->xvip, XISP_BGAIN_REG, xisp->bgain);
		break;
	case V4L2_CID_XILINX_ISP_AWB:
		xisp->mode_reg = ctrl->val;
		xvip_write(&xisp->xvip, XISP_MODE_REG, xisp->mode_reg);
		break;
	case V4L2_CID_XILINX_ISP_THRESHOLD:
		xisp->pawb = ctrl->val;
		xvip_write(&xisp->xvip, XISP_PAWB_REG, xisp->pawb);
		break;
	case V4L2_CID_XILINX_ISP_RED_GAMMA:
		select_gamma(ctrl->val, &xisp->red_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Red Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		xisp_set_lut_entries(xisp, xisp->red_lut, XISP_GAMMA_RED_REG);
		break;
	case V4L2_CID_XILINX_ISP_GREEN_GAMMA:
		select_gamma(ctrl->val, &xisp->green_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Green Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		xisp_set_lut_entries(xisp, xisp->green_lut, XISP_GAMMA_GREEN_REG);
		break;
	case V4L2_CID_XILINX_ISP_BLUE_GAMMA:
		select_gamma(ctrl->val, &xisp->blue_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Blue Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		xisp_set_lut_entries(xisp, xisp->blue_lut, XISP_GAMMA_BLUE_REG);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops xisp_ctrl_ops = {
	.s_ctrl = xisp_s_ctrl,
};

static struct v4l2_ctrl_config xisp_ctrls_aec[] = {
	/* AEC ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_AEC_EN,
		.name = "bypass_aec",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* AEC THRESHOLD */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_AEC_THRESHOLD,
		.name = "aec_threshold",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_AEC_THRESHOLD_DEFAULT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_awb[] = {
	/* AWB ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_AWB_EN,
		.name = "bypass_awb",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* AWB THRESHOLD */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_AWB_THRESHOLD,
		.name = "awb_threshold",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_AWB_THRESHOLD_DEFAULT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_blc[] = {
	/* BLC ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLC_EN,
		.name = "bypass_blc",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* MULTIPLICATION FACTOR */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_MULTI_FACTOR,
		.name = "blc_multiplication_factor",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_BLC_MULTIPLICATION_FACTOR_MAX,
		.step = 1,
		.def = XISP_BLC_MULTIPLICATION_FACTOR_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* BLACK LEVEL */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_BLACK_LEVEL,
		.name = "blc_black_level",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_BLC_BLACK_LEVEL_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_bpc[] = {
	/* BPC ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BPC_EN,
		.name = "bypass_bpc",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_degamma[] = {
	/* DEGAMMA ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_DEGAMMA_EN,
		.name = "bypass_degamma",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* SELECT DEGAMMA PARAMS */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_DEGAMMA_PARAMS,
		.name = "select_degamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls[] = {
	/* Red Gain */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RED_GAIN,
		.name = "red_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 65535,
		.step = 1,
		.def = 100,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Blue Gain */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLUE_GAIN,
		.name = "blue_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 65535,
		.step = 1,
		.def = 350,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* AWB Enable */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_AWB,
		.name = "awb_en",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 1,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* THRESHOLD */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_THRESHOLD,
		.name = "threshold",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 65535,
		.step = 1,
		.def = 512,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Red Gamma */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RED_GAMMA,
		.name = "red_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 20,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Green Gamma */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GREEN_GAMMA,
		.name = "green_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 15,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Blue Gamma */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLUE_GAMMA,
		.name = "blue_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 20,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_mbus_framefmt
*__xisp_get_pad_format(struct xisp_dev *xisp,
			struct v4l2_subdev_state *sd_state,
			unsigned int pad, u32 which)
{
	struct v4l2_mbus_framefmt *get_fmt;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		get_fmt = v4l2_subdev_get_try_format(&xisp->xvip.subdev,
						     sd_state, pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		get_fmt = &xisp->formats[pad];
		break;
	default:
		get_fmt = NULL;
		break;
	}

	return get_fmt;
}

/*
 * xisp_reset - Reset ISP pipeline IP
 */
static void xisp_reset(struct xisp_dev *xisp)
{
	/* reset ip */
	gpiod_set_value_cansleep(xisp->rst_gpio, XISP_RESET_ASSERT);
	udelay(1);
	gpiod_set_value_cansleep(xisp->rst_gpio, XISP_RESET_DEASSERT);
}

static int xisp_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xisp_dev *xisp = to_xisp(subdev);

	if (!enable) {
		dev_dbg(xisp->xvip.dev, "%s : Off", __func__);
		xisp_reset(xisp);
		return 0;
	}

	if (xisp->config->flags & XILINX_ISP_VERSION_1) {
		xvip_write(&xisp->xvip, XISP_WIDTH_REG, xisp->formats[XVIP_PAD_SINK].width);
		xvip_write(&xisp->xvip, XISP_HEIGHT_REG, xisp->formats[XVIP_PAD_SINK].height);
		xvip_write(&xisp->xvip, XISP_INPUT_BAYER_FORMAT_REG, xisp->bayer_fmt);
		xvip_write(&xisp->xvip, XISP_RGAIN_REG, xisp->rgain);
		xvip_write(&xisp->xvip, XISP_BGAIN_REG, xisp->bgain);
		xvip_write(&xisp->xvip, XISP_MODE_REG, xisp->mode_reg);
		xvip_write(&xisp->xvip, XISP_PAWB_REG, xisp->pawb);
		xisp_set_lut_entries(xisp, xisp->red_lut, XISP_GAMMA_RED_REG);
		xisp_set_lut_entries(xisp, xisp->green_lut, XISP_GAMMA_GREEN_REG);
		xisp_set_lut_entries(xisp, xisp->blue_lut, XISP_GAMMA_BLUE_REG);
	} else if (xisp->config->flags & XILINX_ISP_VERSION_2) {
		xisp->width = xisp->formats[XVIP_PAD_SINK].width;
		xisp->height = xisp->formats[XVIP_PAD_SINK].height;
		xvip_write(&xisp->xvip, XISP_COMMON_CONFIG_REG, (xisp->height << 16) | xisp->width);
	} else {
		return -EINVAL;
	}
	/* Start ISP pipeline IP */
	xvip_write(&xisp->xvip, XISP_AP_CTRL_REG, XISP_STREAM_ON);

	return 0;
}

static const struct v4l2_subdev_video_ops xisp_video_ops = {
	.s_stream = xisp_s_stream,
};

static int xisp_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct xisp_dev *xisp = to_xisp(subdev);
	struct v4l2_mbus_framefmt *get_fmt;

	get_fmt = __xisp_get_pad_format(xisp, sd_state, fmt->pad, fmt->which);
	if (!get_fmt)
		return -EINVAL;

	fmt->format = *get_fmt;

	return 0;
}

static bool
xisp_get_bayer_format(struct xisp_dev *xisp, u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		fallthrough;
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		fallthrough;
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		fallthrough;
	case MEDIA_BUS_FMT_SRGGB16_1X16:
		xisp->bayer_fmt = XISP_RGGB;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		fallthrough;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
		fallthrough;
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		fallthrough;
	case MEDIA_BUS_FMT_SGRBG16_1X16:
		xisp->bayer_fmt = XISP_GRBG;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		fallthrough;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
		fallthrough;
	case MEDIA_BUS_FMT_SGBRG12_1X12:
		fallthrough;
	case MEDIA_BUS_FMT_SGBRG16_1X16:
		xisp->bayer_fmt = XISP_GBRG;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		fallthrough;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		fallthrough;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		fallthrough;
	case MEDIA_BUS_FMT_SBGGR16_1X16:
		xisp->bayer_fmt = XISP_BGGR;
		break;
	case MEDIA_BUS_FMT_Y8_1X8:
		fallthrough;
	case MEDIA_BUS_FMT_Y10_1X10:
		fallthrough;
	case MEDIA_BUS_FMT_Y12_1X12:
		xisp->bayer_fmt = XISP_RGGB;
		break;
	default:
		xisp->bayer_fmt = XISP_RGGB;
		dev_dbg(xisp->xvip.dev, "Unsupported format for Sink Pad");
		return false;
	}
	return true;
}

static int xisp_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct xisp_dev *xisp = to_xisp(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_mbus_framefmt *__propagate;
	u16 clamp_max_h, clamp_max_w;

	__format = __xisp_get_pad_format(xisp, sd_state, fmt->pad, fmt->which);
	if (!__format)
		return -EINVAL;

	if (xisp->config->flags & XILINX_ISP_VERSION_1) {
		/* Propagate to Source Pad */
		__propagate = __xisp_get_pad_format(xisp, sd_state,
						    XVIP_PAD_SOURCE, fmt->which);
		if (!__propagate)
			return -EINVAL;
	}
	*__format = fmt->format;

	if (xisp->config->flags & XILINX_ISP_VERSION_1) {
		clamp_max_h = xisp->max_height;
		clamp_max_w = xisp->max_width;
	} else if (xisp->config->flags & XILINX_ISP_VERSION_2) {
		clamp_max_h = XISP_MAX_HEIGHT;
		clamp_max_w = XISP_MAX_WIDTH;
	} else {
		return -EINVAL;
	}
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XISP_MIN_WIDTH, clamp_max_w);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XISP_MIN_HEIGHT, clamp_max_h);

	if (xisp->config->flags & XILINX_ISP_VERSION_1) {
		if (fmt->pad == XVIP_PAD_SOURCE) {
			if (__format->code != MEDIA_BUS_FMT_RBG888_1X24 &&
			    __format->code != MEDIA_BUS_FMT_RBG101010_1X30 &&
			    __format->code != MEDIA_BUS_FMT_RBG121212_1X36 &&
			    __format->code != MEDIA_BUS_FMT_RBG161616_1X48) {
				dev_dbg(xisp->xvip.dev,
					"%s : Unsupported source media bus code format",
					__func__);
				__format->code = MEDIA_BUS_FMT_RBG888_1X24;
			}
		}
		if (fmt->pad == XVIP_PAD_SINK) {
			if (!xisp_get_bayer_format(xisp, __format->code)) {
				dev_dbg(xisp->xvip.dev,
					"Unsupported Sink Pad Media format, defaulting to RGGB");
				__format->code = MEDIA_BUS_FMT_SRGGB10_1X10;
			}
		}

		/* Always propagate Sink image size to Source */
		__propagate->width  = __format->width;
		__propagate->height = __format->height;
	} else if (xisp->config->flags & XILINX_ISP_VERSION_2) {
		if (fmt->pad == XVIP_PAD_SOURCE) {
			if (__format->code != MEDIA_BUS_FMT_Y8_1X8 &&
			    __format->code != MEDIA_BUS_FMT_Y10_1X10 &&
			    __format->code != MEDIA_BUS_FMT_Y12_1X12 &&
			    __format->code != MEDIA_BUS_FMT_RBG888_1X24 &&
			    __format->code != MEDIA_BUS_FMT_RGB101010_1X30 &&
			    __format->code != MEDIA_BUS_FMT_RGB121212_1X36 &&
			    __format->code != MEDIA_BUS_FMT_RGB161616_1X48 &&
			    __format->code != MEDIA_BUS_FMT_BGR888_1X24 &&
			    __format->code != MEDIA_BUS_FMT_GBR888_1X24 &&
			    __format->code != MEDIA_BUS_FMT_RGB888_1X24 &&
			    __format->code != MEDIA_BUS_FMT_RGB101010_1X30) {
				dev_dbg(xisp->xvip.dev,
					"%s : Unsupported source media bus code format",
					__func__);
				__format->code = MEDIA_BUS_FMT_RBG888_1X24;
			}
		}
		if (fmt->pad == XVIP_PAD_SINK) {
			if (!xisp_get_bayer_format(xisp, __format->code)) {
				dev_dbg(xisp->xvip.dev,
					"Unsupported Sink Pad Media format, defaulting to RGGB");
				__format->code = MEDIA_BUS_FMT_SRGGB10_1X10;
			}
		}
	} else {
		return -EINVAL;
	}
	fmt->format = *__format;
	return 0;
}

static const struct v4l2_subdev_pad_ops xisp_pad_ops = {
	.get_fmt = xisp_get_format,
	.set_fmt = xisp_set_format,
};

static const struct v4l2_subdev_ops xisp_ops = {
	.video = &xisp_video_ops,
	.pad = &xisp_pad_ops,
};

static const struct media_entity_operations xisp_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xisp_parse_of(struct xisp_dev *xisp)
{
	struct device *dev = xisp->xvip.dev;
	struct device_node *node = dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	int rval;

	if (xisp->config->flags & XILINX_ISP_VERSION_1) {
		rval = of_property_read_u16(node, "xlnx,max-height",
					    &xisp->max_height);
		if (rval < 0) {
			dev_err(dev, "missing xlnx,max-height property!");
			return -EINVAL;
		}

		if (xisp->max_height > XISP_MAX_HEIGHT ||
		    xisp->max_height < XISP_MIN_HEIGHT) {
			dev_err(dev, "Invalid height in dt");
			return -EINVAL;
		}

		rval = of_property_read_u16(node, "xlnx,max-width",
					    &xisp->max_width);
		if (rval < 0) {
			dev_err(dev, "missing xlnx,max-width property!");
			return -EINVAL;
		}

		if (xisp->max_width > XISP_MAX_WIDTH ||
		    xisp->max_width < XISP_MIN_WIDTH) {
			dev_err(dev, "Invalid width in dt");
			return -EINVAL;
		}

		rval = of_property_read_u16(node, "xlnx,rgain",
					    &xisp->rgain);
		if (rval < 0) {
			dev_err(dev, "missing xlnx,rgain!");
			return -EINVAL;
		}

		rval = of_property_read_u16(node, "xlnx,bgain",
					    &xisp->bgain);
		if (rval < 0) {
			dev_err(dev, "missing xlnx,bgain!");
			return -EINVAL;
		}

		rval = of_property_read_u16(node, "xlnx,pawb",
					    &xisp->pawb);
		if (rval < 0) {
			dev_err(dev, "missing xlnx,pawb!");
			return -EINVAL;
		}

		rval = of_property_read_bool(node, "xlnx,mode-reg");
		if (rval)
			xisp->mode_reg = of_property_read_bool(node, "xlnx,mode-reg");
	}

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;

	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		struct device_node *endpoint;

		if (!port->name || of_node_cmp(port->name, "port"))
			continue;

		endpoint = of_get_next_child(port, NULL);
		if (!endpoint) {
			dev_err(dev, "No port at\n");
			return -EINVAL;
		}

		/* Count the number of ports. */
		xisp->npads++;
	}

	/* validate number of ports */
	if (xisp->npads > XISP_NO_OF_PADS) {
		dev_err(dev, "invalid number of ports %u\n", xisp->npads);
		return -EINVAL;
	}

	xisp->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xisp->rst_gpio)) {
		if (PTR_ERR(xisp->rst_gpio) != -EPROBE_DEFER)
			dev_err(dev, "Reset GPIO not setup in DT");
		return PTR_ERR(xisp->rst_gpio);
	}

	return 0;
}

/**
 * xisp_create_controls - Creates V4L2 controls for the given xisp device
 * @xisp: Pointer to the xisp_dev structure representing the device
 * @index: Index of the module enable bit in xisp->module_en and xisp->module_bypass_en
 * @ctrl_arr: Array of v4l2_ctrl_config structures representing the control configurations
 * @size: Number of control configurations in the ctrl_arr array
 *
 * This function creates custom V4L2 controls for the specified xisp device.
 * It checks whether the module is enabled and whether the module bypass is
 * enabled before creating controls.
 *
 * The controls are created based on the configurations provided in the ctrl_arr array.
 * If the module enable bit is set and the module bypass enable bit is set,
 * it creates the first control from the array. If there are more controls
 * (size > 1), it iterates through the remaining controls and creates them.
 */
static void xisp_create_controls(struct xisp_dev *xisp, u32 index,
				 struct v4l2_ctrl_config *ctrl_arr, u32 size)
{
	int itr;
	bool en_index = XGET_BIT(index, xisp->module_en);
	bool bypass_en_index = XGET_BIT(index, xisp->module_bypass_en);

	if (!en_index)
		return;

	if (bypass_en_index)
		v4l2_ctrl_new_custom(&xisp->ctrl_handler, &ctrl_arr[0], NULL);

	if (size < 2)
		return;

	for (itr = 1; itr < size; itr++)
		v4l2_ctrl_new_custom(&xisp->ctrl_handler, &ctrl_arr[itr], NULL);
}

static int xisp_probe(struct platform_device *pdev)
{
	struct xisp_dev *xisp;
	struct v4l2_subdev *subdev;
	int rval, itr;
	u8 num_of_parameters;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;

	xisp = devm_kzalloc(&pdev->dev, sizeof(*xisp), GFP_KERNEL);
	if (!xisp)
		return -ENOMEM;

	xisp->xvip.dev = &pdev->dev;

	match = of_match_node(xisp_of_id_table, node);
	if (!match)
		return -ENODEV;

	xisp->config = match->data;

	rval = xisp_parse_of(xisp);
	if (rval < 0)
		return rval;

	rval = xvip_init_resources(&xisp->xvip);
	if (rval)
		return -EIO;

	/* Reset ISP pipeline IP */
	xisp_reset(xisp);

	/* Init V4L2 subdev */
	subdev = &xisp->xvip.subdev;
	v4l2_subdev_init(subdev, &xisp_ops);
	subdev->dev = &pdev->dev;
	strscpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	xisp->gamma_table = xgamma_curves;

	if (xisp->config->flags & XILINX_ISP_VERSION_2) {
		xisp->module_conf = xvip_read(&xisp->xvip, XISP_PIPELINE_CONFIG_INFO_REG);
		xisp->ip_max_res = xvip_read(&xisp->xvip, XISP_MAX_SUPPORTED_SIZE_REG);
		xisp->module_en = xvip_read(&xisp->xvip, XISP_FUNCS_AVAILABLE_REG);
		xisp->module_bypass_en = xvip_read(&xisp->xvip, XISP_FUNCS_BYPASSABLE_REG);
	}

	/*
	 * Sink Pad can be any Bayer format.
	 * Default Sink Pad format is RGGB.
	 */
	xisp->formats[XVIP_PAD_SINK].field = V4L2_FIELD_NONE;
	xisp->formats[XVIP_PAD_SINK].colorspace = V4L2_COLORSPACE_SRGB;
	xisp->formats[XVIP_PAD_SINK].width = XISP_MIN_WIDTH;
	xisp->formats[XVIP_PAD_SINK].height = XISP_MIN_HEIGHT;
	xisp->formats[XVIP_PAD_SINK].code = MEDIA_BUS_FMT_SRGGB10_1X10;

	/* Source Pad has a fixed media bus format of RGB */
	xisp->formats[XVIP_PAD_SOURCE].field = V4L2_FIELD_NONE;
	xisp->formats[XVIP_PAD_SOURCE].colorspace = V4L2_COLORSPACE_SRGB;
	xisp->formats[XVIP_PAD_SOURCE].width = XISP_MIN_WIDTH;
	xisp->formats[XVIP_PAD_SOURCE].height = XISP_MIN_HEIGHT;
	xisp->formats[XVIP_PAD_SOURCE].code = MEDIA_BUS_FMT_RBG888_1X24;

	xisp->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xisp->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity */
	subdev->entity.ops = &xisp_media_ops;
	rval = media_entity_pads_init(&subdev->entity, XISP_NO_OF_PADS, xisp->pads);
	if (rval < 0)
		goto media_error;

	/* V4L2 Controls */
	if (xisp->config->flags & XILINX_ISP_VERSION_2) {
		num_of_parameters = ARRAY_SIZE(xisp_ctrls_aec) + ARRAY_SIZE(xisp_ctrls_blc);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_awb) + ARRAY_SIZE(xisp_ctrls_bpc);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_degamma);

		v4l2_ctrl_handler_init(&xisp->ctrl_handler, num_of_parameters);

		xisp_create_controls(xisp, XISP_AEC_INDEX, xisp_ctrls_aec,
				     ARRAY_SIZE(xisp_ctrls_aec));
		xisp_create_controls(xisp, XISP_BLC_INDEX, xisp_ctrls_blc,
				     ARRAY_SIZE(xisp_ctrls_blc));
		xisp_create_controls(xisp, XISP_AWB_INDEX, xisp_ctrls_awb,
				     ARRAY_SIZE(xisp_ctrls_awb));
		xisp_create_controls(xisp, XISP_BPC_INDEX, xisp_ctrls_bpc,
				     ARRAY_SIZE(xisp_ctrls_bpc));
		xisp_create_controls(xisp, XISP_DEGAMMA_INDEX,
				     xisp_ctrls_degamma, ARRAY_SIZE(xisp_ctrls_degamma));
	} else {
		v4l2_ctrl_handler_init(&xisp->ctrl_handler, ARRAY_SIZE(xisp_ctrls));
		for (itr = 0; itr < ARRAY_SIZE(xisp_ctrls); itr++) {
			v4l2_ctrl_new_custom(&xisp->ctrl_handler,
					     &xisp_ctrls[itr], NULL);
		}
	}

	if (xisp->ctrl_handler.error) {
		dev_err(&pdev->dev, "Failed to add V4L2 controls");
		rval = xisp->ctrl_handler.error;
		goto ctrl_error;
	}

	subdev->ctrl_handler = &xisp->ctrl_handler;
	rval = v4l2_ctrl_handler_setup(&xisp->ctrl_handler);
	if (rval < 0) {
		dev_err(&pdev->dev, "Failed to setup control handler");
		goto  ctrl_error;
	}

	platform_set_drvdata(pdev, xisp);
	rval = v4l2_async_register_subdev(subdev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register subdev");
		goto ctrl_error;
	}

	dev_dbg(&pdev->dev, "Xilinx Video ISP Pipeline Probe Successful");
	return 0;

ctrl_error:
	v4l2_ctrl_handler_free(&xisp->ctrl_handler);
media_error:
	xvip_cleanup_resources(&xisp->xvip);

	return rval;
}

static int xisp_remove(struct platform_device *pdev)
{
	struct xisp_dev *xisp = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xisp->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xisp->xvip);

	return 0;
}

MODULE_DEVICE_TABLE(of, xisp_of_id_table);

static struct platform_driver xisp_driver = {
	.driver = {
		.name = "xilinx-isppipeline",
		.of_match_table = xisp_of_id_table,
	},
	.probe = xisp_probe,
	.remove = xisp_remove,

};

module_platform_driver(xisp_driver);
MODULE_DESCRIPTION("Xilinx Video ISP Pipeline IP Driver");
MODULE_LICENSE("GPL");

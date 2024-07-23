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
 *
 * XISP_RGBIR_CONFIG_BASE
 * 0-63: Base address in the device's register space where RGBIR entries will be written.
 *
 * XISP_CCM_CONFIG_1_BASE
 * 0-63: Base address in the device's register space for CCM_Matrix
 *
 * XISP_CCM_CONFIG_2_BASE
 * 0-63: Base address in the device's register space for offsetarray
 *
 * XISP_GAIN_CONTROL_CONFIG_1_REG
 * 0-15: rgain, 16-31: bgain
 *
 * XISP_GAIN_CONTROL_CONFIG_2_REG
 * 0-15: ggain, 16-23: bayer_fmt, 24-31: Reserved
 *
 * XISP_GAMMA_CONFIG_BASE
 * 0-63: Base address in the device's register space for gamma params
 *
 * XISP_GTM_CONFIG_1_REG / XISP_GTM_CONFIG_2_REG
 * 0-31: C1 / C2
 *
 * XISP_LTM_CONFIG_REG
 * 0-15: block_rows, 16-31: block_coloms
 *
 * XISP_LUT3D_CONFIG_REG
 * 0-31: lut3d_dim
 *
 * XISP_CLAHE_CONFIG_1_REG
 * 0-31: clip
 *
 * XISP_CLAHE_CONFIG_2_REG
 * 0-15: tiles_x, 16-31: tiles_y
 *
 * XISP_RESIZE_CONFIG_REG
 * 0-15: resize_new_width, 16-31: resize_new_height
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
#define XISP_GTM_CONFIG_1_REG			(0x48UL)
#define XISP_GTM_CONFIG_2_REG			(0x50UL)
#define XISP_LTM_CONFIG_REG				(0x58UL)
#define XISP_RGBIR_CONFIG_BASE			(0x200UL)
#define XISP_DEGAMMA_CONFIG_BASE		(0x1000UL)
#define XISP_GAMMA_CONFIG_BASE			(0x2000UL)
#define XISP_LUT3D_CONFIG_REG			(0x60UL)
#define XISP_LUT3D_CONFIG_BASE			(0x80000)
#define XISP_RGBIR_CONFIG_BASE1			(XISP_RGBIR_CONFIG_BASE + 0x20)
#define XISP_RGBIR_CONFIG_BASE2			(XISP_RGBIR_CONFIG_BASE + 0x40)
#define XISP_RGBIR_CONFIG_BASE3			(XISP_RGBIR_CONFIG_BASE + 0x60)
#define XISP_RGBIR_CONFIG_BASE4			(XISP_RGBIR_CONFIG_BASE + 0x70)
#define XISP_RGBIR_CONFIG_BASE5			(XISP_RGBIR_CONFIG_BASE + 0x80)
#define XISP_RGBIR_CONFIG_BASE_SIZE		(25)
#define XISP_RGBIR_CONFIG_BASE_1_LSB		(25)
#define XISP_RGBIR_CONFIG_BASE_1_SIZE		(25)
#define XISP_RGBIR_CONFIG_BASE_2_LSB		(50)
#define XISP_RGBIR_CONFIG_BASE_2_SIZE		(25)
#define XISP_RGBIR_CONFIG_BASE_3_LSB		(75)
#define XISP_RGBIR_CONFIG_BASE_3_SIZE		(9)
#define XISP_RGBIR_CONFIG_BASE_4_LSB		(84)
#define XISP_RGBIR_CONFIG_BASE_4_SIZE		(9)
#define XISP_RGBIR_CONFIG_BASE_5_LSB		(93)
#define XISP_RGBIR_CONFIG_BASE_5_SIZE		(4)
#define XISP_CCM_CONFIG_1_BASE			(0x4000UL)
#define XISP_CCM_CONFIG_2_BASE			(0x4100UL)
#define XISP_GAIN_CONTROL_CONFIG_1_REG		(0x38UL)
#define XISP_GAIN_CONTROL_CONFIG_2_REG		(0x40UL)
#define XISP_HDR_DECOM_CONFIG_BASE		(0x100UL)
#define XISP_HDR_MERGE_CONFIG_BASE		(0x8000UL)
#define XISP_CLAHE_CONFIG_1_REG			(0x68UL)
#define XISP_CLAHE_CONFIG_2_REG			(0x70UL)
#define XISP_RESIZE_CONFIG_REG			(0x78UL)
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
#define XISP_HDR_OFFSET				(0x800)
#define XISP_WR_CV				(16384)
#define XISP_W_B_SIZE				(1024)
#define XISP_GAMMA_LUT_LEN		(64)
#define XISP_MIN_VALUE                       (0)
#define XISP_MAX_VALUE                       (65535)
#define XISP_LUT3D_SIZE				(107811UL)
#define XISP_NO_EXPS				(2)
#define XISP_NO_OF_PADS			(2)
#define XISP_1H					(100)
#define XISP_1K					(1000)
#define XISP_1M					(1000000UL)
#define XISP_1B					(1000000000UL)
#define XISP_10B				(10000000000UL)
#define XISP_1H_1B				(100000000000UL)
#define XISP_1T					(1000000000000UL)
#define XISP_CONST1				(4032000000UL)
#define XISP_CONST2				(3628800000000UL)
#define XISP_CONST3				(36288000000000UL)
#define XISP_CONST4				(399168000000UL)
#define XISP_CONST5				(62270208000000UL)

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
#define XISP_RED_GAIN_DEFALUT			(100)
#define XISP_BLUE_GAIN_DEFALUT			(350)
#define XISP_GREEN_GAIN_DEFALUT			(200)
#define XISP_RED_GAMMA_MAX			(40)
#define XISP_RED_GAMMA_DEFALUT			(20)
#define XISP_GREEN_GAMMA_MAX			(40)
#define XISP_GREEN_GAMMA_DEFALUT		(15)
#define XISP_BLUE_GAMMA_MAX			(40)
#define XISP_BLUE_GAMMA_DEFALUT			(20)
#define XISP_HDR_ALPHA_MAX			(256)
#define XISP_HDR_ALPHA_DEFALUT			(5)
#define XISP_HDR_OPTICAL_BLACK_VALUE_MAX	(256)
#define XISP_HDR_OPTICAL_BLACK_VALUE_DEFALUT	(0)
#define XISP_HDR_INTERSEC_MAX			(4000000UL)
#define XISP_HDR_INTERSEC_DEFALUT		(1386290UL)
#define XISP_HDR_RHO_MAX			(65536)
#define XISP_HDR_RHO_DEFALUT			(512)
#define XISP_SELECT_DECOMPAND_MAX		(2)
#define XISP_SELECT_DECOMPAND_DEFALUT		(0)
#define XISP_GTM_C1_DEFALUT			(128)
#define XISP_GTM_C2_DEFALUT			(128)
#define XISP_CLAHE_CLIP_DEFALUT			(3)
#define XISP_CLAHE_TILES_Y_DEFALUT		(4)
#define XISP_CLAHE_TILES_X_DEFALUT		(4)
#define XISP_LTM_BLOCK_HEIGHT_DEFALUT		(8)
#define XISP_LTM_BLOCK_WIDTH_DEFALUT		(8)
#define XISP_3DLUT_DIM_MAX			(0x80000000UL)
#define XISP_3DLUT_DIM_DEFALUT			(107811UL)
#define XISP_MONO_LUMA_GAIN_DEFALUT		(128)
#define XISP_MONO_GAMMA_MAX			(40)
#define XISP_MONO_GAMMA_DEFALUT			(20)
#define XISP_UPPER_WORD_MSB			(31)
#define XISP_UPPER_WORD_LSB			(16)
#define XISP_MAX_INTENSITY			(10 * (XISP_W_B_SIZE - 1))

static const u8 exposure_time[XISP_NO_EXPS] = {100, 25};

enum xisp_bayer_format {
	XISP_RGGB = 3,
	XISP_GRBG = 2,
	XISP_GBRG = 1,
	XISP_BGGR = 0,
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
	XISP_HDR_MODE_INDEX = 0,
	XISP_ISP_EN_INDEX = 0,
	XISP_HDR_INDEX = 1,
	XISP_RGBIR_INDEX = 2,
	XISP_AEC_INDEX = 3,
	XISP_BLC_INDEX = 4,
	XISP_BPC_INDEX = 5,
	XISP_DEGAMMA_INDEX = 6,
	XISP_LSC_INDEX = 7,
	XISP_GAIN_INDEX = 8,
	XISP_DEMOSAIC_INDEX = 9,
	XISP_AWB_INDEX = 10,
	XISP_CCM_INDEX = 11,
	XISP_TM_INDEX = 12,
	XISP_TM_TYPE_LSB_INDEX = 13,
	XISP_TM_TYPE_MSB_INDEX = 14,
	XISP_GAMMA_INDEX = 15,
	XISP_LUT3D_INDEX = 16,
	XISP_CSC_INDEX = 17,
	XISP_BAYER_STATS_INDEX = 18,
	XISP_LUMA_STATS_INDEX = 19,
	XISP_RGB_STATS_INDEX = 20,
	XISP_CLAHE_INDEX = 21,
	XISP_MEDIAN_INDEX = 22,
	XISP_RESIZE_INDEX = 23
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
 * @tm_type: Flag to indicate the type of tone mapping enabled
 * @mult_factor: Expected multiplication factor
 * @black_level: Expected black level
 * @rgain: Expected red gain
 * @bgain: Expected blue gain
 * @ggain: Expected green gain
 * @luma_gain: Expected luminance gain
 * @mode_reg: Track if AWB is enabled or not
 * @gtm_c1: Expected gtm c1 value
 * @gtm_c2: Expected gtm c2 value
 * @lut3d_dim: Expected 3d lut dimension
 * @pawb: Expected threshold
 * @block_rows: Expected number of block rows
 * @block_cols: Expected number of block cols
 * @clip: Expected clip value
 * @tiles_y: Expected number of tiles along y-axis
 * @tiles_x: Expected number of tiles along x-axis
 * @intersec: Expected intersec value
 * @weights1: Expected weights1 values
 * @weights2: Expected weights2 values
 * @rho: Expected rho value
 * @alpha: Expected alpha value
 * @optical_black_value: Expected optical black value
 * @resize_new_height: Expected resize new height
 * @resize_new_width: Expected resize new width
 * @ccm_select: Expected ccm array values
 * @decompand_select: Expected decompand array values
 * @lut3d: Expected 3d lut values
 * @red_lut: Pointer to the gamma coefficient as per the Red Gamma control
 * @green_lut: Pointer to the gamma coefficient as per the Green Gamma control
 * @blue_lut: Pointer to the gamma coefficient as per the Blue Gamma control
 * @gamma_table: Pointer to the table containing various gamma values
 * @threshold_aec: Expected threshold for auto exposure correction
 * @threshold_awb: Expected threshold for auto white balance
 * @degamma_select: Expected degamma array values
 * @degamma_lut: Pointer to the degamma coefficient as per the degamma control
 * @decompand_lut: Pointer to the decompand coefficient as per the decompand control
 * @ccm_matrix_lut: Pointer to the degamma coefficient as per the degamma control
 * @ccm_offsetarray_lut: Pointer to the degamma coefficient as per the degamma control
 * @mono_lut: Pointer to the gamma coefficient as per the mono Gamma control
 */
struct xisp_dev {
	struct xvip_device xvip;
	struct media_pad pads[XISP_NO_OF_PADS];
	struct v4l2_mbus_framefmt formats[XISP_NO_OF_PADS];
	struct v4l2_ctrl_handler ctrl_handler;
	const struct xilinx_isp_feature *config;
	const u32 *lut3d;
	const u32 *mono_lut;
	const u32 *red_lut;
	const u32 *green_lut;
	const u32 *blue_lut;
	const u32 **gamma_table;
	const u32 (*degamma_lut)[XISP_DEGAMMA_KNEE_POINTS][XISP_DEGAMMA_PARAMS];
	const u32 (*decompand_lut)[XISP_HDR_DECOMP_KNEE_POINTS][XISP_HDR_DECOMP_PARAMS];
	const signed int (*ccm_matrix_lut)[XISP_CCM_MATRIX_DIM2];
	const signed int *ccm_offsetarray_lut;
	enum xisp_bayer_format bayer_fmt;
	struct gpio_desc *rst_gpio;
	u32 ip_max_res;
	u32 module_conf;
	u32 module_bypass;
	u32 module_bypass_en;
	u32 module_en;
	u32 mult_factor;
	u32 black_level;
	u32 gtm_c1;
	u32 gtm_c2;
	u32 lut3d_dim;
	u32 intersec;
	u16 npads;
	u16 width;
	u16 height;
	u16 max_width;
	u16 max_height;
	u16 rgain;
	u16 bgain;
	u16 ggain;
	u16 luma_gain;
	u16 block_rows;
	u16 block_cols;
	u16 clip;
	u16 tiles_y;
	u16 tiles_x;
	u16 weights1[XISP_W_B_SIZE];
	u16 weights2[XISP_W_B_SIZE];
	u16 rho;
	u16 pawb;
	u16 resize_new_height;
	u16 resize_new_width;
	u16 threshold_aec;
	u16 threshold_awb;
	u8 tm_type;
	u8 alpha;
	u8 optical_black_value;
	u8 degamma_select;
	u8 decompand_select;
	u8 ccm_select;
	bool mode_reg;
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

/**
 * xisp_compute_data_reliability_weight - Computes data reliability weight
 * @c_intersec: Intersection coefficient used in weight calculation
 * @mu_h: Upper threshold value for 'r'
 * @mu_l: Lower threshold value for 'r'
 * @r: Current value to be evaluated
 *
 * This function computes the data reliability weight based on the input parameters.
 * The weight is calculated differently based on whether 'r' is below 'mu_l',
 * between 'mu_l' and 'mu_h', or above 'mu_h'.
 *
 * When 'r' is below 'mu_l', the weight is computed using a specific formula.
 * When 'r' is between 'mu_l' and 'mu_h', the weight is set to a constant value.
 * When 'r' is above 'mu_h', the weight is computed using a Taylor series expansion
 * for better precision with a series of coefficients.
 *
 * Returns the computed weight as a 16-bit unsigned integer.
 */
static u16 xisp_compute_data_reliability_weight(u32 c_intersec, u32 mu_h, u32 mu_l, u32 r)
{
	u16 wr, wr1;

	if (r < mu_l) {
		u32 x = int_pow((mu_l - r), 2);
		u64 xpow = (c_intersec * x) / XISP_1M;

		wr = XISP_WR_CV / int_pow(3, xpow);
	} else if (r < mu_h) {
		wr = XISP_WR_CV;
	} else {
		u32 x = int_pow((r - mu_h), 2);
		u64 xpow = c_intersec * x;
		u64 taylor_coeff_1 = xpow / XISP_1K;
		u64 int_coeff_21 = (int_pow(xpow, 2) / (XISP_1H * XISP_1K));
		u64 taylor_coeff_2 = int_coeff_21 / (20 * XISP_1K);
		u64 int_coeff_31 = xpow / (6 * XISP_1K);
		u64 taylor_coeff_3 = (int_coeff_21 * int_coeff_31) / (10 * XISP_1M);
		u64 taylor_coeff_4 = (int_coeff_21 * int_coeff_21) / (24 * XISP_1H_1B);
		u64 int_coeff_51 = (int_coeff_21 * xpow) / (120 * XISP_1M);
		u64 taylor_coeff_5 = (int_coeff_21 * int_coeff_51) / XISP_1H_1B;
		u64 int_coeff_61 = (int_coeff_21 * int_coeff_21) / (72 * XISP_1M);
		u64 taylor_coeff_6 = (int_coeff_61 * int_coeff_21) / (10 * XISP_1T);
		u64 int_coeff_71 = (int_coeff_21 * xpow) / 5040;
		u64 int_coeff_72 = (int_coeff_21 * int_coeff_21) / XISP_10B;
		u64 taylor_coeff_7 = (int_coeff_71 * int_coeff_72) / (XISP_1H * XISP_1T);
		u64 int_coeff_81 = (int_coeff_21 * int_coeff_21) / XISP_10B;
		u64 int_coeff_82 = (int_coeff_21 * int_coeff_21) / XISP_10B;
		u64 taylor_coeff_8 = (int_coeff_81 * int_coeff_82) / XISP_CONST1;
		u64 int_coeff_91 = (int_coeff_21 * int_coeff_21) / (XISP_1H * XISP_1M);
		u64 int_coeff_92 = (int_coeff_21 * xpow) / (XISP_1H * XISP_1K);
		u64 int_coeff_93 = (int_coeff_91 * int_coeff_92) / XISP_CONST2;
		u64 taylor_coeff_9 = (int_coeff_93 * int_coeff_21) / XISP_1H_1B;
		u64 int_coeff_101 = (int_coeff_21 * int_coeff_21) / XISP_10B;
		u64 int_coeff_102 = (int_coeff_21 * int_coeff_21) / XISP_10B;
		u64 int_coeff_103 = int_coeff_21 / (XISP_1H * XISP_1K);
		u64 taylor_coeff_10 = (int_coeff_101 * int_coeff_102 * int_coeff_103) /
				       XISP_CONST3;
		u64 int_coeff_111 = (int_coeff_21 * int_coeff_21) / XISP_1H_1B;
		u64 int_coeff_112 = (xpow * int_coeff_21) / XISP_1T;
		u64 int_coeff_113 = (int_coeff_111 * int_coeff_112 * int_coeff_21) /
				    XISP_CONST4;
		u64 taylor_coeff_11 = (int_coeff_113 * int_coeff_21) / XISP_1H_1B;
		u64 int_coeff_121 = (int_coeff_111 * int_coeff_21) / XISP_10B;
		u64 int_coeff_122 = (int_coeff_121 * int_coeff_21) / 479001600;
		u64 int_coeff_123 = (int_coeff_122 * int_coeff_21) / XISP_1M;
		u64 taylor_coeff_12 = (int_coeff_123 * int_coeff_21) / XISP_1T;
		u64 int_coeff_131 = (int_coeff_21 * int_coeff_21) / XISP_1H_1B;
		u64 int_coeff_132 = (int_coeff_131 * int_coeff_112 * int_coeff_21) /
				    XISP_1T;
		u64 int_coeff_133 = (int_coeff_132 * int_coeff_21) / XISP_1M;
		u64 taylor_coeff_13 = (int_coeff_133 * int_coeff_21) / XISP_CONST5;
		u64 int_coeff_141 = (int_coeff_131 * int_coeff_21) / (XISP_1H * XISP_1K);
		u64 int_coeff_142 = (int_coeff_131 * int_coeff_141) / (XISP_1H * XISP_1K);
		u64 taylor_coeff_14 = (int_coeff_131 * int_coeff_142) / (87178291200 * XISP_1K);

		wr1 = XISP_1K - taylor_coeff_1 + taylor_coeff_2 - taylor_coeff_3 + taylor_coeff_4
			- taylor_coeff_5 + taylor_coeff_6 - taylor_coeff_7 + taylor_coeff_8 -
			taylor_coeff_9 + taylor_coeff_10 - taylor_coeff_11 + taylor_coeff_12 -
			taylor_coeff_13 + taylor_coeff_14;

		wr = (wr1 * XISP_WR_CV) / XISP_1K;
	}

	return wr;
}

/**
 * xisp_hdr_merge - Computes HDR merge weights for two exposures
 * @alpha: Scaling factor for the intensity values
 * @optical_black_value: Black level correction value
 * @intersec: Intersection parameter used for reliability weight calculation
 * @rho: Parameter influencing the intensity weighting
 * @weights1: Output array for storing computed weights for the first exposure
 * @weights2: Output array for storing computed weights for the second exposure
 *
 * This function computes the weights for merging High Dynamic Range (HDR) images
 * from two exposures. It calculates the upper and lower intensity thresholds (mu_h and mu_l)
 * and uses them to compute reliability weights for each exposure.
 */
static void xisp_hdr_merge(u32 alpha, u32 optical_black_value, u32 intersec,
			   u32 rho, u16 weights1[XISP_W_B_SIZE], u16 weights2[XISP_W_B_SIZE])
{
	u32 mu_h[XISP_NO_EXPS] = {0, 0};
	u32 mu_l[XISP_NO_EXPS] = {0, 0};
	u32 gamma_out[XISP_NO_EXPS] = {0, 0};
	u32 value_max = (XISP_MAX_INTENSITY - optical_black_value) / alpha;
	u32 c_inters;
	int m = XISP_NO_EXPS;
	int i, j;

	for (i = 0; i < m - 1; i++) {
		gamma_out[i] = (10 * (rho * (XISP_MAX_INTENSITY - optical_black_value)
				- optical_black_value * (XISP_MAX_INTENSITY / 10 - rho))) /
				(exposure_time[i] * rho + exposure_time[i + 1] *
				(XISP_MAX_INTENSITY / 10 - rho));
		if (!i) {
			u32 value = (10 * rho - optical_black_value) / alpha;

			mu_h[i] = (100 * value) / exposure_time[0];
		} else {
			mu_h[i] = gamma_out[i] - (gamma_out[i - 1] - mu_h[i - 1]);
		}
		mu_l[i + 1] = 2 * gamma_out[i] - mu_h[i];
	}

	mu_h[m - 1] = (100 * value_max) / exposure_time[m - 1];
	c_inters = (intersec / (int_pow((gamma_out[0] - mu_h[0]), 2)));

	for (i = 0; i < XISP_NO_EXPS; i++) {
		for (j = 0; j < (XISP_W_B_SIZE); j++) {
			u32 rv = (u32)((100 * j) / exposure_time[i]);

			if (!i) {
				weights1[j] = xisp_compute_data_reliability_weight(c_inters,
										   mu_h[i],
										   mu_l[i],
										   rv);
			} else  {
				weights2[j] = xisp_compute_data_reliability_weight(c_inters,
										   mu_h[i],
										   mu_l[i],
										   rv);
			}
		}
	}
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

/**
 * xisp_select_gamma - Selects the gamma curve coefficients based on the given value
 * @value: Index to select the gamma curve
 * @coeff: Pointer to the location where the selected gamma curve coefficients will be stored
 * @xgamma_curves: Array of pointers to gamma curve coefficient arrays
 *
 * This function selects the gamma curve coefficients from an array of gamma curves
 * based on the provided index value. It adjusts the index by subtracting 1 (assuming
 * 1-based index) and updates the pointer to the coefficients.
 */
static void xisp_select_gamma(u32 value, const u32 **coeff, const u32 **xgamma_curves)
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
 * xisp_hdrmg_set_lut_entries - Sets HDR merge LUT entries for the xisp device
 * @xisp: Pointer to the xisp device structure
 * @lut_base: Base address for the LUT in the device
 * @lut: Array containing LUT entries for the first set
 * @lut1: Array containing LUT entries for the second set
 *
 * This function writes HDR merge Look-Up Table (LUT) entries to the xisp device's
 * memory starting from specified base addresses. It uses interleaved LUT entries
 * from two different sets (lut and lut1) based on the offset.
 */
static void xisp_hdrmg_set_lut_entries(struct xisp_dev *xisp,
				       const u32 lut_base,
				       const u16 lut[XISP_W_B_SIZE],
				       const u16 lut1[XISP_W_B_SIZE])
{
	u32 lut_data;
	int i, j, ival;
	u32 lut_offset[8];

	for (j = 0; j < ARRAY_SIZE(lut_offset); j++)
		lut_offset[j] = lut_base + (j * XISP_HDR_OFFSET);

	for (i = 0; i < ARRAY_SIZE(lut_offset); i++) {
		for (ival = 0; ival < XISP_W_B_SIZE; ival += 2) {
			if ((i & 1) == 0)
				lut_data = (lut[ival + 1] << 16) | lut[ival];
			else
				lut_data = (lut1[ival + 1] << 16) | lut1[ival];

			xvip_write(&xisp->xvip, lut_offset[i], lut_data);
			lut_offset[i] += 4;
		}
	}
}

/**
 * xisp_set_lut3d_entries - Sets 3D LUT entries for the xisp device
 * @xisp: Pointer to the xisp device structure
 * @lut3d_offset: Base address offset for the 3D LUT in the device
 * @lut3d: Pointer to the array containing 3D LUT entries
 *
 * This function writes 3D Look-Up Table (LUT) entries to the xisp device's memory.
 * It iterates over the 3D LUT entries and writes each entry to the device memory
 * starting from the specified base offset.
 */
static void xisp_set_lut3d_entries(struct xisp_dev *xisp, const u32 lut3d_offset,
				   const u32 *lut3d)
{
	int i;

	for (i = 0; i < XISP_LUT3D_SIZE; i++)
		xvip_write(&xisp->xvip, lut3d_offset, lut3d[i]);
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

/**
 * xisp_set_ccm_matrix_entries - Set CCM matrix and offset entries for the XISP device.
 * @xisp: Pointer to the xisp_dev structure representing the device.
 * @ccm_config_1_base: Base address in the device's register space for the CCM matrix entries.
 * @ccm_config_2_base: Base address in the device's register space for the offset entries.
 * @ccm_matrix: Pointer to a 2-dimensional array containing the CCM matrix values to be written.
 *              The dimensions of this array are defined by XISP_CCM_MATRIX_DIM1 and
 *		XISP_CCM_MATRIX_DIM2.
 * @offsetarray: Pointer to an array containing the offset values to be written.
 *
 * This function writes the CCM matrix values from the provided 2D array to the device's registers
 * starting at ccm_config_1_base. After writing the values for each row of the matrix, it writes
 * a corresponding offset value from the offsetarray to the register space starting at
 * ccm_config_2_base.
 *
 * The loops iterate through:
 * - i: Row index of the CCM matrix, ranging from 0 to XISP_CCM_MATRIX_DIM1 - 1
 * - j: Column index of the CCM matrix, ranging from 0 to XISP_CCM_MATRIX_DIM2 - 1
 *
 * Inside the inner loop, the function calls xvip_write() to write each CCM matrix value to the
 * device's register at the current offset for ccm_config_1. After the inner loop, it writes an
 * offset value from offsetarray to the device's register at the current offset for ccm_config_2.
 * The offsets for both configuration bases are incremented by 4 bytes (32 bits) after each write.
 */
static void xisp_set_ccm_matrix_entries(struct xisp_dev *xisp, const u32 ccm_config_1_base,
					const u32 ccm_config_2_base,
					const signed int (*ccm_matrix)[XISP_CCM_MATRIX_DIM2],
					const signed int *offsetarray)
{
	int i, j;
	u32 ccm_config_1_offset, ccm_config_2_offset;

	ccm_config_1_offset = ccm_config_1_base;
	ccm_config_2_offset = ccm_config_2_base;

	for (i = 0; i < XISP_CCM_MATRIX_DIM1; i++) {
		for (j = 0; j < XISP_CCM_MATRIX_DIM2; j++) {
			xvip_write(&xisp->xvip, ccm_config_1_offset, ccm_matrix[i][j]);
			ccm_config_1_offset += 4;
		}
		xvip_write(&xisp->xvip, ccm_config_2_offset, offsetarray[i]);
		ccm_config_2_offset += 4;
	}
}

/**
 * xisp_set_decomp_entries - Set decomposition entries for the XISP device.
 * @xisp: Pointer to the xisp_dev structure representing the device.
 * @decomp_base: Base address in the device's register space where decomposition entries will
 *		 be written.
 * @decomp: Pointer to a 3-dimensional array containing the decomposition values to be written.
 *          The dimensions of this array are defined by XISP_HDR_DECOMP_COLOR_ID,
 *          XISP_HDR_DECOMP_KNEE_POINTS, and XISP_HDR_DECOMP_PARAMS.
 *
 * This function writes the decomposition values from the provided 3D array to the device's
 * registers.
 * The decomposition values are written sequentially starting from the address specified by
 * decomp_base.
 * Each value is written to an offset that increments by 4 bytes (32 bits) for each subsequent
 * value.
 *
 * The loops iterate through:
 * - i: Color ID, ranging from 0 to XISP_HDR_DECOMP_COLOR_ID - 1
 * - j: Knee points, ranging from 0 to XISP_HDR_DECOMP_KNEE_POINTS - 1
 * - k: Parameters, ranging from 0 to XISP_HDR_DECOMP_PARAMS - 1
 *
 * Inside the innermost loop, the function calls xvip_write() to write each decomposition value
 * to thedevice's register at the current offset, and then increments the offset by 4 bytes.
 */
static void xisp_set_decomp_entries(struct xisp_dev *xisp, const u32 decomp_base,
				    const u32 (*decomp)[XISP_HDR_DECOMP_KNEE_POINTS]
						       [XISP_HDR_DECOMP_PARAMS])
{
	int idx;
	u32 decomp_offset;
	int total_elements = XISP_HDR_DECOMP_COLOR_ID * XISP_HDR_DECOMP_KNEE_POINTS *
			     XISP_HDR_DECOMP_PARAMS;
	const u32 *decomp_ptr = &decomp[0][0][0];

	decomp_offset = decomp_base;
	for (idx = 0; idx < total_elements; idx++)
		xvip_write(&xisp->xvip, decomp_offset  * (idx * 4), decomp_ptr[idx]);
}

/**
 * xisp_set_rgbir_entries - Set RGBIR entries for the XISP device.
 * @xisp: Pointer to the xisp_dev structure representing the device.
 * @rgbir_base: Base address in the device's register space where RGBIR entries will be written.
 * @rgbir_param: Pointer to an array containing the RGBIR parameters to be written.
 * @size: Size of the rgbir_param array.
 *
 * This function writes the RGBIR parameters from the provided array to the device's registers.
 * The parameters are written sequentially starting from the address specified by rgbir_base.
 *
 * The loop iterates through the rgbir_param array from index 0 to size - 1.
 * Inside the loop, the function calls xvip_write() to write each RGBIR parameter to the device
 * register at the base address specified by rgbir_base. The base address remains the same for
 * each parameter being written.
 */
static void xisp_set_rgbir_entries(struct xisp_dev *xisp, const u32 rgbir_base, const s8
				    *rgbir_param, int size)
{
	int i;

	for (i = 0; i < size; i++)
		xvip_write(&xisp->xvip, rgbir_base, rgbir_param[i]);
}

/**
 * xisp_set_gamma_common - Set gamma LUT entries based on conditions
 * @lut: Pointer to the lookup table
 * @base_reg: Base register for gamma configuration
 * @xisp: Pointer to the Xilinx ISP device structure
 * @in_type_conf_gamma: Boolean indicating if the input type configuration allows gamma
 *
 * This function sets the gamma LUT entries if the gamma is enabled,
 * not bypassed, and the input type configuration allows gamma. It
 * retrieves the gamma enable and bypass status from the device
 * configuration.
 */
static void xisp_set_gamma_common(const u32 *lut, const u32 base_reg, struct xisp_dev *xisp,
				  bool in_type_conf_gamma)
{
	bool gamma_enabled = XGET_BIT(XISP_GAMMA_INDEX, xisp->module_en);
	bool gamma_bypass_enabled = XGET_BIT(XISP_GAMMA_INDEX, xisp->module_bypass_en);
	bool gamma_bypass = XGET_BIT(XISP_GAMMA_INDEX, xisp->module_bypass);

	if (gamma_enabled & in_type_conf_gamma) {
		if (!gamma_bypass_enabled || !(gamma_bypass_enabled && gamma_bypass))
			xisp_set_lut_entries(xisp, lut, base_reg);
	}
}

/**
 * xisp_handle_hdr_enabled - Handle HDR enabled conditions
 * @xisp: Pointer to the Xilinx ISP device structure
 *
 * This function checks the HDR enabled conditions and sets the LUT entries
 * if the HDR is enabled, not bypassed, and HDR mode is not enabled.
 */
static void xisp_handle_hdr_enabled(struct xisp_dev *xisp)
{
	bool hdr_enabled = XGET_BIT(XISP_HDR_INDEX, xisp->module_en);
	bool hdr_bypass_enabled = XGET_BIT(XISP_HDR_INDEX, xisp->module_bypass_en);
	bool hdr_bypass = XGET_BIT(XISP_HDR_INDEX, xisp->module_bypass);
	bool hdr_mode_enabled = XGET_BIT(XISP_HDR_MODE_INDEX, xisp->module_en);

	if (hdr_enabled) {
		if (!hdr_bypass_enabled || !(hdr_bypass_enabled && hdr_bypass)) {
			if (!hdr_mode_enabled) {
				xisp_hdrmg_set_lut_entries(xisp, XISP_HDR_MERGE_CONFIG_BASE,
							   xisp->weights1, xisp->weights2);
			} else if (hdr_mode_enabled) {
				xisp->decompand_lut =
				xisp_decompand_choices[xisp->decompand_select];
				xisp_set_decomp_entries(xisp, XISP_HDR_DECOM_CONFIG_BASE,
							xisp->decompand_lut);
			}
		}
	}
}

static int xisp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xisp_dev *xisp =
		container_of(ctrl->handler,
			     struct xisp_dev, ctrl_handler);
	bool degamma_enabled, degamma_bypass_enabled, degamma_bypass;
	bool tm_type_enabled;
	bool ccm_enabled, ccm_bypass_enabled, ccm_bypass;
	bool in_type_conf_gain = XGET_BIT(XISP_IN_TYPE_INDEX, xisp->module_conf);
	bool in_type_conf_gamma = XGET_BIT(XISP_IN_TYPE_INDEX, xisp->module_conf);

	tm_type_enabled = (FIELD_GET(GENMASK(XISP_TM_TYPE_MSB_INDEX, XISP_TM_TYPE_LSB_INDEX),
				     xisp->module_en) == 1) ? true : false;

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
	case V4L2_CID_XILINX_ISP_RGBIR_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_RGBIR_INDEX, ctrl->val);
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
	case V4L2_CID_XILINX_ISP_LSC_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_LSC_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_DEMOSAIC_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_DEMOSAIC_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_CCM_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_CCM_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_CCM_PARAMS:
		xisp->ccm_select = ctrl->val;

		ccm_enabled = XGET_BIT(XISP_CCM_INDEX, xisp->module_en);
		ccm_bypass_enabled = XGET_BIT(XISP_CCM_INDEX, xisp->module_bypass_en);
		ccm_bypass = XGET_BIT(XISP_CCM_INDEX, xisp->module_bypass);

		if (ccm_enabled)
			if (!ccm_bypass_enabled || !(ccm_bypass_enabled && ccm_bypass)) {
				xisp->ccm_matrix_lut = xisp_ccm_matrix_choices[xisp->ccm_select];
				xisp->ccm_offsetarray_lut =
				xisp_ccm_offsetarray_choices[xisp->ccm_select];
				xisp_set_ccm_matrix_entries(xisp, XISP_CCM_CONFIG_1_BASE,
							    XISP_CCM_CONFIG_2_BASE,
							    xisp->ccm_matrix_lut,
							    xisp->ccm_offsetarray_lut);
			}
		break;
	case V4L2_CID_XILINX_ISP_GAIN_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_GAIN_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_GAIN_CONTROL_RED_GAIN:
		xisp->rgain = ctrl->val;
		if (in_type_conf_gain) {
			XISP_SET_CFG(XISP_GAIN_INDEX, XISP_GAIN_CONTROL_CONFIG_1_REG,
				     (FIELD_PREP(GENMASK(XISP_UPPER_WORD_MSB,
							 XISP_UPPER_WORD_LSB),
							 xisp->bgain)) |
							 xisp->rgain);
		}
		break;
	case V4L2_CID_XILINX_ISP_GAIN_CONTROL_BLUE_GAIN:
		xisp->bgain = ctrl->val;
		if (in_type_conf_gain) {
			XISP_SET_CFG(XISP_GAIN_INDEX, XISP_GAIN_CONTROL_CONFIG_1_REG,
				     (FIELD_PREP(GENMASK(XISP_UPPER_WORD_MSB,
							 XISP_UPPER_WORD_LSB),
							 xisp->bgain)) |
							 xisp->rgain);
		}
		break;
	case V4L2_CID_XILINX_ISP_GAIN_CONTROL_GREEN_GAIN:
		xisp->ggain = ctrl->val;
		if (in_type_conf_gain) {
			XISP_SET_CFG(XISP_GAIN_INDEX, XISP_GAIN_CONTROL_CONFIG_2_REG,
				     (FIELD_PREP(GENMASK(XISP_UPPER_WORD_MSB,
							 XISP_UPPER_WORD_LSB),
							 xisp->bayer_fmt)) |
							 xisp->ggain);
		}
		break;
	case V4L2_CID_XILINX_ISP_LUMA_GAIN:
		xisp->luma_gain = ctrl->val;
		if (!in_type_conf_gain)
			XISP_SET_CFG(XISP_GAIN_INDEX, XISP_GAIN_CONTROL_CONFIG_1_REG,
				     xisp->luma_gain);
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
	case V4L2_CID_XILINX_ISP_GAMMA_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_GAMMA_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_RED_GAMMA:
		xisp_select_gamma(ctrl->val, &xisp->red_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Red Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		if (xisp->config->flags & XILINX_ISP_VERSION_1)
			xisp_set_lut_entries(xisp, xisp->red_lut, XISP_GAMMA_RED_REG);
		else if (xisp->config->flags & XILINX_ISP_VERSION_2)
			xisp_set_gamma_common(xisp->red_lut, XISP_GAMMA_CONFIG_BASE,
					      xisp, in_type_conf_gamma);
		break;
	case V4L2_CID_XILINX_ISP_GREEN_GAMMA:
		xisp_select_gamma(ctrl->val, &xisp->green_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Green Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		if (xisp->config->flags & XILINX_ISP_VERSION_1)
			xisp_set_lut_entries(xisp, xisp->green_lut, XISP_GAMMA_GREEN_REG);
		else if (xisp->config->flags & XILINX_ISP_VERSION_2)
			xisp_set_gamma_common(xisp->green_lut, XISP_GAMMA_CONFIG_BASE + 0x100,
					      xisp, in_type_conf_gamma);
		break;
	case V4L2_CID_XILINX_ISP_BLUE_GAMMA:
		xisp_select_gamma(ctrl->val, &xisp->blue_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Blue Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		if (xisp->config->flags & XILINX_ISP_VERSION_1)
			xisp_set_lut_entries(xisp, xisp->blue_lut, XISP_GAMMA_BLUE_REG);
		else if (xisp->config->flags & XILINX_ISP_VERSION_2)
			xisp_set_gamma_common(xisp->blue_lut, XISP_GAMMA_CONFIG_BASE + 0x200,
					      xisp, in_type_conf_gamma);
		break;
	case V4L2_CID_GAMMA:
		xisp_select_gamma(ctrl->val, &xisp->mono_lut, xisp->gamma_table);
		dev_dbg(xisp->xvip.dev, "Setting Gamma to %d.%d",
			ctrl->val / 10, ctrl->val % 10);
		xisp_set_gamma_common(xisp->mono_lut, XISP_GAMMA_CONFIG_BASE, xisp,
				      (!in_type_conf_gamma));
		break;
	case V4L2_CID_XILINX_ISP_HDR_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_HDR_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_ALPHA:
		xisp->alpha = ctrl->val;
		xisp_hdr_merge(xisp->alpha, xisp->optical_black_value, xisp->intersec,
			       xisp->rho, xisp->weights1, xisp->weights2);

		xisp_handle_hdr_enabled(xisp);
		break;
	case V4L2_CID_XILINX_ISP_OPTICAL_BLACK_VALUE:
		xisp->optical_black_value = ctrl->val;
		xisp_hdr_merge(xisp->alpha, xisp->optical_black_value, xisp->intersec,
			       xisp->rho, xisp->weights1, xisp->weights2);

		xisp_handle_hdr_enabled(xisp);
		break;
	case V4L2_CID_XILINX_ISP_INTERSEC:
		xisp->intersec = ctrl->val;
		xisp_hdr_merge(xisp->alpha, xisp->optical_black_value, xisp->intersec,
			       xisp->rho, xisp->weights1, xisp->weights2);

		xisp_handle_hdr_enabled(xisp);
		break;
	case V4L2_CID_XILINX_ISP_RHO:
		xisp->rho = ctrl->val;
		xisp_hdr_merge(xisp->alpha, xisp->optical_black_value, xisp->intersec,
			       xisp->rho, xisp->weights1, xisp->weights2);

		xisp_handle_hdr_enabled(xisp);
		break;
	case V4L2_CID_XILINX_ISP_DECOMPAND_PARAMS:
		xisp->decompand_select = ctrl->val;

		xisp_handle_hdr_enabled(xisp);
		break;
	case V4L2_CID_XILINX_ISP_TM_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_TM_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_GTM_C1:
		xisp->gtm_c1 = ctrl->val;
		if (tm_type_enabled)
			XISP_SET_CFG(XISP_TM_INDEX, XISP_GTM_CONFIG_1_REG,
				     xisp->gtm_c1);
		break;
	case V4L2_CID_XILINX_ISP_GTM_C2:
		xisp->gtm_c2 = ctrl->val;
		if (tm_type_enabled)
			XISP_SET_CFG(XISP_TM_INDEX, XISP_GTM_CONFIG_2_REG,
				     xisp->gtm_c2);
		break;
	case V4L2_CID_XILINX_ISP_BLOCK_ROWS:
		xisp->block_rows = ctrl->val;
		tm_type_enabled = FIELD_GET(GENMASK(XISP_TM_TYPE_MSB_INDEX,
						    XISP_TM_TYPE_LSB_INDEX), xisp->module_en) == 0;
		if (!tm_type_enabled)
			XISP_SET_CFG(XISP_TM_INDEX, XISP_LTM_CONFIG_REG,
				     (FIELD_PREP(GENMASK(XISP_UPPER_WORD_MSB,
							 XISP_UPPER_WORD_LSB),
							 xisp->block_rows)) |
							 xisp->block_cols);
		break;
	case V4L2_CID_XILINX_ISP_BLOCK_COLS:
		xisp->block_cols = ctrl->val;
		tm_type_enabled = FIELD_GET(GENMASK(XISP_TM_TYPE_MSB_INDEX,
						    XISP_TM_TYPE_LSB_INDEX), xisp->module_en) == 0;
		if (!tm_type_enabled)
			XISP_SET_CFG(XISP_TM_INDEX, XISP_LTM_CONFIG_REG,
				     (FIELD_PREP(GENMASK(XISP_UPPER_WORD_MSB,
							 XISP_UPPER_WORD_LSB),
							 xisp->block_rows)) |
							 xisp->block_cols);
		break;
	case V4L2_CID_XILINX_ISP_3DLUT_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_LUT3D_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_3DLUT_DIM:
		xisp->lut3d_dim = ctrl->val;
		XISP_SET_CFG(XISP_LUT3D_INDEX, XISP_LUT3D_CONFIG_REG, xisp->lut3d_dim);
		break;
	case V4L2_CID_XILINX_ISP_CSC_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_CSC_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_BAYER_STATS_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_BAYER_STATS_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_LUMA_STATS_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_LUMA_STATS_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_RGB_STATS_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_RGB_STATS_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_CLAHE_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_CLAHE_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_CLIP:
		xisp->clip = ctrl->val;
		XISP_SET_CFG(XISP_CLAHE_INDEX, XISP_CLAHE_CONFIG_1_REG, xisp->clip);
		break;
	case V4L2_CID_XILINX_ISP_TILESY:
		xisp->tiles_y = ctrl->val;
		XISP_SET_CFG(XISP_CLAHE_INDEX, XISP_CLAHE_CONFIG_2_REG,
			     (FIELD_PREP(GENMASK(XISP_UPPER_WORD_MSB,
						 XISP_UPPER_WORD_LSB),
						 xisp->tiles_y)) |
						 xisp->tiles_x);
		break;
	case V4L2_CID_XILINX_ISP_TILESX:
		xisp->tiles_x = ctrl->val;
		XISP_SET_CFG(XISP_CLAHE_INDEX, XISP_CLAHE_CONFIG_2_REG,
			     (FIELD_PREP(GENMASK(XISP_UPPER_WORD_MSB,
						 XISP_UPPER_WORD_LSB),
						 xisp->tiles_y)) |
						 xisp->tiles_x);
		break;
	case V4L2_CID_XILINX_ISP_MEDIAN_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_MEDIAN_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_ISP_EN_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
		break;
	case V4L2_CID_XILINX_ISP_RESIZE_EN:
		xisp->module_bypass = xisp_module_bypass(xisp->module_bypass,
							 XISP_RESIZE_INDEX, ctrl->val);
		xvip_write(&xisp->xvip, XISP_FUNCS_BYPASS_CONFIG_REG, xisp->module_bypass);
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

static struct v4l2_ctrl_config xisp_ctrls_rgbir[] = {
	/* RGBIR ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RGBIR_EN,
		.name = "bypass_rgbir",
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

static struct v4l2_ctrl_config xisp_ctrls_lsc[] = {
	/* LSC ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_LSC_EN,
		.name = "bypass_lsc",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_demosaic[] = {
	/* DEMOSAIC ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_DEMOSAIC_EN,
		.name = "bypass_demosaic",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static const char *const xisp_ccm_string_char[] = {
	/* CCM MENU LIST */
	"bt2020_bt709_arr",
	"bt709_bt2020_arr",
	"rgb_yuv_601_arr",
	"rgb_yuv_709_arr",
	"rgb_yuv_2020_arr",
	"yuv_rgb_601_arr",
	"yuv_rgb_709_arr",
	"yuv_rgb_2020_arr",
	"full_to_16_235_arr",
	"full_from_16_235_arr",
};

static struct v4l2_ctrl_config xisp_ctrls_ccm_pattern[] = {
	/* CCM ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_CCM_EN,
		.name = "bypass_ccm",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	}
};

static struct v4l2_ctrl_config xisp_ctrls_ccm_pattern_menu[] = {
	/* CCM ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_CCM_PARAMS,
		.name = "select_ccm",
		.type = V4L2_CTRL_TYPE_MENU,
		.min = XISP_MIN_VALUE,
		.max = ARRAY_SIZE(xisp_ccm_string_char) - 1,
		.qmenu	= xisp_ccm_string_char,
	}
};

static struct v4l2_ctrl_config xisp_ctrls_gain_control[] = {
	/* GAIN CONTROL ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GAIN_EN,
		.name = "bypass_gain_control",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* RED GAIN*/
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GAIN_CONTROL_RED_GAIN,
		.name = "red_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_RED_GAIN_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* BLUE GAIN */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GAIN_CONTROL_BLUE_GAIN,
		.name = "blue_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_BLUE_GAIN_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* GREEN GAIN */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GAIN_CONTROL_GREEN_GAIN,
		.name = "green_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_GREEN_GAIN_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	}
};

static struct v4l2_ctrl_config xisp_ctrls_gamma_correct[] = {
	/* GAMMA ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GAMMA_EN,
		.name = "bypass_gamma",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* RED GAMMA */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RED_GAMMA,
		.name = "red_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = XISP_RED_GAMMA_MAX,
		.step = 1,
		.def = XISP_RED_GAMMA_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* GREEN GAMMA */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GREEN_GAMMA,
		.name = "green_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = XISP_GREEN_GAMMA_MAX,
		.step = 1,
		.def = XISP_GREEN_GAMMA_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* BLUE GAMMA */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLUE_GAMMA,
		.name = "blue_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = XISP_BLUE_GAMMA_MAX,
		.step = 1,
		.def = XISP_BLUE_GAMMA_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_mono[] = {
	/* LUMIANCE GAIN*/
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_LUMA_GAIN,
		.name = "mono_luma_gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_MONO_LUMA_GAIN_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* GAMMA */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_GAMMA,
		.name = "mono_gamma",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = XISP_MONO_GAMMA_MAX,
		.step = 1,
		.def = XISP_MONO_GAMMA_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_hdr[] = {
	/* HDR ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_HDR_EN,
		.name = "bypass_hdr",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_hdr_merge[] = {
	/* ALPHA */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_ALPHA,
		.name = "hdr_alpha",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = XISP_HDR_ALPHA_MAX,
		.step = 1,
		.def = XISP_HDR_ALPHA_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* OPTICAL BLACK VALUE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_OPTICAL_BLACK_VALUE,
		.name = "hdr_optical_black_value",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_HDR_OPTICAL_BLACK_VALUE_MAX,
		.step = 1,
		.def = XISP_HDR_OPTICAL_BLACK_VALUE_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* INTERSEC */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_INTERSEC,
		.name = "hdr_intersec",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_HDR_INTERSEC_MAX,
		.step = 1,
		.def = XISP_HDR_INTERSEC_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* RHO */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RHO,
		.name = "hdr_rho",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = XISP_HDR_RHO_MAX,
		.step = 1,
		.def = XISP_HDR_RHO_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_hdr_decom[] = {
	/* HDR DECOMPAND ARRAY SELECT */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_DECOMPAND_PARAMS,
		.name = "select_decompand",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_SELECT_DECOMPAND_MAX,
		.step = 1,
		.def = XISP_SELECT_DECOMPAND_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_tm[] = {
	/* TM ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_TM_EN,
		.name = "bypass_tm",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_gtm[] = {
	/* PARAMETER C1 */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GTM_C1,
		.name = "gtm_c1",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_GTM_C1_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* PARAMETER C2 */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_GTM_C2,
		.name = "gtm_c2",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_GTM_C2_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_ltm[] = {
	/* BLOCK ROWS */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLOCK_ROWS,
		.name = "ltm_block_height",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_LTM_BLOCK_HEIGHT_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* BLOCK COLS */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BLOCK_COLS,
		.name = "ltm_block_width",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_LTM_BLOCK_WIDTH_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_lut3d[] = {
	/* 3DLUT ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_3DLUT_EN,
		.name = "bypass_3dlut",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_3DLUT_DIM,
		.name = "3dlut_dim",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_3DLUT_DIM_MAX,
		.step = 1,
		.def = XISP_3DLUT_DIM_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_csc[] = {
	/* CSC  ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_CSC_EN,
		.name = "bypass_csc",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_bayer_stats[] = {
	/* BAYER STATS ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_BAYER_STATS_EN,
		.name = "bypass_bayer_stats",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_luma_stats[] = {
	/* LUMA STATS ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_LUMA_STATS_EN,
		.name = "bypass_luma_stats",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_rgb_stats[] = {
	/* RGB STATS ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RGB_STATS_EN,
		.name = "bypass_rgb_stats",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_clahe[] = {
	/* CLAHE ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_CLAHE_EN,
		.name = "bypass_clahe",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* CLIP */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_CLIP,
		.name = "clahe_clip",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_CLAHE_CLIP_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* TILESY */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_TILESY,
		.name = "clahe_tiles_y",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_CLAHE_TILES_Y_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* TILESX */
	{
		.ops = &xisp_ctrl_ops,
		.id =  V4L2_CID_XILINX_ISP_TILESX,
		.name = "clahe_tiles_x",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = XISP_MIN_VALUE,
		.max = XISP_MAX_VALUE,
		.step = 1,
		.def = XISP_CLAHE_TILES_X_DEFALUT,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_median[] = {
	/* MEDIAN BLUR ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_MEDIAN_EN,
		.name = "bypass_median",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static struct v4l2_ctrl_config xisp_ctrls_resize[] = {
	/* RESIZE ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_RESIZE_EN,
		.name = "bypass_resize",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.min = XISP_MIN_VALUE,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	}
};

static struct v4l2_ctrl_config xisp_ctrls_isp[] = {
	/* ISP ENABLE/DISABLE */
	{
		.ops = &xisp_ctrl_ops,
		.id = V4L2_CID_XILINX_ISP_EN,
		.name = "bypass_isp",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
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

		if (FIELD_GET(BIT(XISP_RGBIR_INDEX), xisp->module_en))
			if (!FIELD_GET(BIT(XISP_RGBIR_INDEX), xisp->module_bypass_en) ||
			    !(FIELD_GET(BIT(XISP_RGBIR_INDEX), xisp->module_bypass_en) &
			    FIELD_GET(BIT(XISP_RGBIR_INDEX), xisp->module_bypass))) {
				xisp_set_rgbir_entries(xisp, XISP_RGBIR_CONFIG_BASE,
						       xisp_rgbir_config,
						       XISP_RGBIR_CONFIG_BASE_SIZE);
				xisp_set_rgbir_entries(xisp, XISP_RGBIR_CONFIG_BASE1,
						       xisp_rgbir_config +
						       XISP_RGBIR_CONFIG_BASE_1_LSB,
						       XISP_RGBIR_CONFIG_BASE_1_SIZE);
				xisp_set_rgbir_entries(xisp, XISP_RGBIR_CONFIG_BASE2,
						       xisp_rgbir_config +
						       XISP_RGBIR_CONFIG_BASE_2_LSB,
						       XISP_RGBIR_CONFIG_BASE_2_SIZE);
				xisp_set_rgbir_entries(xisp, XISP_RGBIR_CONFIG_BASE3,
						       xisp_rgbir_config +
						       XISP_RGBIR_CONFIG_BASE_3_LSB,
						       XISP_RGBIR_CONFIG_BASE_3_SIZE);
				xisp_set_rgbir_entries(xisp, XISP_RGBIR_CONFIG_BASE4,
						       xisp_rgbir_config +
						       XISP_RGBIR_CONFIG_BASE_4_LSB,
						       XISP_RGBIR_CONFIG_BASE_4_SIZE);
				xisp_set_rgbir_entries(xisp, XISP_RGBIR_CONFIG_BASE5,
						       xisp_rgbir_config +
						       XISP_RGBIR_CONFIG_BASE_5_LSB,
						       XISP_RGBIR_CONFIG_BASE_5_SIZE);
			}
		if (XGET_BIT(XISP_LUT3D_INDEX, xisp->module_en))
			if (!XGET_BIT(XISP_LUT3D_INDEX, xisp->module_bypass_en) ||
			    !(XGET_BIT(XISP_LUT3D_INDEX, xisp->module_bypass_en) &
			    XGET_BIT(XISP_LUT3D_INDEX, xisp->module_bypass)))
				xisp_set_lut3d_entries(xisp, XISP_LUT3D_CONFIG_BASE, xisp->lut3d);
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
		xvip_write(&xisp->xvip, XISP_GAIN_CONTROL_CONFIG_2_REG,
			   (xisp->bayer_fmt << XISP_UPPER_WORD_LSB) | xisp->ggain);
		dev_dbg(xisp->xvip.dev, "Unsupported format for Sink Pad");
		return false;
	}
	xvip_write(&xisp->xvip, XISP_GAIN_CONTROL_CONFIG_2_REG,
		   (xisp->bayer_fmt << XISP_UPPER_WORD_LSB) | xisp->ggain);
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
			xisp->resize_new_width  = __format->width;
			xisp->resize_new_height = __format->height;
			xvip_write(&xisp->xvip, XISP_RESIZE_CONFIG_REG,
				   (xisp->resize_new_height << XISP_UPPER_WORD_LSB) |
				   xisp->resize_new_width);
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
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_rgbir);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_lsc);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_demosaic);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_ccm_pattern_menu);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_ccm_pattern);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_gain_control);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_mono);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_gamma_correct);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_hdr);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_hdr_merge);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_hdr_decom);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_tm);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_gtm);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_ltm);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_lut3d);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_csc);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_bayer_stats);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_luma_stats);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_rgb_stats);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_clahe);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_median);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_resize);
		num_of_parameters += ARRAY_SIZE(xisp_ctrls_isp);

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
		xisp_create_controls(xisp, XISP_RGBIR_INDEX,
				     xisp_ctrls_rgbir, ARRAY_SIZE(xisp_ctrls_rgbir));
		xisp_create_controls(xisp, XISP_LSC_INDEX, xisp_ctrls_lsc,
				     ARRAY_SIZE(xisp_ctrls_lsc));
		xisp_create_controls(xisp, XISP_DEMOSAIC_INDEX,
				     xisp_ctrls_demosaic, ARRAY_SIZE(xisp_ctrls_demosaic));
		xisp_create_controls(xisp, XISP_CCM_INDEX,
				     xisp_ctrls_ccm_pattern, ARRAY_SIZE(xisp_ctrls_ccm_pattern));
		if (XGET_BIT(XISP_CCM_INDEX, xisp->module_en))
			v4l2_ctrl_new_custom(&xisp->ctrl_handler,
					     &xisp_ctrls_ccm_pattern_menu[0], NULL);
		if (XGET_BIT(XISP_IN_TYPE_INDEX, xisp->module_conf)) {
			xisp_create_controls(xisp, XISP_GAIN_INDEX,
					     xisp_ctrls_gain_control,
					     ARRAY_SIZE(xisp_ctrls_gain_control));
		}

		if (XGET_BIT(XISP_GAIN_INDEX, xisp->module_en) &&
		    !XGET_BIT(XISP_IN_TYPE_INDEX, xisp->module_conf)) {
			if (XGET_BIT(XISP_GAIN_INDEX, xisp->module_bypass_en))
				v4l2_ctrl_new_custom(&xisp->ctrl_handler,
						     &xisp_ctrls_gain_control[0], NULL);
			v4l2_ctrl_new_custom(&xisp->ctrl_handler, &xisp_ctrls_mono[0], NULL);
		}

		if (XGET_BIT(XISP_GAMMA_INDEX, xisp->module_en)) {
			if (XGET_BIT(XISP_GAMMA_INDEX, xisp->module_bypass_en))
				v4l2_ctrl_new_custom(&xisp->ctrl_handler,
						     &xisp_ctrls_gamma_correct[0], NULL);

			if (!XGET_BIT(XISP_IN_TYPE_INDEX, xisp->module_conf))
				v4l2_ctrl_new_custom(&xisp->ctrl_handler,
						     &xisp_ctrls_mono[1], NULL);
			else if (XGET_BIT(XISP_IN_TYPE_INDEX, xisp->module_conf))
				for (itr = 1; itr < ARRAY_SIZE(xisp_ctrls_gamma_correct); itr++) {
					v4l2_ctrl_new_custom(&xisp->ctrl_handler,
							     &xisp_ctrls_gamma_correct[itr], NULL);
				}
		}

		if (XGET_BIT(XISP_HDR_INDEX, xisp->module_en)) {
			xisp_create_controls(xisp, XISP_HDR_INDEX,
					     xisp_ctrls_hdr, ARRAY_SIZE(xisp_ctrls_hdr));
			xisp_create_controls(xisp, XISP_HDR_INDEX,
					     xisp_ctrls_hdr_decom,
					     ARRAY_SIZE(xisp_ctrls_hdr_decom));
			if (!XGET_BIT(XISP_HDR_MODE_INDEX, xisp->module_en)) {
				for (itr = 0; itr < ARRAY_SIZE(xisp_ctrls_hdr_merge); itr++) {
					v4l2_ctrl_new_custom(&xisp->ctrl_handler,
							     &xisp_ctrls_hdr_merge[itr], NULL);
				}
			}
		}

		if (XGET_BIT(XISP_TM_INDEX, xisp->module_en)) {
			u32 xisp_tm_type =
			FIELD_GET(GENMASK(XISP_TM_TYPE_MSB_INDEX,
					  XISP_TM_TYPE_LSB_INDEX), xisp->module_en);

			if (XGET_BIT(XISP_TM_INDEX, xisp->module_bypass_en))
				v4l2_ctrl_new_custom(&xisp->ctrl_handler, &xisp_ctrls_tm[0], NULL);
			if (xisp_tm_type == 0) {
				for (itr = 0; itr < ARRAY_SIZE(xisp_ctrls_ltm); itr++) {
					v4l2_ctrl_new_custom(&xisp->ctrl_handler,
							     &xisp_ctrls_ltm[itr], NULL);
				}
			}
			if (xisp_tm_type == 1) {
				for (itr = 1; itr < (ARRAY_SIZE(xisp_ctrls_gtm) - 1); itr++) {
					v4l2_ctrl_new_custom(&xisp->ctrl_handler,
							     &xisp_ctrls_gtm[itr], NULL);
				}
			}
		}

		xisp_create_controls(xisp, XISP_LUT3D_INDEX,
				     xisp_ctrls_lut3d, ARRAY_SIZE(xisp_ctrls_lut3d));
		xisp_create_controls(xisp, XISP_CSC_INDEX,
				     xisp_ctrls_csc, ARRAY_SIZE(xisp_ctrls_csc));
		xisp_create_controls(xisp, XISP_BAYER_STATS_INDEX,
				     xisp_ctrls_bayer_stats, ARRAY_SIZE(xisp_ctrls_bayer_stats));
		xisp_create_controls(xisp, XISP_LUMA_STATS_INDEX,
				     xisp_ctrls_luma_stats, ARRAY_SIZE(xisp_ctrls_luma_stats));
		xisp_create_controls(xisp, XISP_RGB_STATS_INDEX,
				     xisp_ctrls_rgb_stats, ARRAY_SIZE(xisp_ctrls_rgb_stats));
		xisp_create_controls(xisp, XISP_CLAHE_INDEX,
				     xisp_ctrls_clahe, ARRAY_SIZE(xisp_ctrls_clahe));
		xisp_create_controls(xisp, XISP_MEDIAN_INDEX,
				     xisp_ctrls_median, ARRAY_SIZE(xisp_ctrls_median));
		xisp_create_controls(xisp, XISP_RESIZE_INDEX,
				     xisp_ctrls_resize, ARRAY_SIZE(xisp_ctrls_resize));
		if (XGET_BIT(XISP_ISP_EN_INDEX, xisp->module_bypass_en))
			v4l2_ctrl_new_custom(&xisp->ctrl_handler, &xisp_ctrls_isp[0], NULL);
	} else {
		v4l2_ctrl_handler_init(&xisp->ctrl_handler, ARRAY_SIZE(xisp_ctrls) +
				       ARRAY_SIZE(xisp_ctrls_gamma_correct));
		for (itr = 0; itr < ARRAY_SIZE(xisp_ctrls); itr++) {
			v4l2_ctrl_new_custom(&xisp->ctrl_handler,
					     &xisp_ctrls[itr], NULL);
		}
		for (itr = 1; itr < ARRAY_SIZE(xisp_ctrls_gamma_correct); itr++) {
			v4l2_ctrl_new_custom(&xisp->ctrl_handler,
					     &xisp_ctrls_gamma_correct[itr], NULL);
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

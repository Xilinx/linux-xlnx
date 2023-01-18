/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#ifndef __XILINX_ISP_PARAMS_H__
#define __XILINX_ISP_PARAMS_H__

#define XISP_DEGAMMA_COLOR_ID (3)
#define XISP_DEGAMMA_KNEE_POINTS (8)
#define XISP_DEGAMMA_PARAMS (3)
#define XISP_RGBIR_LENGTH (97)
#define XISP_CCM_MATRIX_DIM1 (3)
#define XISP_CCM_MATRIX_DIM2 (3)
#define XISP_HDR_DECOMP_COLOR_ID	(3)
#define XISP_HDR_DECOMP_KNEE_POINTS	(4)
#define XISP_HDR_DECOMP_PARAMS		(3)

/*
 * XISP_DEGAMMA_COLOR_ID = COLOR ID (R, G, B)
 * XISP_DEGAMMA_KNEE_POINTS = Number of knee points (we support 4 and 8)
 * XISP_DEGAMMA_PARAMS =  Contains 3 values (max_value for range, slope and constant)
 *
 * The function xisp_set_degamma_entries function writes degamma values from the
 * provided 3D array to the device's registers.
 * The degamma values are written sequentially starting from the address specified by
 * degamma_base.
 * Each value is written to an offset that increments by 4 bytes (32 bits) for each
 * subsequent value.
 */
static const u32 xisp_degamma_choices[2][XISP_DEGAMMA_COLOR_ID][XISP_DEGAMMA_KNEE_POINTS]
				     [XISP_DEGAMMA_PARAMS] = {{
	/* 8-bit */
	{
		{ 32, 1311, 0 },   { 64, 4915, 7 },   { 96, 9011, 23 },  { 128, 13414, 49 },
		{ 160, 18918, 84 }, { 192, 22529, 132 }, { 224, 28672, 200 }, { 256, 32768, 256 }
	},
	{
		{ 32, 1311, 0 },   { 64, 4915, 7 },   { 96, 9011, 23 },  { 128, 13414, 49 },
		{ 160, 18918, 84 }, { 192, 22529, 132 }, { 224, 28672, 200 }, { 256, 32768, 256 }
	},
	{
		{ 32, 1311, 0 },   { 64, 4915, 7 },   { 96, 9011, 23 },  { 128, 13414, 49 },
		{ 160, 18918, 84 }, { 192, 22529, 132 }, { 224, 28672, 200 }, { 256, 32768, 256 }
	}
}, {
	/* 16-bit */
	{
		{ 8192, 1345, 0 },   { 16384, 4853, 1749 }, { 24576, 8933, 5825 },
		{ 32768, 13365, 12476 }, { 40960, 18023, 21782 }, { 49152, 22938, 34162 },
		{ 57344, 28088, 49506 }, { 65536, 32768, 65536 }
	},
	{
		{ 8192, 1345, 0 },   { 16384, 4853, 1749 }, { 24576, 8933, 5825 },
		{ 32768, 13365, 12476 }, { 40960, 18023, 21782 }, { 49152, 22938, 34162 },
		{ 57344, 28088, 49506 }, { 65536, 32768, 65536 }
	},
	{
		{ 8192, 1345, 0 },   { 16384, 4853, 1749 }, { 24576, 8933, 5825 },
		{ 32768, 13365, 12476 }, { 40960, 18023, 21782 }, { 49152, 22938, 34162 },
		{ 57344, 28088, 49506 }, { 65536, 32768, 65536 }
	}
}};

/*
 * The function xisp_set_rgbir_entries writes the RGBIR config parameters from
 * the provided array to the device's registers.
 * The parameters are written sequentially starting from the address specified
 * by rgbir_base.
 */
static const s8 xisp_rgbir_config[XISP_RGBIR_LENGTH] = {
6, 6, 6, 6, 6, 6, 6, 6, 0, 6, 6, 6, 6, 6, 6, 6, 0,
6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 6, 6,
6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 6, 6, 6, 6, 6, 6, 6,
6, 0, 6, 6, 6, 6, 6, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6,
6, 6, 6, 6, 0, 6, 6, 2, 6, 2, 6, 6, 6, 2, 6, 2, 2,
6, 2, 6, 6, 6, 2, 6, 2, 3, 1, 2, 5};

/*
 * XISP_CCM_MATRIX_DIM1 =  Size of CCM matrix entries.
 * XISP_CCM_MATRIX_DIM2 =  Size of offset entries.
 *
 * The function xisp_set_ccm_matrix_entries writes the CCM matrix values from
 * the provided 2D array to the
 * device's registers. After writing the values for each row of the matrix, it writes a
 * corresponding offset value from the offsetarray to the register space.
 *
 * The 10 most commonly used matrices are given.
 * (N,0,0) (N,1,0) (N,2,0) - These are 'r' channel multiplication factors.
 * (N,0,1) (N,1,1) (N,2,1) - These are 'g' channel multiplication factors.
 * (N,0,2) (N,1,2) (N,2,2) - These are 'b' channel multiplication factors.
 *
 * The range for below values is [-7 * 2^20 to 7 * 2^20]
 *
 * The 1st dimension of 10 can be extended to any number with values suitable
 * to the user, with in above range.
 */
static const signed int xisp_ccm_matrix_choices[10][XISP_CCM_MATRIX_DIM1][XISP_CCM_MATRIX_DIM2] = {
	/* bt2020_bt709_arr */
	{
		{1741160, -616143, -76336},
		{-130652, 1187931, -8703},
		{-19084, -105486, 1173042}
	},
	/* bt709_bt2020_arr */
	{
		{657457, 344981, 45403},
		{72456, 964689, 11848},
		{17196, 92274, 939524}
	},
	/* rgb_yuv_601_arr */
	{
		{191889, 643825, 65011},
		{-105906, -354418, 460324},
		{460324, -418381, -41943}
	},
	/* rgb_yuv_709_arr */
	{
		{236572, 610566, 53401},
		{-125743, -324527, 450271},
		{450271, -414056, -36214}
	},
	/* rgb_yuv_2020_arr */
	{
		{1220542, 0, 1673527},
		{1220542, -852492, -409993},
		{1220542, 2116026, 0}
	},
	/* yuv_rgb_601_arr */
	{
		{1220542, 0, 1880096},
		{1220542, -223346, -559939},
		{1220542, 2217738, 0}
	},
	/* yuv_rgb_709_arr */
	{
		{1220945, 0, 1800405},
		{1220945, -200910, -697590},
		{1220945, 2297085, 0}
	},
	/* yuv_rgb_2020_arr */
	{
		{897900, 0, 0},
		{0, 897900, 0},
		{0, 0, 897900}
	},
	/* full_to_16_235_arr */
	{
		{1224535, 0, 0},
		{0, 1224535, 0},
		{0, 0, 1224535}
	},
	/* full_from_16_235_arr */
	{
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0}
	}
};

/*
 * The 10 most commonly used matrices are given.
 * (N,0) -  'r' channel offset value.
 * (N,1) -  'g'  channel offset value.
 * (N,2) -  'b'  channel offset value.
 *
 * The range for above values is [-7 * 2^20 to 7 * 2^20]
 *
 * The 1st dimension of 10 can be extended to any number with
 * values suitable to the user, with in above range.
 */
static const signed int xisp_ccm_offsetarray_choices[10][XISP_CCM_MATRIX_DIM1] = {
	/* bt2020_bt709_off */
	{0, 0, 0},
	/* bt709_bt2020_off */
	{0, 0, 0},
	/* rgb_yuv_601_off */
	{65536, 524288, 524288},
	/* rgb_yuv_709_off */
	{65536, 524288, 524288},
	/* rgb_yuv_2020_off */
	{65792, 524288, 524288},
	/* yuv_rgb_601_off */
	{-913047, 554958, -1134297},
	/* yuv_rgb_709_off */
	{-1016332, 315359, -1185153},
	/* yuv_rgb_2020_off */
	{-976810, 372641, -1225151},
	/* full_to_16_235_off */
	{65536, 65536, 65536},
	/* full_from_16_235_off */
	{-76533, -76533, -76533}
};

/*
 * XISP_HDR_DECOMP_COLOR_ID = COLOR ID (R, G, B)
 * XISP_HDR_DECOMP_KNEE_POINTS = Number of knee points (we support 4)
 * XISP_HDR_DECOMP_PARAMS =  Contains 3 values (max_value for range, slope and constant)
 *
 * The function xisp_set_decomp_entries writes the decomposition values from the provided
 * 3D array to the device's registers.
 * The decomposition values are written sequentially starting from the address specified by
 * decomp_base.
 * Each value is written to an offset that increments by 4 bytes (32 bits) for each subsequent
 * value.
 */
static const u32 xisp_decompand_choices[3][XISP_HDR_DECOMP_COLOR_ID]
						[XISP_HDR_DECOMP_KNEE_POINTS]
						[XISP_HDR_DECOMP_PARAMS] = {{
	/* in: 12-bit, out:20-bit */
	{
		{512, 4, 0}, {1408, 16, 384}, {2176, 64, 1152}, {4096, 512, 2048}
	},
	{
		{512, 4, 0}, {1408, 16, 384}, {2176, 64, 1152}, {4096, 512, 2048}
	},
	{
		{512, 4, 0}, {1408, 16, 384}, {2176, 64, 1152}, {4096, 512, 2048}
	}
}, {
	/* in: 12-bit, out:16-bit */
	{
		{1024, 4, 0}, {1536, 8, 512}, {3072, 16, 1024}, {4096, 32, 2048}
	},
	{
		{1024, 4, 0}, {1536, 8, 512}, {3072, 16, 1024}, {4096, 32, 2048}
	},
	{
		{1024, 4, 0}, {1536, 8, 512}, {3072, 16, 1024}, {4096, 32, 2048}
	}
}, {
	/* in: 16-bit, out:24-bit */
	{
		{8192, 4, 0}, {22528, 16, 6144}, {34816, 64, 18432}, {65536, 512, 32768}
	},
	{
		{8192, 4, 0}, {22528, 16, 6144}, {34816, 64, 18432}, {65536, 512, 32768}
	},
	{
		{8192, 4, 0}, {22528, 16, 6144}, {34816, 64, 18432}, {65536, 512, 32768}
	}
}};

#endif /* __XILINX_ISP_PARAMS_H__ */

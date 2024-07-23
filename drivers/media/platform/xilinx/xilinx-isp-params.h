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

#endif /* __XILINX_ISP_PARAMS_H__ */

/*
 * Copyright (C) 2014 - 2015 Xilinx, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/types.h>
#include "s7_gtxe2_drp.h"
#include "gtx7s_qpll_bands.h"

static const u32 gtx7s_qpll_channel_address_lut
			[GTX7S_QPLL_NUM_CHANNEL_DRP_REGS] = {
	RXCDR_CFG0_ADDR,
	RXCDR_CFG1_ADDR,
	RXCDR_CFG2_ADDR,
	RXCDR_CFG3_ADDR,
	RXCDR_CFG4_ADDR,
	RXOUT_DIV_ADDR,
	TXOUT_DIV_ADDR,
	RX_DFE_LPM_CFG_ADDR,
	QPLL_CFG0_ADDR,
	QPLL_CFG1_ADDR
};

static const u32 gtx7s_qpll_channel_offset_lut
			[GTX7S_QPLL_NUM_CHANNEL_DRP_REGS] = {
	RXCDR_CFG0_OFFSET,
	RXCDR_CFG1_OFFSET,
	RXCDR_CFG2_OFFSET,
	RXCDR_CFG3_OFFSET,
	RXCDR_CFG4_OFFSET,
	RXOUT_DIV_OFFSET,
	TXOUT_DIV_OFFSET,
	RX_DFE_LPM_CFG_OFFSET,
	QPLL_CFG0_OFFSET,
	QPLL_CFG1_OFFSET
};

static const u32 gtx7s_qpll_channel_mask_lut
			[GTX7S_QPLL_NUM_CHANNEL_DRP_REGS] = {
	RXCDR_CFG0_MASK,
	RXCDR_CFG1_MASK,
	RXCDR_CFG2_MASK,
	RXCDR_CFG3_MASK,
	RXCDR_CFG4_MASK,
	RXOUT_DIV_MASK,
	TXOUT_DIV_MASK,
	RX_DFE_LPM_CFG_MASK,
	QPLL_CFG0_MASK,
	QPLL_CFG1_MASK
};

/* Note bands run vertically from 1 to 10 */
static const u16 gtx7s_qpll_channel_param_lut[GTX7S_QPLL_NUM_CHANNEL_DRP_REGS]
			[GTX7S_QPLL_NUM_LINE_RATE_BANDS] = {
{0x20,     0x20,   0x20,   0x20,     0x20,    0x20,    0x20,    0x20,    0x20,    0x20},/* RXCDR_CFG0 */
{0x1008,   0x1010, 0x1020, 0x1010,   0x1020,  0x1040,  0x1020,  0x1040,  0x1040,  0x1040},/* RXCDR_CFG1 */
{0x23ff,   0x23ff, 0x23ff, 0x23ff,   0x23ff,  0x23ff,  0x23ff,  0x23ff,  0x23ff,  0x23ff},/* RXCDR_CFG2 */
{0x0,      0x0,    0x0,    0x0,      0x0,     0x0,     0x0,     0x0,     0x0,     0x0}, /* RXCDR_CFG3 */
{0x3,      0x3,    0x3,    0x3,      0x3,     0x3,     0x3,     0x3,     0x3,     0x3},/* RXCDR_CFG4 */
{0x3e8,    0x4,    0x2,    0x3,      0x2,     0x1,     0x2,     0x1,     0x1,     0x1},/* RXOUT_DIV  */
{0x3e8,    0x4,    0x2,    0x3,      0x2,     0x1,     0x2,     0x1,     0x1,     0x1},/* TXOUT_DIV  */
{0x904,    0x904,  0x904,  0x904,    0x904,   0x904,   0x904,   0x904,   0x104,   0x104},/* RX_DFE_LPM_CFG */
{0x1c1,    0x1c1,  0x1c1,  0x181,    0x1c1,   0x1c1,   0x181,   0x1c1,   0x1c1,   0x181},/* QPLL_CFG0 */
{0x68,     0x68,   0x68,   0x68,     0x68,    0x68,    0x68,    0x68,    0x68,    0x68} /* QPLL_CFG1 */
};

u32 get_gtx7s_qpll_address_lut(u32 lut_address)
{
	return gtx7s_qpll_channel_address_lut[lut_address];
}

u32 get_gtx7s_qpll_offset_lut(u32 lut_address)
{
	return gtx7s_qpll_channel_offset_lut[lut_address];
}

u32 get_gtx7s_qpll_mask_lut(u32 lut_address)
{
	return gtx7s_qpll_channel_mask_lut[lut_address];
}

u16 get_gtx7s_qpll_param_lut(u32 param_address, u32 band_address)
{
	return gtx7s_qpll_channel_param_lut[param_address][band_address];
}

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
#define TXOUT_DIV_ADDR					0x88
#define TXOUT_DIV_MASK					0x70
#define TXOUT_DIV_OFFSET				0x4
#define TXOUT_DIV_WIDTH					0x3
#define TXOUT_DIV_DEFAULT				0x0

#define RXOUT_DIV_ADDR					0x88
#define RXOUT_DIV_MASK					0x7
#define RXOUT_DIV_OFFSET				0x0
#define RXOUT_DIV_WIDTH					0x3
#define RXOUT_DIV_DEFAULT				0x0

#define RXCDR_CFG0_ADDR					0xa8
#define RXCDR_CFG0_MASK					0xffff
#define RXCDR_CFG0_OFFSET				0x0
#define RXCDR_CFG0_WIDTH				0x10
#define RXCDR_CFG0_DEFAULT				0x0

#define RXCDR_CFG1_ADDR					0xa9
#define RXCDR_CFG1_MASK					0xffff
#define RXCDR_CFG1_OFFSET				0x0
#define RXCDR_CFG1_WIDTH				0x10
#define RXCDR_CFG1_DEFAULT				0x0

#define RXCDR_CFG2_ADDR					0xaa
#define RXCDR_CFG2_MASK					0xffff
#define RXCDR_CFG2_OFFSET				0x0
#define RXCDR_CFG2_WIDTH				0x10
#define RXCDR_CFG2_DEFAULT				0x0

#define RXCDR_CFG3_ADDR					0xab
#define RXCDR_CFG3_MASK					0xffff
#define RXCDR_CFG3_OFFSET				0x0
#define RXCDR_CFG3_WIDTH				0x10
#define RXCDR_CFG3_DEFAULT				0x0

#define RXCDR_CFG4_ADDR					0xac
#define RXCDR_CFG4_MASK					0xff
#define RXCDR_CFG4_OFFSET				0x0
#define RXCDR_CFG4_WIDTH				0x8
#define RXCDR_CFG4_DEFAULT				0x0

#define RX_DFE_LPM_CFG_ADDR				0x29
#define RX_DFE_LPM_CFG_MASK				0xffff
#define RX_DFE_LPM_CFG_OFFSET				0x0
#define RX_DFE_LPM_CFG_WIDTH				0x10
#define RX_DFE_LPM_CFG_DEFAULT				0x0

#define QPLL_CFG0_ADDR					0x32
#define QPLL_CFG0_MASK					0xffff
#define QPLL_CFG0_OFFSET				0x0
#define QPLL_CFG0_WIDTH					0x10
#define QPLL_CFG0_DEFAULT				0x0

#define QPLL_CFG1_ADDR					0x33
#define QPLL_CFG1_MASK					0x7ff
#define QPLL_CFG1_OFFSET				0x0
#define QPLL_CFG1_WIDTH					0xb
#define QPLL_CFG1_DEFAULT				0x0

#define QPLL_REFCLK_DIV_M_ADDR				0x33
#define QPLL_REFCLK_DIV_M_MASK				0xf800
#define QPLL_REFCLK_DIV_M_OFFSET			0xb
#define QPLL_REFCLK_DIV_M_WIDTH				0x5
#define QPLL_REFCLK_DIV_M_DEFAULT			0x0

#define QPLL_FBDIV_N_ADDR				0x36
#define QPLL_FBDIV_N_MASK				0x3ff
#define QPLL_FBDIV_N_OFFSET				0x0
#define QPLL_FBDIV_N_WIDTH				0xa
#define QPLL_FBDIV_N_DEFAULT				0x0

#define QPLL_FBDIV_RATIO_ADDR				0x37
#define QPLL_FBDIV_RATIO_MASK				0x40
#define QPLL_FBDIV_RATIO_OFFSET				0x6
#define QPLL_FBDIV_RATIO_WIDTH				0x1
#define QPLL_FBDIV_RATIO_DEFAULT			0x0

#define CPLL_CFG0_ADDR					0x5c
#define CPLL_CFG0_MASK					0xff00
#define CPLL_CFG0_OFFSET				0x8
#define CPLL_CFG0_WIDTH					0x8
#define CPLL_CFG0_DEFAULT				0x0

#define CPLL_CFG1_ADDR					0x5d
#define CPLL_CFG1_MASK					0xffff
#define CPLL_CFG1_OFFSET				0x0
#define CPLL_CFG1_WIDTH					0x10
#define CPLL_CFG1_DEFAULT				0x0

#define CPLL_REFCLK_DIV_M_ADDR				0x5e
#define CPLL_REFCLK_DIV_M_MASK				0x1f00
#define CPLL_REFCLK_DIV_M_OFFSET			0x8
#define CPLL_REFCLK_DIV_M_WIDTH				0x5
#define CPLL_REFCLK_DIV_M_DEFAULT			0x0

#define CPLL_FB_DIV_45_N1_ADDR				0x5e
#define CPLL_FB_DIV_45_N1_MASK				0x80
#define CPLL_FB_DIV_45_N1_OFFSET			0x7
#define CPLL_FB_DIV_45_N1_WIDTH				0x1
#define CPLL_FB_DIV_45_N1_DEFAULT			0x0

#define CPLL_FBDIV_N2_ADDR				0x5e
#define CPLL_FBDIV_N2_MASK				0x7f
#define CPLL_FBDIV_N2_OFFSET				0x0
#define CPLL_FBDIV_N2_WIDTH				0x7
#define CPLL_FBDIV_N2_DEFAULT				0x0

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

#ifndef GTX7S_QPLL_BANDS_H_
#define GTX7S_QPLL_BANDS_H_

#define GTX7S_QPLL_NUM_CHANNEL_DRP_REGS 10
#define GTX7S_QPLL_NUM_LINE_RATE_BANDS 10

u32 get_gtx7s_qpll_address_lut(u32);
u32 get_gtx7s_qpll_offset_lut(u32);
u32 get_gtx7s_qpll_mask_lut(u32);
u16 get_gtx7s_qpll_param_lut(u32, u32);

#endif /* GTX7S_QPLL_BANDS_H_ */

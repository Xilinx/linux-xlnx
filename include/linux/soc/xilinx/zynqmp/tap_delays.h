/*
 * Xilinx Zynq MPSoC Power Management
 *
 *  Copyright (C) 2016 - 2018, Xilinx, Inc.
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

#include <linux/firmware/xilinx/zynqmp/firmware.h>

#ifdef CONFIG_ARCH_ZYNQMP
/* API for programming the tap delays */
void arasan_zynqmp_set_tap_delay(u8 deviceid, u8 timing, u8 bank);

/* API to reset the DLL */
void zynqmp_dll_reset(u8 deviceid);
#else
inline void arasan_zynqmp_set_tap_delay(u8 deviceid, u8 timing, u8 bank) {}
inline void zynqmp_dll_reset(u8 deviceid) {}
#endif

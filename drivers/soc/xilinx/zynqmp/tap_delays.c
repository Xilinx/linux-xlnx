/*
 * Xilinx Zynq MPSoC Tap Delay Programming
 *
 *  Copyright (C) 2016 Xilinx, Inc.
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

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/soc/xilinx/zynqmp/tap_delays.h>

/**
 * arasan_zynqmp_set_tap_delay - Program the tap delays.
 * @deviceid:		Unique Id of device
 * @itap_delay:		Input Tap Delay
 * @oitap_delay:	Output Tap Delay
 */
void arasan_zynqmp_set_tap_delay(u8 deviceid, u8 itap_delay, u8 otap_delay)
{
	u32 node_id = (deviceid == 0) ? NODE_SD_0 : NODE_SD_1;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops->ioctl)
		return;

	/* Set the Input Tap Delay */
	if (itap_delay)
		eemi_ops->ioctl(node_id, IOCTL_SET_SD_TAPDELAY,
				PM_TAPDELAY_INPUT, itap_delay, NULL);

	/* Set the Output Tap Delay */
	if (otap_delay)
		eemi_ops->ioctl(node_id, IOCTL_SET_SD_TAPDELAY,
				PM_TAPDELAY_OUTPUT, otap_delay, NULL);
}
EXPORT_SYMBOL_GPL(arasan_zynqmp_set_tap_delay);

/**
 * arasan_zynqmp_dll_reset - Issue the DLL reset.
 * @deviceid:		Unique Id of device
 */
void zynqmp_dll_reset(u8 deviceid)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops->ioctl)
		return;

	/* Issue DLL Reset */
	if (deviceid == 0)
		eemi_ops->ioctl(NODE_SD_0, IOCTL_SD_DLL_RESET,
			       PM_DLL_RESET_PULSE, 0, NULL);
	else
		eemi_ops->ioctl(NODE_SD_1, IOCTL_SD_DLL_RESET,
			       PM_DLL_RESET_PULSE, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_dll_reset);

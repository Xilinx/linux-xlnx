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

#define SD0_ITAPDLYSEL_HSD		0x15
#define SD0_ITAPDLYSEL_SD_DDR50		0x3D
#define SD0_ITAPDLYSEL_MMC_DDR50	0x12
#define SD1_ITAPDLYSEL_HSD		0x15
#define SD1_ITAPDLYSEL_SD_DDR50		0x3D
#define SD1_ITAPDLYSEL_MMC_DDR50	0x12

#define SD0_OTAPDLYSEL_MMC_HSD		0x06
#define SD0_OTAPDLYSEL_SD_HSD		0x05
#define SD0_OTAPDLYSEL_SDR50		0x03
#define SD0_OTAPDLYSEL_SDR104_B0	0x03
#define SD0_OTAPDLYSEL_SDR104_B2	0x02
#define SD0_OTAPDLYSEL_SD_DDR50		0x04
#define SD0_OTAPDLYSEL_MMC_DDR50	0x06
#define SD1_OTAPDLYSEL_MMC_HSD		0x06
#define SD1_OTAPDLYSEL_SD_HSD		0x05
#define SD1_OTAPDLYSEL_SDR50		0x03
#define SD1_OTAPDLYSEL_SDR104_B0	0x03
#define SD1_OTAPDLYSEL_SDR104_B2	0x02
#define SD1_OTAPDLYSEL_SD_DDR50		0x04
#define SD1_OTAPDLYSEL_MMC_DDR50	0x06

#define MMC_BANK2		0x2

#define MMC_TIMING_MMC_HS       1
#define MMC_TIMING_SD_HS        2
#define MMC_TIMING_UHS_SDR25    4
#define MMC_TIMING_UHS_SDR50    5
#define MMC_TIMING_UHS_SDR104   6
#define MMC_TIMING_UHS_DDR50    7
#define MMC_TIMING_MMC_DDR52    8
#define MMC_TIMING_MMC_HS200    9

/**
 * arasan_zynqmp_tap_sdr104 - Program the tap delys for HS and SDR25 modes.
 * @deviceid:		Unique Id of device
 * @timing:		timing specification used
 * @bank:		MIO bank for SDIO
 */
static void arasan_zynqmp_tap_hs(u8 deviceid, u8 timing, u8 bank)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->ioctl)
		return;

	if (deviceid == 0) {
		eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
			       PM_TAPDELAY_INPUT, SD0_ITAPDLYSEL_HSD, NULL);
		if (timing == MMC_TIMING_MMC_HS)
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD0_OTAPDLYSEL_MMC_HSD,
				       NULL);
		else
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD0_OTAPDLYSEL_SD_HSD,
				       NULL);
	} else {
		eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
			       PM_TAPDELAY_INPUT, SD1_ITAPDLYSEL_HSD, NULL);
		if (timing == MMC_TIMING_MMC_HS)
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD1_OTAPDLYSEL_MMC_HSD,
				       NULL);
		else
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD1_OTAPDLYSEL_SD_HSD,
				       NULL);
	}
}

/**
 * arasan_zynqmp_tap_ddr50 - Program the tap delys for DDR50 and DDR52 modes.
 * @deviceid:		Unique Id of device
 * @timing:		timing specification used
 * @bank:		MIO bank for SDIO
 */
static void arasan_zynqmp_tap_ddr50(u8 deviceid, u8 timing, u8 bank)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->ioctl)
		return;

	if (deviceid == 0) {
		if (timing == MMC_TIMING_UHS_DDR50) {
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_INPUT,
				       SD0_ITAPDLYSEL_SD_DDR50,
				       NULL);
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD0_OTAPDLYSEL_SD_DDR50,
				       NULL);
		} else {
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_INPUT,
				       SD0_ITAPDLYSEL_MMC_DDR50,
				       NULL);
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD0_OTAPDLYSEL_MMC_DDR50,
				       NULL);
		}
	} else {
		if (timing == MMC_TIMING_UHS_DDR50) {
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_INPUT,
				       SD1_ITAPDLYSEL_SD_DDR50,
				       NULL);
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD1_OTAPDLYSEL_SD_DDR50,
				       NULL);
		} else {
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_INPUT,
				       SD1_ITAPDLYSEL_MMC_DDR50,
				       NULL);
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD1_OTAPDLYSEL_MMC_DDR50,
				       NULL);
		}
	}
}

/**
 * arasan_zynqmp_tap_sdr50 - Program the tap delys for SDR50 mode.
 * @deviceid:		Unique Id of device
 * @timing:		timing specification used
 * @bank:		MIO bank for SDIO
 */
static void arasan_zynqmp_tap_sdr50(u8 deviceid, u8 timing, u8 bank)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->ioctl)
		return;

	if (deviceid == 0) {
		eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
			       PM_TAPDELAY_OUTPUT,
			       SD0_OTAPDLYSEL_SDR50,
			       NULL);
	} else {
		/* Program OTAP */
		eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
			       PM_TAPDELAY_OUTPUT,
			       SD1_OTAPDLYSEL_SDR50,
			       NULL);
	}
}

/**
 * arasan_zynqmp_tap_sdr104 - Program the tap delys for SDR104 and HS200 modes.
 * @deviceid:		Unique Id of device
 * @timing:		timing specification used
 * @bank:		MIO bank for SDIO
 */
static void arasan_zynqmp_tap_sdr104(u8 deviceid, u8 timing, u8 bank)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->ioctl)
		return;

	if (deviceid == 0) {
		/* Program OTAP */
		if (bank == MMC_BANK2)
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD0_OTAPDLYSEL_SDR104_B2,
				       NULL);
		else
			eemi_ops->ioctl(NODE_SD_0, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD0_OTAPDLYSEL_SDR104_B0,
				       NULL);
	} else {
		/* Program OTAP */
		if (bank == MMC_BANK2)
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD1_OTAPDLYSEL_SDR104_B2,
				       NULL);
		else
			eemi_ops->ioctl(NODE_SD_1, IOCTL_SET_SD_TAPDELAY,
				       PM_TAPDELAY_OUTPUT,
				       SD1_OTAPDLYSEL_SDR104_B0,
				       NULL);
	}
}

/**
 * arasan_zynqmp_set_tap_delay - Program the tap delys based on the mmc timing.
 * @deviceid:		Unique Id of device
 * @timing:		Timing specification used
 * @bank:		MIO bank for SDIO
 */
void arasan_zynqmp_set_tap_delay(u8 deviceid, u8 timing, u8 bank)
{
	switch (timing) {
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_UHS_SDR25:
		arasan_zynqmp_tap_hs(deviceid, timing, bank);
		break;
	case MMC_TIMING_UHS_SDR50:
		arasan_zynqmp_tap_sdr50(deviceid, timing, bank);
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		arasan_zynqmp_tap_sdr104(deviceid, timing, bank);
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		arasan_zynqmp_tap_ddr50(deviceid, timing, bank);
		break;
	}
}
EXPORT_SYMBOL_GPL(arasan_zynqmp_set_tap_delay);

/**
 * arasan_zynqmp_dll_reset - Issue the DLL reset.
 * @deviceid:		Unique Id of device
 */
void zynqmp_dll_reset(u8 deviceid)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->ioctl)
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

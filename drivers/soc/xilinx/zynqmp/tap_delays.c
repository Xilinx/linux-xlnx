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

#define SD_DLL_CTRL			0xFF180358
#define SD_ITAP_DLY			0xFF180314
#define SD_OTAP_DLY			0xFF180318
#define SD0_DLL_RST_MASK		0x00000004
#define SD0_DLL_RST			0x00000004
#define SD1_DLL_RST_MASK		0x00040000
#define SD1_DLL_RST			0x00040000
#define SD0_ITAPCHGWIN_MASK		0x00000200
#define SD0_ITAPCHGWIN			0x00000200
#define SD1_ITAPCHGWIN_MASK		0x02000000
#define SD1_ITAPCHGWIN			0x02000000
#define SD0_ITAPDLYENA_MASK		0x00000100
#define SD0_ITAPDLYENA			0x00000100
#define SD1_ITAPDLYENA_MASK		0x01000000
#define SD1_ITAPDLYENA			0x01000000
#define SD0_ITAPDLYSEL_MASK		0x000000FF
#define SD0_ITAPDLYSEL_HSD		0x00000015
#define SD0_ITAPDLYSEL_SD_DDR50		0x0000003D
#define SD0_ITAPDLYSEL_MMC_DDR50	0x00000012

#define SD1_ITAPDLYSEL_MASK		0x00FF0000
#define SD1_ITAPDLYSEL_HSD		0x00150000
#define SD1_ITAPDLYSEL_SD_DDR50		0x003D0000
#define SD1_ITAPDLYSEL_MMC_DDR50	0x00120000

#define SD0_OTAPDLYENA_MASK		0x00000040
#define SD0_OTAPDLYENA			0x00000040
#define SD1_OTAPDLYENA_MASK		0x00400000
#define SD1_OTAPDLYENA			0x00400000
#define SD0_OTAPDLYSEL_MASK		0x0000003F
#define SD0_OTAPDLYSEL_MMC_HSD		0x00000006
#define SD0_OTAPDLYSEL_SD_HSD		0x00000005
#define SD0_OTAPDLYSEL_SDR50		0x00000003
#define SD0_OTAPDLYSEL_SDR104_B0	0x00000003
#define SD0_OTAPDLYSEL_SDR104_B2	0x00000002
#define SD0_OTAPDLYSEL_SD_DDR50		0x00000004
#define SD0_OTAPDLYSEL_MMC_DDR50	0x00000006

#define SD1_OTAPDLYSEL_MASK		0x003F0000
#define SD1_OTAPDLYSEL_MMC_HSD		0x00060000
#define SD1_OTAPDLYSEL_SD_HSD		0x00050000
#define SD1_OTAPDLYSEL_SDR50		0x00030000
#define SD1_OTAPDLYSEL_SDR104_B0	0x00030000
#define SD1_OTAPDLYSEL_SDR104_B2	0x00020000
#define SD1_OTAPDLYSEL_SD_DDR50		0x00040000
#define SD1_OTAPDLYSEL_MMC_DDR50	0x00060000

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
	if (deviceid == 0) {
		/* Program ITAP */
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPCHGWIN_MASK,
				     SD0_ITAPCHGWIN);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPDLYENA_MASK,
				     SD0_ITAPDLYENA);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPDLYSEL_MASK,
				     SD0_ITAPDLYSEL_HSD);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPCHGWIN_MASK, 0x0);
		/* Program OTAP */
		if (timing == MMC_TIMING_MMC_HS)
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD0_OTAPDLYSEL_MASK,
					     SD0_OTAPDLYSEL_MMC_HSD);
		else
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD0_OTAPDLYSEL_MASK,
					     SD0_OTAPDLYSEL_SD_HSD);
	} else {
		/* Program ITAP */
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPCHGWIN_MASK,
				     SD1_ITAPCHGWIN);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPDLYENA_MASK,
				     SD1_ITAPDLYENA);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPDLYSEL_MASK,
				     SD1_ITAPDLYSEL_HSD);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPCHGWIN_MASK, 0x0);
		/* Program OTAP */
		if (timing == MMC_TIMING_MMC_HS)
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD1_OTAPDLYSEL_MASK,
					     SD1_OTAPDLYSEL_MMC_HSD);
		else
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD1_OTAPDLYSEL_MASK,
					     SD1_OTAPDLYSEL_SD_HSD);
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
	if (deviceid == 0) {
		/* Program ITAP */
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPCHGWIN_MASK,
				     SD0_ITAPCHGWIN);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPDLYENA_MASK,
				     SD0_ITAPDLYENA);
		if (timing == MMC_TIMING_UHS_DDR50)
			zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPDLYSEL_MASK,
					     SD0_ITAPDLYSEL_SD_DDR50);
		else
			zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPDLYSEL_MASK,
					     SD0_ITAPDLYSEL_MMC_DDR50);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD0_ITAPCHGWIN_MASK, 0x0);
		/* Program OTAP */
		if (timing == MMC_TIMING_UHS_DDR50)
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD0_OTAPDLYSEL_MASK,
					     SD0_OTAPDLYSEL_SD_DDR50);
		else
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD0_OTAPDLYSEL_MASK,
					     SD0_OTAPDLYSEL_MMC_DDR50);
	} else {
		/* Program ITAP */
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPCHGWIN_MASK,
				     SD1_ITAPCHGWIN);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPDLYENA_MASK,
				     SD1_ITAPDLYENA);
		if (timing == MMC_TIMING_UHS_DDR50)
			zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPDLYSEL_MASK,
					     SD1_ITAPDLYSEL_SD_DDR50);
		else
			zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPDLYSEL_MASK,
					     SD1_ITAPDLYSEL_MMC_DDR50);
		zynqmp_pm_mmio_write(SD_ITAP_DLY, SD1_ITAPCHGWIN_MASK, 0x0);
		/* Program OTAP */
		if (timing == MMC_TIMING_UHS_DDR50)
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD1_OTAPDLYSEL_MASK,
					     SD1_OTAPDLYSEL_SD_DDR50);
		else
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD1_OTAPDLYSEL_MASK,
					     SD1_OTAPDLYSEL_MMC_DDR50);
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
	if (deviceid == 0) {
		/* Program OTAP */
		zynqmp_pm_mmio_write(SD_OTAP_DLY, SD0_OTAPDLYSEL_MASK,
				     SD0_OTAPDLYSEL_SDR50);
	} else {
		/* Program OTAP */
		zynqmp_pm_mmio_write(SD_OTAP_DLY, SD1_OTAPDLYSEL_MASK,
				     SD1_OTAPDLYSEL_SDR50);
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
	if (deviceid == 0) {
		/* Program OTAP */
		if (bank == MMC_BANK2)
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD0_OTAPDLYSEL_MASK,
					     SD0_OTAPDLYSEL_SDR104_B2);
		else
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD0_OTAPDLYSEL_MASK,
					     SD0_OTAPDLYSEL_SDR104_B0);
	} else {
		/* Program OTAP */
		if (bank == MMC_BANK2)
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD1_OTAPDLYSEL_MASK,
					     SD1_OTAPDLYSEL_SDR104_B2);
		else
			zynqmp_pm_mmio_write(SD_OTAP_DLY, SD1_OTAPDLYSEL_MASK,
					     SD1_OTAPDLYSEL_SDR104_B0);
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
	if (deviceid == 0)
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD0_DLL_RST_MASK,
				     SD0_DLL_RST);
	else
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD1_DLL_RST_MASK,
				     SD1_DLL_RST);

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

	if (deviceid == 0)
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD0_DLL_RST_MASK, 0x0);
	else
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD1_DLL_RST_MASK, 0x0);

}
EXPORT_SYMBOL_GPL(arasan_zynqmp_set_tap_delay);

/**
 * arasan_zynqmp_dll_reset - Issue the DLL reset.
 * @deviceid:		Unique Id of device
 */
void zynqmp_dll_reset(u8 deviceid)
{
	/* Issue DLL Reset */
	if (deviceid == 0)
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD0_DLL_RST_MASK,
				     SD0_DLL_RST);
	else
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD1_DLL_RST_MASK,
				     SD1_DLL_RST);

	mdelay(1);

	/* Release DLL Reset */
	if (deviceid == 0)
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD0_DLL_RST_MASK, 0x0);
	else
		zynqmp_pm_mmio_write(SD_DLL_CTRL, SD1_DLL_RST_MASK, 0x0);
}
EXPORT_SYMBOL_GPL(zynqmp_dll_reset);

/*
 * Xilinx SDI Rx Subsystem
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * Contacts: Vishal Sagar <vsagar@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <dt-bindings/media/xilinx-vip.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/v4l2-subdev.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>
#include "xilinx-vip.h"

/*
 * SDI Rx register map, bitmask and offsets
 */
#define XSDIRX_MDL_CTRL_REG		0x00
#define XSDIRX_STAT_RESET_REG		0x04
#define XSDIRX_INTR_STAT_REG		0x08
#define XSDIRX_INTR_MASK_REG		0x0C
#define XSDIRX_INTR_CLEAR_REG		0x10
#define XSDIRX_MODE_DET_STAT_REG	0x14
#define XSDIRX_TS_DET_STAT_REG		0x18
#define XSDIRX_EDH_STAT_REG		0x1C
#define XSDIRX_EDH_ERRCNT_REG		0x20
#define XSDIRX_CRC_ERRCNT_REG		0x24
#define XSDIRX_ST352_VALID_REG		0x28
#define XSDIRX_ST352_DS0_REG		0x2C
#define XSDIRX_ST352_DS1_REG		0x30
#define XSDIRX_ST352_DS2_REG		0x34
#define XSDIRX_ST352_DS3_REG		0x38
#define XSDIRX_ST352_DS4_REG		0x3C
#define XSDIRX_ST352_DS5_REG		0x40
#define XSDIRX_ST352_DS6_REG		0x44
#define XSDIRX_ST352_DS7_REG		0x48
#define XSDIRX_VERSION_REG		0x4C
#define XSDIRX_SYSCONFIG_REG		0x50
#define XSDIRX_EDH_ERRCNT_EN_REG	0x54
#define XSDIRX_STAT_SB_RX_TDATA_REG	0x58
#define XSDIRX_VID_LOCK_WINDOW_REG	0x5C
#define XSDIRX_BRIDGE_CTRL_REG		0x60
#define XSDIRX_BRIDGE_STAT_REG		0x64
#define XSDIRX_VID_IN_AXIS4_CTRL_REG	0x68
#define XSDIRX_VID_IN_AXIS4_STAT_REG	0x6C

#define XSDIRX_MDL_CTRL_MDL_EN_MASK	BIT(0)
#define XSDIRX_MDL_CTRL_FRM_EN_MASK	BIT(4)

#define XSDIRX_MDL_CTRL_MODE_DET_EN_MASK	BIT(5)
#define XSDIRX_MDL_CTRL_MODE_HD_EN_MASK		BIT(8)
#define XSDIRX_MDL_CTRL_MODE_SD_EN_MASK		BIT(9)
#define XSDIRX_MDL_CTRL_MODE_3G_EN_MASK		BIT(10)
#define XSDIRX_MDL_CTRL_MODE_6G_EN_MASK		BIT(11)
#define XSDIRX_MDL_CTRL_MODE_12GI_EN_MASK	BIT(12)
#define XSDIRX_MDL_CTRL_MODE_12GF_EN_MASK	BIT(13)
#define XSDIRX_MDL_CTRL_MODE_AUTO_DET_MASK	GENMASK(13, 8)

#define XSDIRX_MDL_CTRL_FORCED_MODE_OFFSET	16
#define XSDIRX_MDL_CTRL_FORCED_MODE_MASK	GENMASK(18, 16)

#define XSDIRX_STAT_RESET_CRC_ERRCNT_MASK	BIT(0)
#define XSDIRX_STAT_RESET_EDH_ERRCNT_MASK	BIT(1)

#define XSDIRX_INTR_VIDLOCK_MASK	BIT(0)
#define XSDIRX_INTR_VIDUNLOCK_MASK	BIT(1)
#define XSDIRX_INTR_ALL_MASK	(XSDIRX_INTR_VIDLOCK_MASK |\
				XSDIRX_INTR_VIDUNLOCK_MASK)

#define XSDIRX_MODE_DET_STAT_RX_MODE_MASK	GENMASK(2, 0)
#define XSDIRX_MODE_DET_STAT_MODE_LOCK_MASK	BIT(3)
#define XSDIRX_MODE_DET_STAT_ACT_STREAM_MASK	GENMASK(6, 4)
#define XSDIRX_MODE_DET_STAT_LVLB_3G_MASK	BIT(7)

#define XSDIRX_ACTIVE_STREAMS_1		0x0
#define XSDIRX_ACTIVE_STREAMS_2		0x1
#define XSDIRX_ACTIVE_STREAMS_4		0x2
#define XSDIRX_ACTIVE_STREAMS_8		0x3
#define XSDIRX_ACTIVE_STREAMS_16	0x4

#define XSDIRX_TS_DET_STAT_LOCKED_MASK		BIT(0)
#define XSDIRX_TS_DET_STAT_SCAN_MASK		BIT(1)
#define XSDIRX_TS_DET_STAT_FAMILY_MASK		GENMASK(7, 4)
#define XSDIRX_TS_DET_STAT_FAMILY_OFFSET	(4)
#define XSDIRX_TS_DET_STAT_RATE_MASK		GENMASK(11, 8)
#define XSDIRX_TS_DET_STAT_RATE_OFFSET		(8)

#define XSDIRX_EDH_STAT_EDH_AP_MASK	BIT(0)
#define XSDIRX_EDH_STAT_EDH_FF_MASK	BIT(1)
#define XSDIRX_EDH_STAT_EDH_ANC_MASK	BIT(2)
#define XSDIRX_EDH_STAT_AP_FLAG_MASK	GENMASK(8, 4)
#define XSDIRX_EDH_STAT_FF_FLAG_MASK	GENMASK(13, 9)
#define XSDIRX_EDH_STAT_ANC_FLAG_MASK	GENMASK(18, 14)
#define XSDIRX_EDH_STAT_PKT_FLAG_MASK	GENMASK(22, 19)

#define XSDIRX_EDH_ERRCNT_COUNT_MASK	GENMASK(15, 0)

#define XSDIRX_CRC_ERRCNT_COUNT_MASK	GENMASK(15, 0)
#define XSDIRX_CRC_ERRCNT_DS_CRC_MASK	GENMASK(31, 16)

#define XSDIRX_VERSION_REV_MASK		GENMASK(7, 0)
#define XSDIRX_VERSION_PATCHID_MASK	GENMASK(11, 8)
#define XSDIRX_VERSION_VER_REV_MASK	GENMASK(15, 12)
#define XSDIRX_VERSION_VER_MIN_MASK	GENMASK(23, 16)
#define XSDIRX_VERSION_VER_MAJ_MASK	GENMASK(31, 24)

#define XSDIRX_SYSCONFIG_EDH_INCLUDED_MASK	BIT(1)

#define XSDIRX_STAT_SB_RX_TDATA_CHANGE_DONE_MASK	BIT(0)
#define XSDIRX_STAT_SB_RX_TDATA_CHANGE_FAIL_MASK	BIT(1)
#define XSDIRX_STAT_SB_RX_TDATA_GT_RESETDONE		BIT(2)
#define XSDIRX_STAT_SB_RX_TDATA_GT_BITRATE		BIT(3)

#define XSDIRX_VID_LOCK_WINDOW_VAL_MASK		GENMASK(15, 0)

#define XSDIRX_BRIDGE_CTRL_MDL_ENB_MASK		BIT(0)

#define XSDIRX_BRIDGE_STAT_SEL_MASK		BIT(0)
#define XSDIRX_BRIDGE_STAT_MODE_LOCKED_MASK	BIT(1)
#define XSDIRX_BRIDGE_STAT_MODE_MASK		GENMASK(6, 4)
#define XSDIRX_BRIDGE_STAT_LVLB_MASK		BIT(7)

#define XSDIRX_VID_IN_AXIS4_CTRL_MDL_ENB_MASK	BIT(0)
#define XSDIRX_VID_IN_AXIS4_CTRL_AXIS_ENB_MASK	BIT(1)
#define XSDIRX_VID_IN_AXIS4_CTRL_ALL_MASK	GENMASK(1, 0)

#define XSDIRX_VID_IN_AXIS4_STAT_OVERFLOW_MASK	BIT(0)
#define XSDIRX_VID_IN_AXIS4_STAT_UNDERFLOW_MASK	BIT(1)

/* Number of media pads */
#define XSDIRX_MEDIA_PADS	(1)

#define XSDIRX_DEFAULT_WIDTH	(1920)
#define XSDIRX_DEFAULT_HEIGHT	(1080)

#define XSDIRX_MAX_STR_LENGTH	16

#define XSDIRXSS_SDI_STD_3G		0
#define XSDIRXSS_SDI_STD_6G		1
#define XSDIRXSS_SDI_STD_12G_8DS	2

#define XSDIRX_DEFAULT_VIDEO_LOCK_WINDOW	0x3000
#define XSDIRX_DEFAULT_EDH_ERRCNT		0x420

#define XSDIRX_MODE_HD_MASK	0x0
#define XSDIRX_MODE_SD_MASK	0x1
#define XSDIRX_MODE_3G_MASK	0x2
#define XSDIRX_MODE_6G_MASK	0x4
#define XSDIRX_MODE_12GI_MASK	0x5
#define XSDIRX_MODE_12GF_MASK	0x6

enum {
	XSDIRX_MODE_SD_OFFSET = 0,
	XSDIRX_MODE_HD_OFFSET,
	XSDIRX_MODE_3G_OFFSET,
	XSDIRX_MODE_6G_OFFSET,
	XSDIRX_MODE_12GI_OFFSET,
	XSDIRX_MODE_12GF_OFFSET,
	XSDIRX_MODE_NUM_SUPPORTED,
};

#define XSDIRX_DETECT_ALL_MODES		((1 << XSDIRX_MODE_SD_OFFSET) | \
					(1 << XSDIRX_MODE_HD_OFFSET) | \
					(1 << XSDIRX_MODE_3G_OFFSET) | \
					(1 << XSDIRX_MODE_6G_OFFSET) | \
					(1 << XSDIRX_MODE_12GI_OFFSET) | \
					(1 << XSDIRX_MODE_12GF_OFFSET)) \

/**
 * struct xsdirxss_core - Core configuration SDI Rx Subsystem device structure
 * @dev: Platform structure
 * @iomem: Base address of subsystem
 * @irq: requested irq number
 * @include_edh: EDH processor presence
 * @mode: 3G/6G/12G mode
 */
struct xsdirxss_core {
	struct device *dev;
	void __iomem *iomem;
	int irq;
	bool include_edh;
	int mode;
};

/**
 * struct xsdirxss_state - SDI Rx Subsystem device structure
 * @core: Core structure for MIPI SDI Rx Subsystem
 * @subdev: The v4l2 subdev structure
 * @formats: Active V4L2 formats on each pad
 * @default_format: default V4L2 media bus format
 * @vip_format: format information corresponding to the active format
 * @pads: media pads
 * @streaming: Flag for storing streaming state
 * @vidlocked: Flag indicating SDI Rx has locked onto video stream
 *
 * This structure contains the device driver related parameters
 */
struct xsdirxss_state {
	struct xsdirxss_core core;
	struct v4l2_subdev subdev;
	struct v4l2_mbus_framefmt formats[XSDIRX_MEDIA_PADS];
	struct v4l2_mbus_framefmt default_format;
	const struct xvip_video_format *vip_format;
	struct media_pad pads[XSDIRX_MEDIA_PADS];
	bool streaming;
	bool vidlocked;
};

static inline struct xsdirxss_state *
to_xsdirxssstate(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xsdirxss_state, subdev);
}

/*
 * Register related operations
 */
static inline u32 xsdirxss_read(struct xsdirxss_core *xsdirxss, u32 addr)
{
	return ioread32(xsdirxss->iomem + addr);
}

static inline void xsdirxss_write(struct xsdirxss_core *xsdirxss, u32 addr,
				  u32 value)
{
	iowrite32(value, xsdirxss->iomem + addr);
}

static inline void xsdirxss_clr(struct xsdirxss_core *xsdirxss, u32 addr,
				u32 clr)
{
	xsdirxss_write(xsdirxss, addr, xsdirxss_read(xsdirxss, addr) & ~clr);
}

static inline void xsdirxss_set(struct xsdirxss_core *xsdirxss, u32 addr,
				u32 set)
{
	xsdirxss_write(xsdirxss, addr, xsdirxss_read(xsdirxss, addr) | set);
}

static void xsdirx_core_disable(struct xsdirxss_core *core)
{
	xsdirxss_write(core, XSDIRX_MDL_CTRL_REG, 0);
}

static void xsdirx_core_enable(struct xsdirxss_core *core)
{
	xsdirxss_set(core, XSDIRX_MDL_CTRL_REG, XSDIRX_MDL_CTRL_MDL_EN_MASK);
}

static int xsdirx_set_modedetect(struct xsdirxss_core *core, u16 mask)
{
	u32 i, val;

	mask = mask & XSDIRX_DETECT_ALL_MODES;
	if (!mask) {
		dev_err(core->dev, "Invalid bit mask = 0x%08x\n", mask);
		return -EINVAL;
	}

	val = xsdirxss_read(core, XSDIRX_MDL_CTRL_REG);
	val &= ~(XSDIRX_MDL_CTRL_MODE_DET_EN_MASK);
	val &= ~(XSDIRX_MDL_CTRL_MODE_AUTO_DET_MASK);
	val &= ~(XSDIRX_MDL_CTRL_FORCED_MODE_MASK);

	if (hweight16(mask) > 1) {
		/* Multi mode detection as more than 1 bit set in mask */
		dev_dbg(core->dev, "Detect multiple modes\n");
		for (i = 0; i < XSDIRX_MODE_NUM_SUPPORTED; i++) {
			switch (mask & (1 << i)) {
			case BIT(XSDIRX_MODE_SD_OFFSET):
				val |= XSDIRX_MDL_CTRL_MODE_SD_EN_MASK;
				break;
			case BIT(XSDIRX_MODE_HD_OFFSET):
				val |= XSDIRX_MDL_CTRL_MODE_HD_EN_MASK;
				break;
			case BIT(XSDIRX_MODE_3G_OFFSET):
				val |= XSDIRX_MDL_CTRL_MODE_3G_EN_MASK;
				break;
			case BIT(XSDIRX_MODE_6G_OFFSET):
				val |= XSDIRX_MDL_CTRL_MODE_6G_EN_MASK;
				break;
			case BIT(XSDIRX_MODE_12GI_OFFSET):
				val |= XSDIRX_MDL_CTRL_MODE_12GI_EN_MASK;
				break;
			case BIT(XSDIRX_MODE_12GF_OFFSET):
				val |= XSDIRX_MDL_CTRL_MODE_12GF_EN_MASK;
				break;
			default:
				return -EINVAL;
			}
		}
		val |= XSDIRX_MDL_CTRL_MODE_DET_EN_MASK;
	} else {
		/* Fixed Mode */
		u32 forced_mode_mask;

		dev_dbg(core->dev, "Detect fixed mode\n");

		/* Find offset of first bit set */
		switch (__ffs(mask)) {
		case XSDIRX_MODE_SD_OFFSET:
			forced_mode_mask = XSDIRX_MODE_SD_MASK;
			break;
		case XSDIRX_MODE_HD_OFFSET:
			forced_mode_mask = XSDIRX_MODE_HD_MASK;
			break;
		case XSDIRX_MODE_3G_OFFSET:
			forced_mode_mask = XSDIRX_MODE_3G_MASK;
			break;
		case XSDIRX_MODE_6G_OFFSET:
			forced_mode_mask = XSDIRX_MODE_6G_MASK;
			break;
		case XSDIRX_MODE_12GI_OFFSET:
			forced_mode_mask = XSDIRX_MODE_12GI_MASK;
			break;
		case XSDIRX_MODE_12GF_OFFSET:
			forced_mode_mask = XSDIRX_MODE_12GF_MASK;
			break;
		default:
			return -EINVAL;
		}
		dev_dbg(core->dev, "Forced Mode Mask : 0x%x\n",
			forced_mode_mask);
		val |= forced_mode_mask << XSDIRX_MDL_CTRL_FORCED_MODE_OFFSET;
	}

	dev_dbg(core->dev, "Modes to be detected : sdi ctrl reg = 0x%08x\n",
		val);
	xsdirxss_write(core, XSDIRX_MDL_CTRL_REG, val);

	return 0;
}

static void xsdirx_framer(struct xsdirxss_core *core, bool flag)
{
	if (flag)
		xsdirxss_set(core, XSDIRX_MDL_CTRL_REG,
			     XSDIRX_MDL_CTRL_FRM_EN_MASK);
	else
		xsdirxss_clr(core, XSDIRX_MDL_CTRL_REG,
			     XSDIRX_MDL_CTRL_FRM_EN_MASK);
}

static void xsdirx_setedherrcnttrigger(struct xsdirxss_core *core, u32 enable)
{
	u32 val = xsdirxss_read(core, XSDIRX_EDH_ERRCNT_EN_REG);

	val |= enable & 0xFFFF;

	xsdirxss_write(core, XSDIRX_EDH_ERRCNT_EN_REG, val);
}

static void xsdirx_setvidlockwindow(struct xsdirxss_core *core, u32 val)
{
	/*
	 * The video lock window is the amount of time for which the
	 * the mode and transport stream should be locked to get the
	 * video lock interrupt.
	 */
	xsdirxss_write(core, XSDIRX_VID_LOCK_WINDOW_REG,
		       val & XSDIRX_VID_LOCK_WINDOW_VAL_MASK);
}

static void xsdirx_disableintr(struct xsdirxss_core *core, u8 mask)
{
	xsdirxss_set(core, XSDIRX_INTR_MASK_REG, mask);
}

static void xsdirx_enableintr(struct xsdirxss_core *core, u8 mask)
{
	xsdirxss_clr(core, XSDIRX_INTR_MASK_REG, mask);
}

static void xsdirx_clearintr(struct xsdirxss_core *core, u8 mask)
{
	xsdirxss_set(core, XSDIRX_INTR_CLEAR_REG, mask);
	xsdirxss_clr(core, XSDIRX_INTR_CLEAR_REG, mask);
}

static void xsdirx_vid_bridge_control(struct xsdirxss_core *core, bool enable)
{
	if (enable)
		xsdirxss_set(core, XSDIRX_BRIDGE_CTRL_REG,
			     XSDIRX_BRIDGE_CTRL_MDL_ENB_MASK);
	else
		xsdirxss_clr(core, XSDIRX_BRIDGE_CTRL_REG,
			     XSDIRX_BRIDGE_CTRL_MDL_ENB_MASK);
}

static void xsdirx_axis4_bridge_control(struct xsdirxss_core *core, bool enable)
{
	if (enable)
		xsdirxss_set(core, XSDIRX_VID_IN_AXIS4_CTRL_REG,
			     XSDIRX_VID_IN_AXIS4_CTRL_ALL_MASK);
	else
		xsdirxss_clr(core, XSDIRX_VID_IN_AXIS4_CTRL_REG,
			     XSDIRX_VID_IN_AXIS4_CTRL_ALL_MASK);
}

static void xsdirx_streamflow_control(struct xsdirxss_core *core, bool enable)
{
	/* The sdi to native bridge is followed by native to axis4 bridge */
	if (enable) {
		xsdirx_axis4_bridge_control(core, enable);
		xsdirx_vid_bridge_control(core, enable);
	} else {
		xsdirx_vid_bridge_control(core, enable);
		xsdirx_axis4_bridge_control(core, enable);
	}
}

static void xsdirx_streamdowncb(struct xsdirxss_core *core)
{
	xsdirx_core_disable(core);
	xsdirx_streamflow_control(core, false);
	xsdirx_framer(core, true);
	xsdirx_set_modedetect(core, XSDIRX_DETECT_ALL_MODES);
	xsdirx_core_enable(core);
}

/**
 * xsdirxss_irq_handler - Interrupt handler for SDI Rx
 * @irq: IRQ number
 * @dev_id: Pointer to device state
 *
 * The SDI Rx interrupts are cleared by first setting and then clearing the bits
 * in the interrupt clear register. The interrupt status register is read only.
 *
 * Return: IRQ_HANDLED after handling interrupts
 */
static irqreturn_t xsdirxss_irq_handler(int irq, void *dev_id)
{
	struct xsdirxss_state *state = (struct xsdirxss_state *)dev_id;
	struct xsdirxss_core *core = &state->core;
	u32 status;

	status = xsdirxss_read(core, XSDIRX_INTR_STAT_REG);
	dev_dbg(core->dev, "interrupt status = 0x%08x\n", status);

	if (!status)
		return IRQ_NONE;

	if (status & XSDIRX_INTR_VIDLOCK_MASK) {
		u32 val1, val2;

		dev_dbg(core->dev, "video lock interrupt\n");
		xsdirx_clearintr(core, XSDIRX_INTR_VIDLOCK_MASK);

		val1 = xsdirxss_read(core, XSDIRX_MODE_DET_STAT_REG);
		val2 = xsdirxss_read(core, XSDIRX_TS_DET_STAT_REG);

		if ((val1 & XSDIRX_MODE_DET_STAT_MODE_LOCK_MASK) &&
		    (val2 & XSDIRX_TS_DET_STAT_LOCKED_MASK)) {
			u32 mask = XSDIRX_STAT_RESET_CRC_ERRCNT_MASK |
				   XSDIRX_STAT_RESET_EDH_ERRCNT_MASK;

			dev_dbg(core->dev, "mode & ts lock occurred\n");

			xsdirxss_set(core, XSDIRX_STAT_RESET_REG, mask);
			xsdirxss_clr(core, XSDIRX_STAT_RESET_REG, mask);

			val1 = xsdirxss_read(core, XSDIRX_ST352_VALID_REG);
			val2 = xsdirxss_read(core, XSDIRX_ST352_DS0_REG);

			dev_dbg(core->dev, "valid st352 mask = 0x%08x\n", val1);
			dev_dbg(core->dev, "st352 payload = 0x%08x\n", val2);

			state->vidlocked = true;
		} else {
			dev_dbg(core->dev, "video unlock before video lock!\n");
			state->vidlocked = false;
		}
	}

	if (status & XSDIRX_INTR_VIDUNLOCK_MASK) {
		dev_dbg(core->dev, "video unlock interrupt\n");
		xsdirx_clearintr(core, XSDIRX_INTR_VIDUNLOCK_MASK);
		xsdirx_streamdowncb(core);
		state->vidlocked = false;
	}

	return IRQ_HANDLED;
}

/**
 * xsdirxss_log_status - Logs the status of the SDI Rx Subsystem
 * @sd: Pointer to V4L2 subdevice structure
 *
 * This function prints the current status of Xilinx SDI Rx Subsystem
 *
 * Return: 0 on success
 */
static int xsdirxss_log_status(struct v4l2_subdev *sd)
{
	struct xsdirxss_state *xsdirxss = to_xsdirxssstate(sd);
	struct xsdirxss_core *core = &xsdirxss->core;
	u32 data, i;

	v4l2_info(sd, "***** SDI Rx subsystem reg dump start *****\n");
	for (i = 0; i < 0x28; i++) {
		data = xsdirxss_read(core, i * 4);
		v4l2_info(sd, "offset 0x%08x data 0x%08x\n",
			  i * 4, data);
	}
	v4l2_info(sd, "***** SDI Rx subsystem reg dump end *****\n");
	return 0;
}

static void xsdirxss_start_stream(struct xsdirxss_state *xsdirxss)
{
	xsdirx_streamflow_control(&xsdirxss->core, true);
}

static void xsdirxss_stop_stream(struct xsdirxss_state *xsdirxss)
{
	xsdirx_streamflow_control(&xsdirxss->core, false);
}

/**
 * xsdirxss_s_stream - It is used to start/stop the streaming.
 * @sd: V4L2 Sub device
 * @enable: Flag (True / False)
 *
 * This function controls the start or stop of streaming for the
 * Xilinx SDI Rx Subsystem.
 *
 * Return: 0 on success, errors otherwise
 */
static int xsdirxss_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct xsdirxss_state *xsdirxss = to_xsdirxssstate(sd);
	struct xsdirxss_core *core = &xsdirxss->core;

	if (enable) {
		if (!xsdirxss->vidlocked) {
			dev_dbg(core->dev, "Video is not locked\n");
			return -EINVAL;
		}
		if (xsdirxss->streaming) {
			dev_dbg(core->dev, "Already streaming\n");
			return -EINVAL;
		}

		xsdirxss_start_stream(xsdirxss);
		xsdirxss->streaming = true;
		dev_dbg(core->dev, "Streaming started\n");
	} else {
		if (!xsdirxss->streaming) {
			dev_dbg(core->dev, "Stopped streaming already\n");
			return -EINVAL;
		}

		xsdirxss_stop_stream(xsdirxss);
		xsdirxss->streaming = false;
		dev_dbg(core->dev, "Streaming stopped\n");
	}

	return 0;
}

static struct v4l2_mbus_framefmt *
__xsdirxss_get_pad_format(struct xsdirxss_state *xsdirxss,
			  struct v4l2_subdev_pad_config *cfg,
				unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xsdirxss->subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xsdirxss->formats[pad];
	default:
		return NULL;
	}
}

/**
 * xsdirxss_get_format - Get the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to get the pad format information.
 *
 * Return: 0 on success
 */
static int xsdirxss_get_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
					struct v4l2_subdev_format *fmt)
{
	struct xsdirxss_state *xsdirxss = to_xsdirxssstate(sd);

	fmt->format = *__xsdirxss_get_pad_format(xsdirxss, cfg,
							fmt->pad, fmt->which);
	return 0;
}

/**
 * xsdirxss_set_format - This is used to set the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to set the pad format.
 * Since the pad format is fixed in hardware, it can't be
 * modified on run time.
 *
 * Return: 0 on success
 */
static int xsdirxss_set_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *__format;
	struct xsdirxss_state *xsdirxss = to_xsdirxssstate(sd);

	dev_dbg(xsdirxss->core.dev,
		"set width %d height %d code %d field %d colorspace %d\n",
		fmt->format.width, fmt->format.height,
		fmt->format.code, fmt->format.field,
		fmt->format.colorspace);

	__format = __xsdirxss_get_pad_format(xsdirxss, cfg,
					     fmt->pad, fmt->which);

	/* Currently reset the code to one fixed in hardware */
	/* TODO : Add checks for width height */
	fmt->format.code = __format->code;

	return 0;
}

/**
 * xsdirxss_open - Called on v4l2_open()
 * @sd: Pointer to V4L2 sub device structure
 * @fh: Pointer to V4L2 File handle
 *
 * This function is called on v4l2_open(). It sets the default format for pad.
 *
 * Return: 0 on success
 */
static int xsdirxss_open(struct v4l2_subdev *sd,
			 struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;
	struct xsdirxss_state *xsdirxss = to_xsdirxssstate(sd);

	format = v4l2_subdev_get_try_format(sd, fh->pad, 0);
	*format = xsdirxss->default_format;

	return 0;
}

/**
 * xsdirxss_close - Called on v4l2_close()
 * @sd: Pointer to V4L2 sub device structure
 * @fh: Pointer to V4L2 File handle
 *
 * This function is called on v4l2_close().
 *
 * Return: 0 on success
 */
static int xsdirxss_close(struct v4l2_subdev *sd,
			  struct v4l2_subdev_fh *fh)
{
	return 0;
}

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xsdirxss_media_ops = {
	.link_validate = v4l2_subdev_link_validate
};

static const struct v4l2_subdev_core_ops xsdirxss_core_ops = {
	.log_status = xsdirxss_log_status,
};

static struct v4l2_subdev_video_ops xsdirxss_video_ops = {
	.s_stream = xsdirxss_s_stream
};

static struct v4l2_subdev_pad_ops xsdirxss_pad_ops = {
	.get_fmt = xsdirxss_get_format,
	.set_fmt = xsdirxss_set_format,
};

static struct v4l2_subdev_ops xsdirxss_ops = {
	.core = &xsdirxss_core_ops,
	.video = &xsdirxss_video_ops,
	.pad = &xsdirxss_pad_ops
};

static const struct v4l2_subdev_internal_ops xsdirxss_internal_ops = {
	.open = xsdirxss_open,
	.close = xsdirxss_close
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xsdirxss_parse_of(struct xsdirxss_state *xsdirxss)
{
	struct device_node *node = xsdirxss->core.dev->of_node;
	struct device_node *ports = NULL;
	struct device_node *port = NULL;
	unsigned int nports = 0;
	struct xsdirxss_core *core = &xsdirxss->core;
	int ret;
	const char *sdi_std;

	core->include_edh = of_property_read_bool(node, "xlnx,include-edh");
	dev_dbg(core->dev, "EDH property = %s\n",
		core->include_edh ? "Present" : "Absent");

	ret = of_property_read_string(node, "xlnx,line-rate",
				      &sdi_std);
	if (ret < 0) {
		dev_err(core->dev, "xlnx,line-rate property not found\n");
		return ret;
	}

	if (!strncmp(sdi_std, "12G_SDI_8DS", XSDIRX_MAX_STR_LENGTH)) {
		core->mode = XSDIRXSS_SDI_STD_12G_8DS;
	} else if (!strncmp(sdi_std, "6G_SDI", XSDIRX_MAX_STR_LENGTH)) {
		core->mode = XSDIRXSS_SDI_STD_6G;
	} else if (!strncmp(sdi_std, "3G_SDI", XSDIRX_MAX_STR_LENGTH)) {
		core->mode = XSDIRXSS_SDI_STD_3G;
	} else {
		dev_err(core->dev, "Invalid Line Rate\n");
		return -EINVAL;
	}
	dev_dbg(core->dev, "SDI Rx Line Rate = %s, mode = %d\n", sdi_std,
		core->mode);

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;

	for_each_child_of_node(ports, port) {
		const struct xvip_video_format *format;
		struct device_node *endpoint;

		if (!port->name || of_node_cmp(port->name, "port"))
			continue;

		format = xvip_of_get_format(port);
		if (IS_ERR(format)) {
			dev_err(core->dev, "invalid format in DT");
			return PTR_ERR(format);
		}

		dev_dbg(core->dev, "vf_code = %d bpc = %d bpp = %d\n",
			format->vf_code, format->width, format->bpp);

		if (format->vf_code != XVIP_VF_YUV_422) {
			dev_err(core->dev, "Incorrect UG934 video format set. Accepts only YUV422\n");
			return -EINVAL;
		}
		xsdirxss->vip_format = format;

		endpoint = of_get_next_child(port, NULL);
		if (!endpoint) {
			dev_err(core->dev, "No port at\n");
			return -EINVAL;
		}

		/* Count the number of ports. */
		nports++;
	}

	if (nports != 1) {
		dev_err(core->dev, "invalid number of ports %u\n", nports);
		return -EINVAL;
	}

	/* Register interrupt handler */
	core->irq = irq_of_parse_and_map(node, 0);

	ret = devm_request_irq(core->dev, core->irq, xsdirxss_irq_handler,
			       IRQF_SHARED, "xilinx-sdirxss", xsdirxss);
	if (ret) {
		dev_err(core->dev, "Err = %d Interrupt handler reg failed!\n",
			ret);
		return ret;
	}

	return 0;
}

static int xsdirxss_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xsdirxss_state *xsdirxss;
	struct xsdirxss_core *core;
	struct resource *res;
	int ret;

	xsdirxss = devm_kzalloc(&pdev->dev, sizeof(*xsdirxss), GFP_KERNEL);
	if (!xsdirxss)
		return -ENOMEM;

	xsdirxss->core.dev = &pdev->dev;
	core = &xsdirxss->core;

	ret = xsdirxss_parse_of(xsdirxss);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xsdirxss->core.iomem = devm_ioremap_resource(xsdirxss->core.dev, res);
	if (IS_ERR(xsdirxss->core.iomem))
		return PTR_ERR(xsdirxss->core.iomem);

	/* Initialize V4L2 subdevice and media entity */
	xsdirxss->pads[0].flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize the default format */
	xsdirxss->default_format.code = xsdirxss->vip_format->code;
	xsdirxss->default_format.field = V4L2_FIELD_NONE;
	xsdirxss->default_format.colorspace = V4L2_COLORSPACE_DEFAULT;
	xsdirxss->default_format.width = XSDIRX_DEFAULT_WIDTH;
	xsdirxss->default_format.height = XSDIRX_DEFAULT_HEIGHT;

	xsdirxss->formats[0] = xsdirxss->default_format;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xsdirxss->subdev;
	v4l2_subdev_init(subdev, &xsdirxss_ops);

	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xsdirxss_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));

	subdev->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;

	subdev->entity.ops = &xsdirxss_media_ops;

	v4l2_set_subdevdata(subdev, xsdirxss);

	ret = media_entity_pads_init(&subdev->entity, 1, xsdirxss->pads);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xsdirxss);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	xsdirxss->streaming = false;

	dev_info(xsdirxss->core.dev, "Xilinx SDI Rx Subsystem device found!\n");

	/* Enable all stream detection by default */
	xsdirx_core_disable(core);
	xsdirx_streamflow_control(core, false);
	xsdirx_framer(core, true);
	xsdirx_setedherrcnttrigger(core, XSDIRX_DEFAULT_EDH_ERRCNT);
	xsdirx_setvidlockwindow(core, XSDIRX_DEFAULT_VIDEO_LOCK_WINDOW);
	xsdirx_clearintr(core, XSDIRX_INTR_ALL_MASK);
	xsdirx_disableintr(core, XSDIRX_INTR_ALL_MASK);
	xsdirx_enableintr(core, XSDIRX_INTR_ALL_MASK);
	xsdirx_set_modedetect(core, XSDIRX_DETECT_ALL_MODES);
	xsdirx_core_enable(core);

	return 0;
error:
	media_entity_cleanup(&subdev->entity);

	return ret;
}

static int xsdirxss_remove(struct platform_device *pdev)
{
	struct xsdirxss_state *xsdirxss = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xsdirxss->subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xsdirxss_of_id_table[] = {
	{ .compatible = "xlnx,v-smpte-uhdsdi-rx-ss" },
	{ }
};
MODULE_DEVICE_TABLE(of, xsdirxss_of_id_table);

static struct platform_driver xsdirxss_driver = {
	.driver = {
		.name		= "xilinx-sdirxss",
		.of_match_table	= xsdirxss_of_id_table,
	},
	.probe			= xsdirxss_probe,
	.remove			= xsdirxss_remove,
};

module_platform_driver(xsdirxss_driver);

MODULE_AUTHOR("Vishal Sagar <vsagar@xilinx.com>");
MODULE_DESCRIPTION("Xilinx SDI Rx Subsystem Driver");
MODULE_LICENSE("GPL v2");

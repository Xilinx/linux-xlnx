/*
 * Xilinx Video HDMI RX Subsystem driver implementing a V4L2 subdevice
 *
 * Copyright (C) 2016-2017 Leon Woestenberg <leon@sidebranch.com>
 * Copyright (C) 2016-2017 Xilinx, Inc.
 *
 * Authors: Leon Woestenberg <leon@sidebranch.com>
 *          Rohit Consul <rohitco@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* if both both DEBUG and DEBUG_TRACE are defined, trace_printk() is used */
//#define DEBUG
//#define DEBUG_TRACE

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/firmware.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-dv-timings.h>

#include "linux/phy/phy-vphy.h"

#include "xilinx-vip.h"

/* baseline driver includes */
#include "xilinx-hdmi-rx/xv_hdmirxss.h"

/* for the HMAC, using password to decrypt HDCP keys */
#include "phy-xilinx-vphy/xhdcp22_common.h"
#include "phy-xilinx-vphy/aes256.h"
/* select either trace or printk logging */
#ifdef DEBUG_TRACE
#define do_hdmi_dbg(format, ...) do { \
  trace_printk("xlnx-hdmi-rxss: " format, ##__VA_ARGS__); \
} while(0)
#else
#define do_hdmi_dbg(format, ...) do { \
  printk(KERN_DEBUG "xlnx-hdmi-rxss: " format, ##__VA_ARGS__); \
} while(0)
#endif

/* Use hdmi_dbg to debug control flow.
 * Use dev_err() to report errors to user.
 * either enable or disable debugging. */
#ifdef DEBUG
#  define hdmi_dbg(x...) do_hdmi_dbg(x)
#else
#  define hdmi_dbg(x...)
#endif

#define hdmi_mutex_lock(x) mutex_lock(x)
#define hdmi_mutex_unlock(x) mutex_unlock(x)


#define HDMI_MAX_LANES	4
#define EDID_BLOCKS_MAX 10
#define EDID_BLOCK_SIZE 128

/* RX Subsystem Sub-core offsets */
#define RXSS_RX_OFFSET				0x00000u
#define RXSS_HDCP14_OFFSET			0x10000u
#define RXSS_HDCP14_TIMER_OFFSET	0x20000u
#define RXSS_HDCP22_OFFSET			0x40000u
/* HDCP22 sub-core offsets */
#define RX_HDCP22_CIPHER_OFFSET		0x00000u
#define RX_HDCP2_MMULT_OFFSET		0x10000u
#define RX_HDCP22_TIMER_OFFSET		0x20000u
#define RX_HDCP22_RNG_OFFSET		0x30000u

struct xhdmi_device {
	struct device xvip;
	struct device *dev;
	void __iomem *iomem;
	void __iomem *hdcp1x_keymngmt_iomem;

	/* clocks */
	struct clk *clk;
	struct clk *axi_lite_clk;

	/* HDMI RXSS interrupt number */
	int irq;
	/* HDCP interrupt numbers */
	int hdcp1x_irq;
	int hdcp1x_timer_irq;
	int hdcp22_irq;
	int hdcp22_timer_irq;
	/* status */
	bool hdcp_authenticated;
	bool hdcp_encrypted;
	bool hdcp_password_accepted;
	/* delayed work to drive HDCP poll */
	struct delayed_work delayed_work_hdcp_poll;

	bool teardown;
	struct phy *phy[HDMI_MAX_LANES];

	/* mutex to prevent concurrent access to this structure */
	struct mutex xhdmi_mutex;
	/* protects concurrent access from interrupt context */
	spinlock_t irq_lock;

	/* schedule (future) work */
	struct workqueue_struct *work_queue;
	struct delayed_work delayed_work_enable_hotplug;

	struct v4l2_subdev subdev;

	/* V4L media output pad to construct the video pipeline */
	struct media_pad pad;
	struct v4l2_mbus_framefmt detected_format;
	struct v4l2_dv_timings detected_timings;
	const struct xvip_video_format *vip_format;
	struct v4l2_ctrl_handler ctrl_handler;

	bool cable_is_connected;
	bool hdmi_stream_is_up;

	/* copy of user specified EDID block, if any */
	u8 edid_user[EDID_BLOCKS_MAX * EDID_BLOCK_SIZE];
	/* number of actual blocks valid in edid_user */
	int edid_user_blocks;

	/* number of EDID blocks supported by IP */
	int edid_blocks_max;

	/* configuration for the baseline subsystem driver instance */
	XV_HdmiRxSs_Config config;
	/* bookkeeping for the baseline subsystem driver instance */
	XV_HdmiRxSs xv_hdmirxss;
	/* sub core interrupt status registers */
	u32 IntrStatus[7];
	/* pointer to xvphy */
	XVphy *xvphy;
	/* HDCP keys */
	u8 hdcp_password[32];
	u8 Hdcp22Lc128[16];
	u8 Hdcp22PrivateKey[902];
	u8 Hdcp14KeyA[328];
	u8 Hdcp14KeyB[328];
};

// Xilinx EDID
static const u8 xilinx_edid[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x61, 0x98, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
	0x1F, 0x19, 0x01, 0x03, 0x80, 0x59, 0x32, 0x78, 0x0A, 0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26,
	0x0F, 0x50, 0x54, 0x21, 0x08, 0x00, 0x71, 0x4F, 0x81, 0xC0, 0x81, 0x00, 0x81, 0x80, 0x95, 0x00,
	0xA9, 0xC0, 0xB3, 0x00, 0x01, 0x01, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0x20, 0xC2, 0x31, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x58, 0x49, 0x4C,
	0x49, 0x4E, 0x58, 0x20, 0x48, 0x44, 0x4D, 0x49, 0x0A, 0x20, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0C,
	0x02, 0x03, 0x34, 0x71, 0x57, 0x61, 0x10, 0x1F, 0x04, 0x13, 0x05, 0x14, 0x20, 0x21, 0x22, 0x5D,
	0x5E, 0x5F, 0x60, 0x65, 0x66, 0x62, 0x63, 0x64, 0x07, 0x16, 0x03, 0x12, 0x23, 0x09, 0x07, 0x07,
	0x67, 0x03, 0x0C, 0x00, 0x10, 0x00, 0x78, 0x3C, 0xE3, 0x0F, 0x01, 0xE0, 0x67, 0xD8, 0x5D, 0xC4,
	0x01, 0x78, 0x80, 0x07, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C, 0x45, 0x00,
	0x20, 0xC2, 0x31, 0x00, 0x00, 0x1E, 0x08, 0xE8, 0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80, 0xB0, 0x58,
	0x8A, 0x00, 0x20, 0xC2, 0x31, 0x00, 0x00, 0x1E, 0x04, 0x74, 0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80,
	0xB0, 0x58, 0x8A, 0x00, 0x20, 0x52, 0x31, 0x00, 0x00, 0x1E, 0x66, 0x21, 0x56, 0xAA, 0x51, 0x00,
	0x1E, 0x30, 0x46, 0x8F, 0x33, 0x00, 0x50, 0x1D, 0x74, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x2E
};

static inline struct xhdmi_device *to_xhdmi(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xhdmi_device, subdev);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static const struct v4l2_event xhdmi_ev_fmt = {
	.type = V4L2_EVENT_SOURCE_CHANGE,
	.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
};

static int xhdmi_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh, struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
		case V4L2_EVENT_SOURCE_CHANGE:
		{
			int rc;
			rc = v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
			printk(KERN_INFO "xhdmi_subscribe_event(V4L2_EVENT_SOURCE_CHANGE) = %d\n", rc);
			return rc;
		}
#if 0
		case V4L2_EVENT_CTRL:
			return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
#endif
		default:
		{
			printk(KERN_INFO "xhdmi_subscribe_event() default: -EINVAL\n");
			return -EINVAL;
		}
	}
}

 /* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xhdmi_s_stream(struct v4l2_subdev *subdev, int enable)
{
	/* HDMI does not need to be enabled when we start streaming */
	printk(KERN_INFO "xhdmi_s_stream enable = %d\n", enable);
	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

 /* https://linuxtv.org/downloads/v4l-dvb-apis/vidioc-dv-timings-cap.html */

/* https://linuxtv.org/downloads/v4l-dvb-apis/vidioc-subdev-g-fmt.html */
static struct v4l2_mbus_framefmt *
__xhdmi_get_pad_format_ptr(struct xhdmi_device *xhdmi,
		struct v4l2_subdev_pad_config *cfg,
		unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		hdmi_dbg("__xhdmi_get_pad_format(): V4L2_SUBDEV_FORMAT_TRY\n");
		return v4l2_subdev_get_try_format(&xhdmi->subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		hdmi_dbg("__xhdmi_get_pad_format(): V4L2_SUBDEV_FORMAT_ACTIVE\n");
		hdmi_dbg("detected_format->width = %u\n", xhdmi->detected_format.width);
		return &xhdmi->detected_format;
	default:
		return NULL;
	}
}

static int xhdmi_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xhdmi_device *xhdmi = to_xhdmi(subdev);
	hdmi_dbg("xhdmi_get_format\n");

	if (fmt->pad > 0)
		return -EINVAL;

	/* copy either try or currently-active (i.e. detected) format to caller */
	fmt->format = *__xhdmi_get_pad_format_ptr(xhdmi, cfg, fmt->pad, fmt->which);

	hdmi_dbg("xhdmi_get_format, height = %u\n", fmt->format.height);

	return 0;
}

/* we must modify the requested format to match what the hardware can provide */
static int xhdmi_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct xhdmi_device *xhdmi = to_xhdmi(subdev);
	hdmi_dbg("xhdmi_set_format\n");
	if (fmt->pad > 0)
		return -EINVAL;
	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	/* there is nothing we can take from the format requested by the caller,
	 * by convention we must return the active (i.e. detected) format */
	fmt->format = xhdmi->detected_format;
	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
	return 0;
}

/* https://linuxtv.org/downloads/v4l-dvb-apis-new/media/kapi/v4l2-subdev.html#v4l2-sub-device-functions-and-data-structures
 * https://linuxtv.org/downloads/v4l-dvb-apis/vidioc-g-edid.html
 */
static int xhdmi_get_edid(struct v4l2_subdev *subdev, struct v4l2_edid *edid) {
	struct xhdmi_device *xhdmi = to_xhdmi(subdev);
	int do_copy = 1;
	if (edid->pad > 0)
		return -EINVAL;
	if (edid->start_block != 0)
		return -EINVAL;
	/* caller is only interested in the size of the EDID? */
	if ((edid->start_block == 0) && (edid->blocks == 0)) do_copy = 0;
	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	/* user EDID active? */
	if (xhdmi->edid_user_blocks) {
		if (do_copy)
			memcpy(edid->edid, xhdmi->edid_user, 128 * xhdmi->edid_user_blocks);
		edid->blocks = xhdmi->edid_user_blocks;
	} else {
		if (do_copy)
			memcpy(edid->edid, &xilinx_edid[0], sizeof(xilinx_edid));
		edid->blocks = sizeof(xilinx_edid) / 128;
	}
	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
	return 0;
}

static void xhdmi_set_hpd(struct xhdmi_device *xhdmi, int enable)
{
	XV_HdmiRxSs *HdmiRxSsPtr;
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	XV_HdmiRx_SetHpd(HdmiRxSsPtr->HdmiRxPtr, enable);
}

static void xhdmi_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct xhdmi_device *xhdmi = container_of(dwork, struct xhdmi_device,
						delayed_work_enable_hotplug);
	XV_HdmiRxSs *HdmiRxSsPtr;
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = &xhdmi->xv_hdmirxss;

	XV_HdmiRx_SetHpd(HdmiRxSsPtr->HdmiRxPtr, 1);
}

static int xhdmi_set_edid(struct v4l2_subdev *subdev, struct v4l2_edid *edid) {
	struct xhdmi_device *xhdmi = to_xhdmi(subdev);
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	if (edid->pad > 0)
		return -EINVAL;
	if (edid->start_block != 0)
		return -EINVAL;
	if (edid->blocks > xhdmi->edid_blocks_max) {
		/* notify caller of how many EDID blocks this driver supports */
		edid->blocks = xhdmi->edid_blocks_max;
		return -E2BIG;
	}
	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	xhdmi->edid_user_blocks = edid->blocks;

	/* Disable hotplug and I2C access to EDID RAM from DDC port */
	cancel_delayed_work_sync(&xhdmi->delayed_work_enable_hotplug);
	xhdmi_set_hpd(xhdmi, 0);

	if (edid->blocks) {
		memcpy(xhdmi->edid_user, edid->edid, 128 * edid->blocks);
		XV_HdmiRxSs_LoadEdid(HdmiRxSsPtr, (u8 *)&xhdmi->edid_user, 128 * xhdmi->edid_user_blocks);
		/* enable hotplug after 100 ms */
		queue_delayed_work(xhdmi->work_queue,
				&xhdmi->delayed_work_enable_hotplug, HZ / 10);
	}
	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int xhdmi_enum_frame_size(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad > 0)
		return -EINVAL;
	/* we support a non-discrete set, i.e. contiguous range of frame sizes,
	 * do not return a discrete set */
	return 0;
}

static int xhdmi_dv_timings_cap(struct v4l2_subdev *subdev,
		struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;
	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.max_width = 4096;
	cap->bt.max_height = 2160;
	cap->bt.min_pixelclock = 25000000;
	cap->bt.max_pixelclock = 297000000;
	cap->bt.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			 V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT;
	cap->bt.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
		V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM;
	return 0;
}

static int xhdmi_query_dv_timings(struct v4l2_subdev *subdev,
			struct v4l2_dv_timings *timings)
{
	struct xhdmi_device *xhdmi = to_xhdmi(subdev);

	if (!timings)
		return -EINVAL;

	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	if (!xhdmi->hdmi_stream_is_up) {
		hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
		return -ENOLINK;
	}

	/* copy detected timings into destination */
	*timings = xhdmi->detected_timings;

	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
	return 0;
}

/* struct v4l2_subdev_internal_ops.open */
static int xhdmi_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xhdmi_device *xhdmi = to_xhdmi(subdev);
	(void)xhdmi;
	hdmi_dbg("xhdmi_open\n");
	return 0;
}

/* struct v4l2_subdev_internal_ops.close */
static int xhdmi_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	hdmi_dbg("xhdmi_close\n");
	return 0;
}

static int xhdmi_s_ctrl(struct v4l2_ctrl *ctrl)
{
	hdmi_dbg("xhdmi_s_ctrl\n");
	return 0;
}

static const struct v4l2_ctrl_ops xhdmi_ctrl_ops = {
	.s_ctrl	= xhdmi_s_ctrl,
};

static struct v4l2_subdev_core_ops xhdmi_core_ops = {
	.subscribe_event = xhdmi_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_video_ops xhdmi_video_ops = {
	.s_stream = xhdmi_s_stream,
	.query_dv_timings = xhdmi_query_dv_timings,
};

/* If the subdev driver intends to process video and integrate with the media framework,
 * it must implement format related functionality using v4l2_subdev_pad_ops instead of
 * v4l2_subdev_video_ops. */
static struct v4l2_subdev_pad_ops xhdmi_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xhdmi_enum_frame_size,
	.get_fmt			= xhdmi_get_format,
	.set_fmt			= xhdmi_set_format,
	.get_edid			= xhdmi_get_edid,
	.set_edid			= xhdmi_set_edid,
	.dv_timings_cap		= xhdmi_dv_timings_cap,
};

static struct v4l2_subdev_ops xhdmi_ops = {
	.core   = &xhdmi_core_ops,
	.video  = &xhdmi_video_ops,
	.pad    = &xhdmi_pad_ops,
};

static const struct v4l2_subdev_internal_ops xhdmi_internal_ops = {
	.open	= xhdmi_open,
	.close	= xhdmi_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xhdmi_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Power Management
 */

static int __maybe_unused xhdmi_pm_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused xhdmi_pm_resume(struct device *dev)
{
	return 0;
}

void HdmiRx_PioIntrHandler(XV_HdmiRx *InstancePtr);
void HdmiRx_TmrIntrHandler(XV_HdmiRx *InstancePtr);
void HdmiRx_VtdIntrHandler(XV_HdmiRx *InstancePtr);
void HdmiRx_DdcIntrHandler(XV_HdmiRx *InstancePtr);
void HdmiRx_AuxIntrHandler(XV_HdmiRx *InstancePtr);
void HdmiRx_AudIntrHandler(XV_HdmiRx *InstancePtr);
void HdmiRx_LinkStatusIntrHandler(XV_HdmiRx *InstancePtr);

static void XV_HdmiRxSs_IntrEnable(XV_HdmiRxSs *HdmiRxSsPtr)
{
	XV_HdmiRx_PioIntrEnable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_TmrIntrEnable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_VtdIntrEnable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_DdcIntrEnable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_AuxIntrEnable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_AudioIntrEnable(HdmiRxSsPtr->HdmiRxPtr);
}

static void XV_HdmiRxSs_IntrDisable(XV_HdmiRxSs *HdmiRxSsPtr)
{
	XV_HdmiRx_PioIntrDisable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_TmrIntrDisable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_VtdIntrDisable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_DdcIntrDisable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_AuxIntrDisable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_AudioIntrDisable(HdmiRxSsPtr->HdmiRxPtr);
	XV_HdmiRx_LinkIntrDisable(HdmiRxSsPtr->HdmiRxPtr);
}

static irqreturn_t hdmirx_irq_handler(int irq, void *dev_id)
{
	struct xhdmi_device *xhdmi;
	XV_HdmiRxSs *HdmiRxSsPtr;
	unsigned long flags;
	BUG_ON(!dev_id);
	xhdmi = (struct xhdmi_device *)dev_id;
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr->HdmiRxPtr);

	if (HdmiRxSsPtr->IsReady != XIL_COMPONENT_IS_READY) {
		printk(KERN_INFO "hdmirx_irq_handler(): HDMI RX SS is not initialized?!\n");
	}

	/* read status registers */
	xhdmi->IntrStatus[0] = XV_HdmiRx_ReadReg(HdmiRxSsPtr->HdmiRxPtr->Config.BaseAddress, (XV_HDMIRX_PIO_STA_OFFSET)) & (XV_HDMIRX_PIO_STA_IRQ_MASK);
	xhdmi->IntrStatus[1] = XV_HdmiRx_ReadReg(HdmiRxSsPtr->HdmiRxPtr->Config.BaseAddress, (XV_HDMIRX_TMR_STA_OFFSET)) & (XV_HDMIRX_TMR_STA_IRQ_MASK);
	xhdmi->IntrStatus[2] = XV_HdmiRx_ReadReg(HdmiRxSsPtr->HdmiRxPtr->Config.BaseAddress, (XV_HDMIRX_VTD_STA_OFFSET)) & (XV_HDMIRX_VTD_STA_IRQ_MASK);
	xhdmi->IntrStatus[3] = XV_HdmiRx_ReadReg(HdmiRxSsPtr->HdmiRxPtr->Config.BaseAddress, (XV_HDMIRX_DDC_STA_OFFSET)) & (XV_HDMIRX_DDC_STA_IRQ_MASK);
	xhdmi->IntrStatus[4] = XV_HdmiRx_ReadReg(HdmiRxSsPtr->HdmiRxPtr->Config.BaseAddress, (XV_HDMIRX_AUX_STA_OFFSET)) & (XV_HDMIRX_AUX_STA_IRQ_MASK);
	xhdmi->IntrStatus[5] = XV_HdmiRx_ReadReg(HdmiRxSsPtr->HdmiRxPtr->Config.BaseAddress, (XV_HDMIRX_AUD_STA_OFFSET)) & (XV_HDMIRX_AUD_STA_IRQ_MASK);
	xhdmi->IntrStatus[6] = XV_HdmiRx_ReadReg(HdmiRxSsPtr->HdmiRxPtr->Config.BaseAddress, (XV_HDMIRX_LNKSTA_STA_OFFSET)) & (XV_HDMIRX_LNKSTA_STA_IRQ_MASK);

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	/* mask interrupt request */
	XV_HdmiRxSs_IntrDisable(HdmiRxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	/* call bottom-half */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t hdmirx_irq_thread(int irq, void *dev_id)
{
	struct xhdmi_device *xhdmi;
	XV_HdmiRxSs *HdmiRxSsPtr;
	unsigned long flags;

	BUG_ON(!dev_id);
	xhdmi = (struct xhdmi_device *)dev_id;
	if (xhdmi->teardown) {
		printk(KERN_INFO "irq_thread: teardown\n");
		return IRQ_HANDLED;
	}
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr->HdmiRxPtr);

	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	/* call baremetal interrupt handler, this in turn will
	 * call the registed callbacks functions */

	if (xhdmi->IntrStatus[0]) HdmiRx_PioIntrHandler(HdmiRxSsPtr->HdmiRxPtr);
	if (xhdmi->IntrStatus[1]) HdmiRx_TmrIntrHandler(HdmiRxSsPtr->HdmiRxPtr);
	if (xhdmi->IntrStatus[2]) HdmiRx_VtdIntrHandler(HdmiRxSsPtr->HdmiRxPtr);
	if (xhdmi->IntrStatus[3]) HdmiRx_DdcIntrHandler(HdmiRxSsPtr->HdmiRxPtr);
	if (xhdmi->IntrStatus[4]) HdmiRx_AuxIntrHandler(HdmiRxSsPtr->HdmiRxPtr);
	if (xhdmi->IntrStatus[5]) HdmiRx_AudIntrHandler(HdmiRxSsPtr->HdmiRxPtr);
	if (xhdmi->IntrStatus[6]) HdmiRx_LinkStatusIntrHandler(HdmiRxSsPtr->HdmiRxPtr);

	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	/* unmask interrupt request */
	XV_HdmiRxSs_IntrEnable(HdmiRxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	return IRQ_HANDLED;
}


/* top-half interrupt handler for HDMI RX HDCP */
static irqreturn_t hdmirx_hdcp_irq_handler(int irq, void *dev_id)
{
	struct xhdmi_device *xhdmi;
	XV_HdmiRxSs *HdmiRxSsPtr;
	unsigned long flags;
	BUG_ON(!dev_id);
	xhdmi = (struct xhdmi_device *)dev_id;
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr->HdmiRxPtr);

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	/* mask/disable interrupt requests */
	if (irq == xhdmi->hdcp1x_irq) {
		XHdcp1x_WriteReg(HdmiRxSsPtr->Hdcp14Ptr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_MASK, (u32)0xFFFFFFFFu);
	} else if (irq == xhdmi->hdcp1x_timer_irq) {
		XTmrCtr_DisableIntr(HdmiRxSsPtr->HdcpTimerPtr->BaseAddress, 0);
	} else if (irq == xhdmi->hdcp22_timer_irq) {
		XTmrCtr_DisableIntr(HdmiRxSsPtr->Hdcp22Ptr->TimerInst.BaseAddress, 0);
		XTmrCtr_DisableIntr(HdmiRxSsPtr->Hdcp22Ptr->TimerInst.BaseAddress, 1);
	}
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	/* call bottom-half */
	return IRQ_WAKE_THREAD;
}

/* HDCP service routine, runs outside of interrupt context and can sleep and takes mutexes */
static irqreturn_t hdmirx_hdcp_irq_thread(int irq, void *dev_id)
{
	struct xhdmi_device *xhdmi;
	XV_HdmiRxSs *HdmiRxSsPtr;
	unsigned long flags;
	BUG_ON(!dev_id);
	xhdmi = (struct xhdmi_device *)dev_id;
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr->HdmiRxPtr);

	/* driver is being torn down, do not process further interrupts */
	if (xhdmi->teardown) {
		printk(KERN_INFO "irq_thread: teardown\n");
		return IRQ_HANDLED;
	}

	/* invoke the bare-metal interrupt handler under mutex lock */
	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	if (irq == xhdmi->hdcp1x_irq) {
		XV_HdmiRxSS_HdcpIntrHandler(HdmiRxSsPtr);
	} else if (irq == xhdmi->hdcp1x_timer_irq) {
		XV_HdmiRxSS_HdcpTimerIntrHandler(HdmiRxSsPtr);
	} else if (irq == xhdmi->hdcp22_timer_irq) {
		XV_HdmiRxSS_Hdcp22TimerIntrHandler(HdmiRxSsPtr);
	}
	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);

	/* re-enable interrupt requests */
	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	if (irq == xhdmi->hdcp1x_irq) {
		XHdcp1x_WriteReg(HdmiRxSsPtr->Hdcp14Ptr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_MASK, (u32)0xFFFFFFFDu);
	} else if (irq == xhdmi->hdcp1x_timer_irq) {
		XTmrCtr_EnableIntr(HdmiRxSsPtr->HdcpTimerPtr->BaseAddress, 0);
	} else if (irq == xhdmi->hdcp22_timer_irq) {
		XTmrCtr_EnableIntr(HdmiRxSsPtr->Hdcp22Ptr->TimerInst.BaseAddress, 0);
		XTmrCtr_EnableIntr(HdmiRxSsPtr->Hdcp22Ptr->TimerInst.BaseAddress, 1);
	}
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	return IRQ_HANDLED;
}

/* callbacks from HDMI RX SS interrupt handler
 * these are called with the xhdmi->mutex locked and the xvphy_mutex non-locked
 * to prevent mutex deadlock, always lock the xhdmi first, then the xvphy mutex */
static void RxConnectCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	XVphy *VphyPtr = xhdmi->xvphy;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	if (!xhdmi || !HdmiRxSsPtr || !VphyPtr) return;

	xhdmi->cable_is_connected = !!HdmiRxSsPtr->IsStreamConnected;
	hdmi_dbg("RxConnectCallback(): cable is %sconnected.\n", xhdmi->cable_is_connected? "": "dis");

	xvphy_mutex_lock(xhdmi->phy[0]);
	/* RX cable is connected? */
	if (HdmiRxSsPtr->IsStreamConnected) {
		XVphy_IBufDsEnable(VphyPtr, 0, XVPHY_DIR_RX, (TRUE));
	} else {
		/* clear GT RX TMDS clock ratio */
		VphyPtr->HdmiRxTmdsClockRatio = 0;
		XVphy_IBufDsEnable(VphyPtr, 0, XVPHY_DIR_RX, (FALSE));
	}
	xvphy_mutex_unlock(xhdmi->phy[0]);
}

static void RxStreamDownCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	if (!xhdmi || !HdmiRxSsPtr) return;
	(void)HdmiRxSsPtr;
	hdmi_dbg("RxStreamDownCallback()\n");
	xhdmi->hdmi_stream_is_up = 0;
	xhdmi->hdcp_authenticated = 0;
}

static void RxStreamInitCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	XVphy *VphyPtr = xhdmi->xvphy;
	XVidC_VideoStream *HdmiRxSsVidStreamPtr;
	u32 Status;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	BUG_ON(!VphyPtr);
	if (!xhdmi || !HdmiRxSsPtr || !VphyPtr) return;
	hdmi_dbg("RxStreamInitCallback\r\n");
	// Calculate RX MMCM parameters
	// In the application the YUV422 colordepth is 12 bits
	// However the HDMI transports YUV422 in 8 bits.
	// Therefore force the colordepth to 8 bits when the colorspace is YUV422

	HdmiRxSsVidStreamPtr = XV_HdmiRxSs_GetVideoStream(HdmiRxSsPtr);

	xvphy_mutex_lock(xhdmi->phy[0]);

	if (HdmiRxSsVidStreamPtr->ColorFormatId == XVIDC_CSF_YCRCB_422) {
		Status = XVphy_HdmiCfgCalcMmcmParam(VphyPtr, 0, XVPHY_CHANNEL_ID_CH1,
				XVPHY_DIR_RX,
				HdmiRxSsVidStreamPtr->PixPerClk,
				XVIDC_BPC_8);
	// Other colorspaces
	} else {
		Status = XVphy_HdmiCfgCalcMmcmParam(VphyPtr, 0, XVPHY_CHANNEL_ID_CH1,
				XVPHY_DIR_RX,
				HdmiRxSsVidStreamPtr->PixPerClk,
				HdmiRxSsVidStreamPtr->ColorDepth);
	}

	if (Status == XST_FAILURE) {
		xvphy_mutex_unlock(xhdmi->phy[0]);
		return;
	}

	// Enable and configure RX MMCM
	XVphy_MmcmStart(VphyPtr, 0, XVPHY_DIR_RX);
	//wait 10ms for PLL to stabilize
	usleep_range(10000, 11000);
	xvphy_mutex_unlock(xhdmi->phy[0]);
}

/* @TODO Once this upstream V4L2 patch lands, consider VIC support: https://patchwork.linuxtv.org/patch/37137/ */
static void RxStreamUpCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	XVidC_VideoStream *Stream;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	BUG_ON(!HdmiRxSsPtr->HdmiRxPtr);
	if (!xhdmi || !HdmiRxSsPtr || !HdmiRxSsPtr->HdmiRxPtr) return;
	hdmi_dbg("RxStreamUpCallback() - stream is up.\n");
	Stream = &HdmiRxSsPtr->HdmiRxPtr->Stream.Video;
#ifdef DEBUG
	XV_HdmiRx_DebugInfo(HdmiRxSsPtr->HdmiRxPtr);
#endif
	/* http://lxr.free-electrons.com/source/include/uapi/linux/videodev2.h#L1229 */
	xhdmi->detected_format.width = Stream->Timing.HActive;
	xhdmi->detected_format.height = Stream->Timing.VActive;

	xhdmi->detected_format.field = Stream->IsInterlaced? V4L2_FIELD_INTERLACED: V4L2_FIELD_NONE;
	/* https://linuxtv.org/downloads/v4l-dvb-apis/ch02s05.html#v4l2-colorspace */
	if (Stream->ColorFormatId == XVIDC_CSF_RGB) {
		hdmi_dbg("xhdmi->detected_format.colorspace = V4L2_COLORSPACE_SRGB\n");
		xhdmi->detected_format.colorspace = V4L2_COLORSPACE_SRGB;
	} else {
		hdmi_dbg("xhdmi->detected_format.colorspace = V4L2_COLORSPACE_REC709\n");
		xhdmi->detected_format.colorspace = V4L2_COLORSPACE_REC709;
	}

	/* https://linuxtv.org/downloads/v4l-dvb-apis/subdev.html#v4l2-mbus-framefmt */
	/* see UG934 page 8 */
	/* the V4L2 media bus fmt codes match the AXI S format, and match those from TPG */
	if (Stream->ColorFormatId == XVIDC_CSF_RGB) {
		/* red blue green */
		xhdmi->detected_format.code = MEDIA_BUS_FMT_RBG888_1X24;
		hdmi_dbg("XVIDC_CSF_RGB -> MEDIA_BUS_FMT_RBG888_1X24\n");
	} else if (Stream->ColorFormatId == XVIDC_CSF_YCRCB_444) {
		xhdmi->detected_format.code = MEDIA_BUS_FMT_VUY8_1X24;
		hdmi_dbg("XVIDC_CSF_YCRCB_444 -> MEDIA_BUS_FMT_VUY8_1X24\n");
	} else if (Stream->ColorFormatId == XVIDC_CSF_YCRCB_422) {
		xhdmi->detected_format.code = MEDIA_BUS_FMT_UYVY8_1X16;
		hdmi_dbg("XVIDC_CSF_YCRCB_422 -> MEDIA_BUS_FMT_UYVY8_1X16\n");
	} else if (Stream->ColorFormatId == XVIDC_CSF_YCRCB_420) {
		xhdmi->detected_format.code = MEDIA_BUS_FMT_VYYUYY8_1X24;
		hdmi_dbg("XVIDC_CSF_YCRCB_420 -> MEDIA_BUS_FMT_VYYUYY8_1X24\n");
	}

	xhdmi->detected_format.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	xhdmi->detected_format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	xhdmi->detected_format.quantization = V4L2_QUANTIZATION_DEFAULT;

	/* map to v4l2_dv_timings */
	xhdmi->detected_timings.type =  V4L2_DV_BT_656_1120;

	/* Read Active Pixels */
	xhdmi->detected_timings.bt.width = Stream->Timing.HActive;
	/* Active lines field 1 */
	xhdmi->detected_timings.bt.height = Stream->Timing.VActive;
	/* Interlaced */
	xhdmi->detected_timings.bt.interlaced = !!Stream->IsInterlaced;
	xhdmi->detected_timings.bt.polarities =
	/* Vsync polarity, Positive == 1 */
		(Stream->Timing.VSyncPolarity? V4L2_DV_VSYNC_POS_POL: 0) |
	/* Hsync polarity, Positive == 1 */
		(Stream->Timing.HSyncPolarity? V4L2_DV_HSYNC_POS_POL: 0);

	/* from xvid.c:XVidC_GetPixelClockHzByVmId() but without VmId */
	if (Stream->IsInterlaced) {
		xhdmi->detected_timings.bt.pixelclock =
			(Stream->Timing.F0PVTotal + Stream->Timing.F1VTotal) *
			Stream->FrameRate / 2;
	} else {
		xhdmi->detected_timings.bt.pixelclock =
			Stream->Timing.F0PVTotal * Stream->FrameRate;
	}
	xhdmi->detected_timings.bt.pixelclock *= Stream->Timing.HTotal;

	hdmi_dbg("HdmiRxSsPtr->HdmiRxPtr->Stream.PixelClk = %d\n", HdmiRxSsPtr->HdmiRxPtr->Stream.PixelClk);
	/* Read HFront Porch */
	xhdmi->detected_timings.bt.hfrontporch = Stream->Timing.HFrontPorch;
	/* Read Hsync Width */
	xhdmi->detected_timings.bt.hsync = Stream->Timing.HSyncWidth;
	/* Read HBack Porch */
	xhdmi->detected_timings.bt.hbackporch = Stream->Timing.HBackPorch;
	/* Read VFront Porch field 1*/
	xhdmi->detected_timings.bt.vfrontporch = Stream->Timing.F0PVFrontPorch;
	/* Read VSync Width field 1*/
	xhdmi->detected_timings.bt.vsync = Stream->Timing.F0PVSyncWidth;
	/* Read VBack Porch field 1 */
	xhdmi->detected_timings.bt.vbackporch = Stream->Timing.F0PVBackPorch;
	/* Read VFront Porch field 2*/
	xhdmi->detected_timings.bt.il_vfrontporch = Stream->Timing.F1VFrontPorch;
	/* Read VSync Width field 2*/
	xhdmi->detected_timings.bt.il_vsync = Stream->Timing.F1VSyncWidth;
	/* Read VBack Porch field 2 */
	xhdmi->detected_timings.bt.il_vbackporch = Stream->Timing.F1VBackPorch;
	xhdmi->detected_timings.bt.standards = V4L2_DV_BT_STD_CEA861;
	xhdmi->detected_timings.bt.flags = V4L2_DV_FL_IS_CE_VIDEO;

	(void)Stream->VmId;

	xhdmi->hdmi_stream_is_up = 1;
	/* notify source format change event */
	v4l2_subdev_notify_event(&xhdmi->subdev, &xhdmi_ev_fmt);

#ifdef DEBUG
	v4l2_print_dv_timings("xilinx-hdmi-rx", "", & xhdmi->detected_timings, 1);
#endif
}

/* Called from non-interrupt context with xvphy mutex locked
 */
static void VphyHdmiRxInitCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	XVphy *VphyPtr = xhdmi->xvphy;
	BUG_ON(!xhdmi);
	BUG_ON(!VphyPtr);
	BUG_ON(!xhdmi->phy[0]);
	if (!xhdmi || !VphyPtr) return;
	hdmi_dbg("VphyHdmiRxInitCallback()\n");

	/* a pair of mutexes must be locked in fixed order to prevent deadlock,
	 * and the order is RX SS then XVPHY, so first unlock XVPHY then lock both */
	xvphy_mutex_unlock(xhdmi->phy[0]);
	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	xvphy_mutex_lock(xhdmi->phy[0]);

	XV_HdmiRxSs_RefClockChangeInit(HdmiRxSsPtr);
	/* @NOTE maybe implement xvphy_set_hdmirx_tmds_clockratio(); */
	VphyPtr->HdmiRxTmdsClockRatio = HdmiRxSsPtr->TMDSClockRatio;
	/* unlock RX SS but keep XVPHY locked */
	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
}

/* Called from non-interrupt context with xvphy mutex locked
 */
static void VphyHdmiRxReadyCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	XVphy *VphyPtr = xhdmi->xvphy;
	XVphy_PllType RxPllType;
	BUG_ON(!xhdmi);
	BUG_ON(!VphyPtr);
	BUG_ON(!xhdmi->phy[0]);
	if (!xhdmi || !VphyPtr) return;
	hdmi_dbg("VphyHdmiRxReadyCallback()\n");

	/* a pair of mutexes must be locked in fixed order to prevent deadlock,
	 * and the order is RX SS then XVPHY, so first unlock XVPHY then lock both */
	xvphy_mutex_unlock(xhdmi->phy[0]);
	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	xvphy_mutex_lock(xhdmi->phy[0]);

	RxPllType = XVphy_GetPllType(VphyPtr, 0, XVPHY_DIR_RX,
		XVPHY_CHANNEL_ID_CH1);
	if (!(RxPllType == XVPHY_PLL_TYPE_CPLL)) {
		XV_HdmiRxSs_SetStream(&xhdmi->xv_hdmirxss, VphyPtr->HdmiRxRefClkHz,
				(XVphy_GetLineRateHz(VphyPtr, 0, XVPHY_CHANNEL_ID_CMN0)/1000000));
	}
	else {
		XV_HdmiRxSs_SetStream(&xhdmi->xv_hdmirxss, VphyPtr->HdmiRxRefClkHz,
				(XVphy_GetLineRateHz(VphyPtr, 0, XVPHY_CHANNEL_ID_CH1)/1000000));
	}
	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
}

static void RxHdcpAuthenticatedCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	XV_HdmiRxSs *HdmiRxSsPtr;
	int HdcpProtocol;
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);
	HdcpProtocol = XV_HdmiRxSs_HdcpGetProtocol(HdmiRxSsPtr);
	xhdmi->hdcp_authenticated = 1;
	switch (HdcpProtocol) {
	case XV_HDMIRXSS_HDCP_22:
		hdmi_dbg("HDCP 2.2 RX authenticated.\n");
		break;
	case XV_HDMIRXSS_HDCP_14:
		hdmi_dbg("HDCP 1.4 RX authenticated.\n");
		break;
	}
}

static void RxHdcpUnauthenticatedCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	BUG_ON(!xhdmi);
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);
	xhdmi->hdcp_authenticated = 0;
	hdmi_dbg("HDCP RX unauthenticated.\n");
}

static void RxHdcpEncryptionUpdateCallback(void *CallbackRef)
{
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)CallbackRef;
	BUG_ON(!xhdmi);
	XV_HdmiRxSs *HdmiRxSsPtr = &xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);
	xhdmi->hdcp_encrypted = !!XV_HdmiRxSs_HdcpIsEncrypted(HdmiRxSsPtr);
	hdmi_dbg("HDCP RX encryption changed; now %s.\n", xhdmi->hdcp_encrypted? "enabled": "disabled");
}

/* this function is responsible for periodically calling XV_HdmiRxSs_HdcpPoll() */
static void hdcp_poll_work(struct work_struct *work)
{
	/* find our parent container structure */
	struct xhdmi_device *xhdmi = container_of(work, struct xhdmi_device,
		delayed_work_hdcp_poll.work);

	XV_HdmiRxSs *HdmiRxSsPtr;
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);

	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);
	XV_HdmiRxSs_HdcpPoll(HdmiRxSsPtr);
	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
	/* reschedule this work again in 1 millisecond */
	schedule_delayed_work(&xhdmi->delayed_work_hdcp_poll, msecs_to_jiffies(1));
	return;
}

static int XHdcp_KeyManagerInit(uintptr_t BaseAddress, u8 *Hdcp14Key)
{
	u32 RegValue;
	u8 Row;
	u8 i;
	u8 *KeyPtr;
	u8 Status;

	/* Assign key pointer */
	KeyPtr = Hdcp14Key;

	/* Reset */
	Xil_Out32((BaseAddress + 0x0c), (1<<31));

	// There are 41 rows
	for (Row=0; Row<41; Row++)
	{
		/* Set write enable */
		Xil_Out32((BaseAddress + 0x20), 1);

		/* High data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		/* Write high data */
		Xil_Out32((BaseAddress + 0x2c), RegValue);

		/* Low data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		/* Write low data */
		Xil_Out32((BaseAddress + 0x30), RegValue);

		/* Table / Row Address */
		Xil_Out32((BaseAddress + 0x28), Row);

		// Write in progress
		do
		{
			RegValue = Xil_In32(BaseAddress + 0x24);
			RegValue &= 1;
		} while (RegValue != 0);
	}

	// Verify

	/* Re-Assign key pointer */
	KeyPtr = Hdcp14Key;

	/* Default Status */
	Status = XST_SUCCESS;

	/* Start at row 0 */
	Row = 0;

	do
	{
		/* Set read enable */
		Xil_Out32((BaseAddress + 0x20), (1<<1));

		/* Table / Row Address */
		Xil_Out32((BaseAddress + 0x28), Row);

		// Read in progress
		do
		{
			RegValue = Xil_In32(BaseAddress + 0x24);
			RegValue &= 1;
		} while (RegValue != 0);

		/* High data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		if (RegValue != Xil_In32(BaseAddress + 0x2c))
			Status = XST_FAILURE;

		/* Low data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		if (RegValue != Xil_In32(BaseAddress + 0x30))
			Status = XST_FAILURE;

		/* Increment row */
		Row++;

	} while ((Row<41) && (Status == XST_SUCCESS));

	if (Status == XST_SUCCESS)
	{
		/* Set read lockout */
		Xil_Out32((BaseAddress + 0x20), (1<<31));

		/* Start AXI-Stream */
		Xil_Out32((BaseAddress + 0x0c), (1));
	}

	return Status;
}



/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */
static int instance = 0;
/* TX uses [1, 127] and RX uses [128, 254] */
/* The HDCP22 timer uses an additional offset of +64 */
#define RX_DEVICE_ID_BASE 128

/* Local Global table for sub-core instance(s) configuration settings */
XV_HdmiRx_Config XV_HdmiRx_ConfigTable[XPAR_XV_HDMIRX_NUM_INSTANCES];

extern XHdcp22_Cipher_Config XHdcp22_Cipher_ConfigTable[];
extern XHdcp22_Rng_Config XHdcp22_Rng_ConfigTable[];
extern XHdcp22_mmult_Config XHdcp22_mmult_ConfigTable[];
extern XHdcp1x_Config XHdcp1x_ConfigTable[];
extern XTmrCtr_Config XTmrCtr_ConfigTable[];
extern XHdcp22_Rx_Config XHdcp22_Rx_ConfigTable[];

/* Compute the absolute address by adding subsystem base address
   to sub-core offset */
static int xhdmi_subcore_AbsAddr(uintptr_t SubSys_BaseAddr,
                                 uintptr_t SubSys_HighAddr,
								 uintptr_t SubCore_Offset,
								 uintptr_t *SubCore_AbsAddr)
{
  int Status;
  uintptr_t absAddr;

  absAddr = SubSys_BaseAddr | SubCore_Offset;
  if((absAddr>=SubSys_BaseAddr) && (absAddr<=SubSys_HighAddr)) {
    *SubCore_AbsAddr = absAddr;
    Status = XST_SUCCESS;
  } else {
    *SubCore_AbsAddr = 0;
    Status = XST_FAILURE;
  }

  return(Status);
}

/* Each sub-core within the subsystem has defined offset read from
   device-tree. */
static int xhdmi_compute_subcore_AbsAddr(XV_HdmiRxSs_Config *config)
{
	int ret;

	/* Subcore: Rx */
	ret = xhdmi_subcore_AbsAddr(config->BaseAddress,
							    config->HighAddress,
							    config->HdmiRx.AbsAddr,
							    &(config->HdmiRx.AbsAddr));
	if (ret != XST_SUCCESS) {
	   hdmi_dbg("hdmirx sub-core address out-of range\n");
	   return -EFAULT;
	}
	XV_HdmiRx_ConfigTable[instance].BaseAddress = config->HdmiRx.AbsAddr;

	/* Subcore: hdcp1x */
	if (config->Hdcp14.IsPresent) {
		ret = xhdmi_subcore_AbsAddr(config->BaseAddress,
									config->HighAddress,
									config->Hdcp14.AbsAddr,
									&(config->Hdcp14.AbsAddr));
	  if (ret != XST_SUCCESS) {
	     hdmi_dbg("hdcp1x sub-core address out-of range\n");
	     return -EFAULT;
	  }
	  XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES/2 + instance].BaseAddress = config->Hdcp14.AbsAddr;
    }

	/* Subcore: hdcp1x timer */
	if (config->HdcpTimer.IsPresent) {
	  ret = xhdmi_subcore_AbsAddr(config->BaseAddress,
	  						      config->HighAddress,
	  						      config->HdcpTimer.AbsAddr,
	  						      &(config->HdcpTimer.AbsAddr));
	  if (ret != XST_SUCCESS) {
	     hdmi_dbg("hdcp1x timer sub-core address out-of range\n");
	     return -EFAULT;
	  }
	  XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES/2 + instance * 2 + 0].BaseAddress = config->HdcpTimer.AbsAddr;
    }

	/* Subcore: hdcp22 */
	if (config->Hdcp22.IsPresent) {
	  ret = xhdmi_subcore_AbsAddr(config->BaseAddress,
	  						      config->HighAddress,
	  						      config->Hdcp22.AbsAddr,
	  						      &(config->Hdcp22.AbsAddr));
	  if (ret != XST_SUCCESS) {
	     hdmi_dbg("hdcp22 sub-core address out-of range\n");
	     return -EFAULT;
	  }
	  XHdcp22_Rx_ConfigTable[instance].BaseAddress = config->Hdcp22.AbsAddr;
	}

	return (ret);
}

static ssize_t vphy_log_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	XVphy *VphyPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	VphyPtr = xhdmi->xvphy;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	BUG_ON(!VphyPtr);
	count = XVphy_LogShow(VphyPtr, buf, PAGE_SIZE);
	return count;
}

static ssize_t vphy_info_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	XVphy *VphyPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	VphyPtr = xhdmi->xvphy;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	BUG_ON(!VphyPtr);
	count = XVphy_HdmiDebugInfo(VphyPtr, 0, XVPHY_CHANNEL_ID_CHA, buf, PAGE_SIZE);
	count += scnprintf(&buf[count], (PAGE_SIZE-count), "Rx Ref Clk: %0d Hz\n",
				XVphy_ClkDetGetRefClkFreqHz(xhdmi->xvphy, XVPHY_DIR_RX));
	count += scnprintf(&buf[count], (PAGE_SIZE-count), "DRU Ref Clk: %0d Hz\n",
				XVphy_DruGetRefClkFreqHz(xhdmi->xvphy));
	return count;
}

static ssize_t hdmi_log_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	count = XV_HdmiRxSs_LogShow(HdmiRxSsPtr, buf, PAGE_SIZE);
	return count;
}

static ssize_t hdcp_log_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	count = XV_HdmiRxSs_HdcpInfo(HdmiRxSsPtr, buf, PAGE_SIZE);
	return count;
}

static ssize_t hdmi_info_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	count = 0;
	if (XV_HdmiRxSs_IsStreamUp(HdmiRxSsPtr)) {
		count = XVidC_ShowStreamInfo(&HdmiRxSsPtr->HdmiRxPtr->Stream.Video, buf, PAGE_SIZE);
	}
	count += XV_HdmiRxSs_ShowInfo(HdmiRxSsPtr, &buf[count], (PAGE_SIZE-count));
	return count;
}

static ssize_t hdcp_debugen_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	long int i;
	XV_HdmiRxSs *HdmiRxSsPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);
	if (kstrtol(buf, 10, &i)) {
		printk(KERN_INFO "hdcp_debugen_store() input invalid.\n");
		return count;
	}
	i = !!i;
	if (i) {
		/* Enable detail logs for hdcp transactions*/
		XV_HdmiRxSs_HdcpSetInfoDetail(HdmiRxSsPtr, TRUE);
	} else {
		/* Disable detail logs for hdcp transactions*/
		XV_HdmiRxSs_HdcpSetInfoDetail(HdmiRxSsPtr, FALSE);
	}
	return count;
}

static ssize_t hdcp_authenticated_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);
	count = scnprintf(buf, PAGE_SIZE, "%d", xhdmi->hdcp_authenticated);
	return count;
}

static ssize_t hdcp_encrypted_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);
	count = scnprintf(buf, PAGE_SIZE, "%d", xhdmi->hdcp_encrypted);
	return count;
}

/* This function decrypts the HDCP keys, uses aes256.c */
/* Note that the bare-metal implementation deciphers in-place in the cipherbuffer, then after that copies to the plaintext buffer,
 * thus trashing the source.
 *
 * In this implementation, the cipher is first copied to the plain buffer, where it is then decrypted in-place. This leaves the
 * source buffer intact.
 */
static void Decrypt(const u8 *CipherBufferPtr/*src*/, u8 *PlainBufferPtr/*dst*/, u8 *Key, u16 Length)
{
	u8 i;
	u8 *AesBufferPtr;
	u16 AesLength;
	aes256_context ctx;

	// Copy cipher into plain buffer // @NOTE: Added
	memcpy(PlainBufferPtr, CipherBufferPtr, Length);

	// Assign local Pointer // @NOTE: Changed
	AesBufferPtr = PlainBufferPtr;

	// Initialize AES256
	aes256_init(&ctx, Key);

	AesLength = Length/16;
	if (Length % 16) {
		AesLength++;
	}

	for (i=0; i<AesLength; i++)
	{
		// Decrypt
		aes256_decrypt_ecb(&ctx, AesBufferPtr);

		// Increment pointer
		AesBufferPtr += 16;	// The aes always encrypts 16 bytes
	}

	// Done
	aes256_done(&ctx);
}

#define SIGNATURE_OFFSET			0
#define HDCP22_LC128_OFFSET			16
#define HDCP22_CERTIFICATE_OFFSET	32
#define HDCP14_KEY1_OFFSET			1024
#define HDCP14_KEY2_OFFSET			1536

/* buffer points to the encrypted data (from EEPROM), password points to a 32-character password */
static int XHdcp_LoadKeys(const u8 *Buffer, u8 *Password, u8 *Hdcp22Lc128, u32 Hdcp22Lc128Size, u8 *Hdcp22RxPrivateKey, u32 Hdcp22RxPrivateKeySize,
	u8 *Hdcp14KeyA, u32 Hdcp14KeyASize, u8 *Hdcp14KeyB, u32 Hdcp14KeyBSize)
{
	u8 i;
	const u8 HdcpSignature[16] = { "xilinx_hdcp_keys" };
	u8 Key[32];
	u8 SignatureOk;
	u8 HdcpSignatureBuffer[16];

	// Generate password hash
	XHdcp22Cmn_Sha256Hash(Password, 32, Key);

	/* decrypt the signature */
	Decrypt(&Buffer[SIGNATURE_OFFSET]/*source*/, HdcpSignatureBuffer/*destination*/, Key, sizeof(HdcpSignature));

	SignatureOk = 1;
	for (i = 0; i < sizeof(HdcpSignature); i++) {
		if (HdcpSignature[i] != HdcpSignatureBuffer[i])
			SignatureOk = 0;
	}

	/* password and buffer are correct, as the generated key could correctly decrypt the signature */
	if (SignatureOk == 1) {
		/* decrypt the keys */
		Decrypt(&Buffer[HDCP22_LC128_OFFSET], Hdcp22Lc128, Key, Hdcp22Lc128Size);
		Decrypt(&Buffer[HDCP22_CERTIFICATE_OFFSET], Hdcp22RxPrivateKey, Key, Hdcp22RxPrivateKeySize);
		Decrypt(&Buffer[HDCP14_KEY1_OFFSET], Hdcp14KeyA, Key, Hdcp14KeyASize);
		Decrypt(&Buffer[HDCP14_KEY2_OFFSET], Hdcp14KeyB, Key, Hdcp14KeyBSize);
		return XST_SUCCESS;
	} else {
		printk(KERN_INFO "HDCP key store signature mismatch; HDCP key data and/or password are invalid.\n");
	}
	return XST_FAILURE;
}

/* assume the HDCP C structures containing the keys are valid, and sets them in the bare-metal driver / IP */
static int hdcp_keys_configure(struct xhdmi_device *xhdmi)
{
	XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;

	if (xhdmi->config.Hdcp14.IsPresent && xhdmi->config.HdcpTimer.IsPresent && xhdmi->hdcp1x_keymngmt_iomem) {
		u8 Status;
		hdmi_dbg("HDCP1x components are all there.\n");
		/* Set pointer to HDCP 1.4 key */
		XV_HdmiRxSs_HdcpSetKey(HdmiRxSsPtr, XV_HDMIRXSS_KEY_HDCP14, xhdmi->Hdcp14KeyB);
		/* Key manager Init */
		Status = XHdcp_KeyManagerInit((uintptr_t)xhdmi->hdcp1x_keymngmt_iomem, HdmiRxSsPtr->Hdcp14KeyPtr);
		if (Status != XST_SUCCESS) {
			dev_err(xhdmi->dev, "HDCP 1.4 RX Key Manager initialization error.\n");
			return -EINVAL;
		}
		dev_info(xhdmi->dev, "HDCP 1.4 RX Key Manager initialized OK.\n");
	}
	if (xhdmi->config.Hdcp22.IsPresent) {
		/* Set pointer to HDCP 2.2 LC128 */
		XV_HdmiRxSs_HdcpSetKey(HdmiRxSsPtr, XV_HDMIRXSS_KEY_HDCP22_LC128, xhdmi->Hdcp22Lc128);
		/* Set pointer to HDCP 2.2 private key */
		XV_HdmiRxSs_HdcpSetKey(HdmiRxSsPtr, XV_HDMIRXSS_KEY_HDCP22_PRIVATE, xhdmi->Hdcp22PrivateKey);
	}
	return 0;
}

/* the EEPROM contents (i.e. the encrypted HDCP keys) must be dumped as a binary blob;
 * the user must first upload the password */
static ssize_t hdcp_key_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	long int i;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	/* check for valid size of HDCP encrypted key binary blob, @TODO adapt */
	if (count < 1872) {
		printk(KERN_INFO "hdcp_key_store(count = %d, expected >=1872)\n", (int)count);
		return -EINVAL;
	}
	xhdmi->hdcp_password_accepted = 0;
	/* decrypt the keys from the binary blob (buffer) into the C structures for keys */
	if (XHdcp_LoadKeys(buf, xhdmi->hdcp_password,
		xhdmi->Hdcp22Lc128, sizeof(xhdmi->Hdcp22Lc128),
		xhdmi->Hdcp22PrivateKey, sizeof(xhdmi->Hdcp22PrivateKey),
		xhdmi->Hdcp14KeyA, sizeof(xhdmi->Hdcp14KeyA),
		xhdmi->Hdcp14KeyB, sizeof(xhdmi->Hdcp14KeyB)) == XST_SUCCESS) {

		xhdmi->hdcp_password_accepted = 1;
		/* configure the keys in the IP */
		hdcp_keys_configure(xhdmi);

		/* configure HDCP in HDMI */
		u8 Status = XV_HdmiRxSs_CfgInitializeHdcp(HdmiRxSsPtr, &xhdmi->config, (uintptr_t)xhdmi->iomem);
		if (Status != XST_SUCCESS) {
			dev_err(xhdmi->dev, "XV_HdmiRxSs_CfgInitializeHdcp() failed with error %d\n", Status);
			return -EINVAL;
		}

		XV_HdmiRxSs_SetCallback(HdmiRxSsPtr, XV_HDMIRXSS_HANDLER_HDCP_AUTHENTICATED,
			RxHdcpAuthenticatedCallback, (void *)xhdmi);
		XV_HdmiRxSs_SetCallback(HdmiRxSsPtr, XV_HDMIRXSS_HANDLER_HDCP_UNAUTHENTICATED,
			RxHdcpUnauthenticatedCallback, (void *)xhdmi);
		XV_HdmiRxSs_SetCallback(HdmiRxSsPtr, XV_HDMIRXSS_HANDLER_HDCP_ENCRYPTION_UPDATE,
			RxHdcpEncryptionUpdateCallback, (void *)xhdmi);

		if (HdmiRxSsPtr->Config.Hdcp14.IsPresent || HdmiRxSsPtr->Config.Hdcp22.IsPresent) {
			if (xhdmi->cable_is_connected) {
				// Push connect event to HDCP event queue
				XV_HdmiRxSs_HdcpPushEvent(HdmiRxSsPtr, XV_HDMIRXSS_HDCP_CONNECT_EVT);
				/* Force HPD toggle */
				XV_HdmiRxSs_ToggleHpd(HdmiRxSsPtr);
			}
			/* call into hdcp_poll_work, which will reschedule itself */
			hdcp_poll_work(&xhdmi->delayed_work_hdcp_poll.work);
		}
	}
	return count;
}

static ssize_t hdcp_password_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiRxSs *HdmiRxSsPtr;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	BUG_ON(!xhdmi);
	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!HdmiRxSsPtr);
	count = scnprintf(buf, PAGE_SIZE, "%s", xhdmi->hdcp_password_accepted? "accepted": "rejected");
	return count;
}

/* store the HDCP key password, after this the HDCP key can be written to sysfs */
static ssize_t hdcp_password_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int i = 0;
	struct xhdmi_device *xhdmi = (struct xhdmi_device *)dev_get_drvdata(sysfs_dev);
	XV_HdmiRxSs *HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	BUG_ON(!xhdmi);
	BUG_ON(!HdmiRxSsPtr);
	if (count > sizeof(xhdmi->hdcp_password)) return -EINVAL;
	/* copy password characters up to newline or carriage return */
	while ((i < count) && (i < sizeof(xhdmi->hdcp_password))) {
		/* do not include newline or carriage return in password */
		if ((buf[i] == '\n') || (buf[i] == '\r') || (buf[i] == 0)) break;
		xhdmi->hdcp_password[i] = buf[i];
		i++;
	}
	/* zero remaining characters */
	while (i < sizeof(xhdmi->hdcp_password)) {
		xhdmi->hdcp_password[i] = 0;
		i++;
	}
	return count;
}

static ssize_t null_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{ return 0; }

static ssize_t null_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{ return count; }

static DEVICE_ATTR(vphy_log,  0444, vphy_log_show, NULL/*null_store*/);
static DEVICE_ATTR(vphy_info, 0444, vphy_info_show, NULL/*null_store*/);
static DEVICE_ATTR(hdmi_log,  0444, hdmi_log_show, NULL/*null_store*/);
static DEVICE_ATTR(hdcp_log,  0444, hdcp_log_show, NULL/*null_store*/);
static DEVICE_ATTR(hdmi_info, 0444, hdmi_info_show, NULL/*null_store*/);
static DEVICE_ATTR(hdcp_debugen, 0220, NULL/*show*/, hdcp_debugen_store);
static DEVICE_ATTR(hdcp_key, 0220, NULL/*null_show*/, hdcp_key_store);
static DEVICE_ATTR(hdcp_password, 0660, hdcp_password_show, hdcp_password_store);
/* read-only status */
static DEVICE_ATTR(hdcp_authenticated, 0444, hdcp_authenticated_show, NULL/*store*/);
static DEVICE_ATTR(hdcp_encrypted, 0444, hdcp_encrypted_show, NULL/*store*/);

static struct attribute *attrs[] = {
	&dev_attr_vphy_log.attr,
	&dev_attr_vphy_info.attr,
	&dev_attr_hdmi_log.attr,
	&dev_attr_hdcp_log.attr,
	&dev_attr_hdmi_info.attr,
	&dev_attr_hdcp_debugen.attr,
	&dev_attr_hdcp_key.attr,
	&dev_attr_hdcp_password.attr,
	&dev_attr_hdcp_authenticated.attr,
	&dev_attr_hdcp_encrypted.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};
static int xhdmi_parse_of(struct xhdmi_device *xhdmi, XV_HdmiRxSs_Config *config)
{
	struct device *dev = xhdmi->dev;
	struct device_node *node = dev->of_node;
	int rc;
	bool isHdcp14_en, isHdcp22_en;
	u32 val;

	rc = of_property_read_u32(node, "xlnx,input-pixels-per-clock", &val);
	if (rc < 0)
		goto error_dt;
	config->Ppc = val;

	rc = of_property_read_u32(node, "xlnx,edid-ram-size", &val);
	if (rc == 0) {
		if (val % 128)
			goto error_dt;
		xhdmi->edid_blocks_max = val / EDID_BLOCK_SIZE;
	}

	/* RX Core */
	config->HdmiRx.DeviceId = RX_DEVICE_ID_BASE + instance;
	config->HdmiRx.IsPresent = 1;
	config->HdmiRx.AbsAddr = RXSS_RX_OFFSET;
	XV_HdmiRx_ConfigTable[instance].DeviceId = RX_DEVICE_ID_BASE + instance;
	XV_HdmiRx_ConfigTable[instance].BaseAddress = RXSS_RX_OFFSET;

	isHdcp14_en = of_property_read_bool(node, "xlnx,include-hdcp-1-4");
	isHdcp22_en = of_property_read_bool(node, "xlnx,include-hdcp-2-2");

	if (isHdcp14_en) {
		/* HDCP14 Core */
		/* make subcomponent of RXSS present */
		config->Hdcp14.DeviceId = RX_DEVICE_ID_BASE + instance;
		config->Hdcp14.IsPresent = 1;
		config->Hdcp14.AbsAddr = RXSS_HDCP14_OFFSET;
		/* and configure it */
		XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES/2 + instance].DeviceId = config->Hdcp14.DeviceId;
		XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES/2 + instance].BaseAddress = RXSS_HDCP14_OFFSET;
		XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES/2 + instance].IsRx = 1;
		XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES/2 + instance].IsHDMI = 1;

		/* HDCP14 Timer Core */
		/* make subcomponent of RXSS present */
		config->HdcpTimer.DeviceId = RX_DEVICE_ID_BASE + instance;
		config->HdcpTimer.IsPresent = 1;
		config->HdcpTimer.AbsAddr = RXSS_HDCP14_TIMER_OFFSET;
		/* and configure it */
		XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES/2 + instance * 2 + 0].DeviceId = config->HdcpTimer.DeviceId;
		XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES/2 + instance * 2 + 0].BaseAddress = RXSS_HDCP14_TIMER_OFFSET;
		/* @TODO increment timer index */
	}

	if (isHdcp22_en) {
		/* HDCP22 SS */
		config->Hdcp22.DeviceId = RX_DEVICE_ID_BASE + instance;
		config->Hdcp22.IsPresent = 1;
		config->Hdcp22.AbsAddr = RXSS_HDCP22_OFFSET;
		XHdcp22_Rx_ConfigTable[instance].DeviceId = config->Hdcp22.DeviceId;
		XHdcp22_Rx_ConfigTable[instance].BaseAddress = RXSS_HDCP22_OFFSET;
		XHdcp22_Rx_ConfigTable[instance].Protocol = 0; /*HDCP22_RX_HDMI*/
		XHdcp22_Rx_ConfigTable[instance].Mode = 0; /*XHDCP22_RX_RECEIVER*/
		XHdcp22_Rx_ConfigTable[instance].TimerDeviceId = RX_DEVICE_ID_BASE + instance;
		XHdcp22_Rx_ConfigTable[instance].CipherDeviceId = RX_DEVICE_ID_BASE + instance;
		XHdcp22_Rx_ConfigTable[instance].MontMultDeviceId = RX_DEVICE_ID_BASE + instance;
		XHdcp22_Rx_ConfigTable[instance].RngDeviceId = RX_DEVICE_ID_BASE + instance;

		/* HDCP22 Cipher Core */
		XHdcp22_Cipher_ConfigTable[XPAR_XHDCP22_CIPHER_NUM_INSTANCES/2 + instance].DeviceId = RX_DEVICE_ID_BASE + instance;
		XHdcp22_Cipher_ConfigTable[XPAR_XHDCP22_CIPHER_NUM_INSTANCES/2 + instance].BaseAddress = RX_HDCP22_CIPHER_OFFSET;
		/* HDCP22 MMULT Core */
		XHdcp22_mmult_ConfigTable[XPAR_XHDCP22_MMULT_NUM_INSTANCES/2 + instance].DeviceId = RX_DEVICE_ID_BASE + instance;
		XHdcp22_mmult_ConfigTable[XPAR_XHDCP22_MMULT_NUM_INSTANCES/2 + instance].BaseAddress = RX_HDCP2_MMULT_OFFSET;
		/* HDCP22-Timer Core */
		XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES/2 + instance * 2 + 1].DeviceId = RX_DEVICE_ID_BASE + 64 + instance;
		XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES/2 + instance * 2 + 1].BaseAddress = RX_HDCP22_TIMER_OFFSET;
		/* HDCP22 RNG Core */
		XHdcp22_Rng_ConfigTable[XPAR_XHDCP22_RNG_NUM_INSTANCES/2 + instance].DeviceId = RX_DEVICE_ID_BASE + instance;
		XHdcp22_Rng_ConfigTable[XPAR_XHDCP22_RNG_NUM_INSTANCES/2 + instance].BaseAddress = RX_HDCP22_RNG_OFFSET;
	}

	return 0;

error_dt:
		dev_err(xhdmi->dev, "Error parsing device tree");
		return rc;
}

static int xhdmi_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xhdmi_device *xhdmi;
	int ret;
	unsigned int index = 0;
	struct resource *res;

	const struct firmware *fw_edid;
	const char *fw_edid_name = "xilinx/xilinx-hdmi-rx-edid.bin";
	unsigned long flags;
	unsigned long axi_clk_rate;

	XV_HdmiRxSs *HdmiRxSsPtr;
	u32 Status;

	dev_info(&pdev->dev, "xlnx-hdmi-rx probed\n");
	/* allocate zeroed HDMI RX device structure */
	xhdmi = devm_kzalloc(&pdev->dev, sizeof(*xhdmi), GFP_KERNEL);
	if (!xhdmi)
		return -ENOMEM;
	/* store pointer of the real device inside platform device */
	xhdmi->dev = &pdev->dev;

	xhdmi->edid_blocks_max = 2;

	/* mutex that protects against concurrent access */
	mutex_init(&xhdmi->xhdmi_mutex);
	spin_lock_init(&xhdmi->irq_lock);
	/* work queues */
	xhdmi->work_queue = create_singlethread_workqueue("xilinx-hdmi-rx");
	if (!xhdmi->work_queue) {
		dev_info(xhdmi->dev, "Could not create work queue\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&xhdmi->delayed_work_enable_hotplug,
		xhdmi_delayed_work_enable_hotplug);

	hdmi_dbg("xhdmi_probe DT parse start\n");
	/* parse open firmware device tree data */
	ret = xhdmi_parse_of(xhdmi, &xhdmi->config);
	if (ret < 0)
		return ret;
	hdmi_dbg("xhdmi_probe DT parse done\n");

	/* acquire vphy lanes */
	for (index = 0; index < 3; index++)
	{
		char phy_name[16];
		snprintf(phy_name, sizeof(phy_name), "hdmi-phy%d", index);
		xhdmi->phy[index] = devm_phy_get(xhdmi->dev, phy_name);
		if (IS_ERR(xhdmi->phy[index])) {
			ret = PTR_ERR(xhdmi->phy[index]);
			xhdmi->phy[index] = NULL;
			if (ret == -EPROBE_DEFER) {
				hdmi_dbg("xvphy not ready -EPROBE_DEFER\n");
				return ret;
			}
			if (ret != -EPROBE_DEFER)
				dev_err(xhdmi->dev, "failed to get phy lane %s index %d, error %d\n",
					phy_name, index, ret);
			goto error_phy;
		}

		ret = phy_init(xhdmi->phy[index]);
		if (ret) {
			dev_err(xhdmi->dev,
				"failed to init phy lane %d\n", index);
			goto error_phy;
		}
	}

	/* get ownership of the HDMI RXSS MMIO register space resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* map the MMIO region */
	xhdmi->iomem = devm_ioremap_resource(xhdmi->dev, res);
	if (IS_ERR(xhdmi->iomem)) {
		ret = PTR_ERR(xhdmi->iomem);
		goto error_resource;
	}
	xhdmi->config.DeviceId = instance;
	xhdmi->config.BaseAddress = (uintptr_t)xhdmi->iomem;
	xhdmi->config.HighAddress = (uintptr_t)xhdmi->iomem + resource_size(res) - 1;

	/* Compute AbsAddress for sub-cores */
	ret = xhdmi_compute_subcore_AbsAddr(&xhdmi->config);
	if (ret == -EFAULT) {
	   dev_err(xhdmi->dev, "hdmi-rx sub-core address out-of range\n");
	   return ret;
	}

	/* video streaming bus clock */
	xhdmi->clk = devm_clk_get(xhdmi->dev, "video");
	if (IS_ERR(xhdmi->clk)) {
		ret = PTR_ERR(xhdmi->clk);
		if (ret == -EPROBE_DEFER)
			dev_info(xhdmi->dev, "video-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(xhdmi->dev, "failed to get video clk\n");
		return ret;
	}

	clk_prepare_enable(xhdmi->clk);

	/* AXI lite register bus clock */
	xhdmi->axi_lite_clk = devm_clk_get(xhdmi->dev, "axi-lite");
	if (IS_ERR(xhdmi->axi_lite_clk)) {
		ret = PTR_ERR(xhdmi->clk);
		if (ret == -EPROBE_DEFER)
			dev_info(xhdmi->dev, "axi-lite clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(xhdmi->dev, "failed to get axi-lite clk\n");
		return ret;
	}

	clk_prepare_enable(xhdmi->axi_lite_clk);
	axi_clk_rate = clk_get_rate(xhdmi->axi_lite_clk);
	hdmi_dbg("AXI Lite clock rate = %lu Hz\n", axi_clk_rate);

	/* we now know the AXI clock rate */
	XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES/2 + instance].SysFrequency = axi_clk_rate;
	XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES/2 + instance * 2 + 0].SysClockFreqHz = axi_clk_rate;
	XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES/2 + instance * 2 + 1].SysClockFreqHz = axi_clk_rate;

	/* get ownership of the HDCP1x key management MMIO register space resource */
	if (xhdmi->config.Hdcp14.IsPresent) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdcp1x-keymngmt");

		if (res) {
			hdmi_dbg("Mapping HDCP1x key management block.\n");
			xhdmi->hdcp1x_keymngmt_iomem = devm_ioremap_resource(xhdmi->dev, res);
			hdmi_dbg("HDCP1x key management block @%p.\n", xhdmi->hdcp1x_keymngmt_iomem);
			if (IS_ERR(xhdmi->hdcp1x_keymngmt_iomem)) {
				dev_err(xhdmi->dev, "Could not ioremap hdcp1x-keymngmt.\n");
				return PTR_ERR(xhdmi->hdcp1x_keymngmt_iomem);
			}
		}
	}

	/* get HDMI RXSS irq */
	xhdmi->irq = platform_get_irq(pdev, 0);
	if (xhdmi->irq <= 0) {
		dev_err(&pdev->dev, "platform_get_irq() failed\n");
		return xhdmi->irq;
	}

	if (xhdmi->config.Hdcp14.IsPresent) {
		xhdmi->hdcp1x_irq = platform_get_irq_byname(pdev, "hdcp1x");
		hdmi_dbg("xhdmi->hdcp1x_irq = %d\n", xhdmi->hdcp1x_irq);
		xhdmi->hdcp1x_timer_irq = platform_get_irq_byname(pdev, "hdcp1x-timer");
		hdmi_dbg("xhdmi->hdcp1x_timer_irq = %d\n", xhdmi->hdcp1x_timer_irq);
	}

	if (xhdmi->config.Hdcp22.IsPresent) {
		xhdmi->hdcp22_irq = platform_get_irq_byname(pdev, "hdcp22");
		hdmi_dbg("xhdmi->hdcp22_irq = %d\n", xhdmi->hdcp22_irq);
		xhdmi->hdcp22_timer_irq = platform_get_irq_byname(pdev, "hdcp22-timer");
		hdmi_dbg("xhdmi->hdcp22_timer_irq = %d\n", xhdmi->hdcp22_timer_irq);
	}

	if (xhdmi->config.Hdcp14.IsPresent || xhdmi->config.Hdcp22.IsPresent) {
	  INIT_DELAYED_WORK(&xhdmi->delayed_work_hdcp_poll, hdcp_poll_work/*function*/);
	}

	/* create sysfs group entry */
	ret = sysfs_create_group(&xhdmi->dev->kobj, &attr_group);
	if (ret) {
		dev_err(xhdmi->dev, "sysfs group creation (%d) failed \n", ret);
		return ret;
	}

	HdmiRxSsPtr = (XV_HdmiRxSs *)&xhdmi->xv_hdmirxss;
	hdmi_mutex_lock(&xhdmi->xhdmi_mutex);

	ret = devm_request_threaded_irq(&pdev->dev, xhdmi->irq, hdmirx_irq_handler, hdmirx_irq_thread,
		IRQF_TRIGGER_HIGH, "xilinx-hdmi-rx", xhdmi/*dev_id*/);

	if (ret) {
		dev_err(&pdev->dev, "unable to request IRQ %d\n", xhdmi->irq);
		hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
		goto error_phy;
	}

	/* HDCP 1.4 Cipher interrupt */
	if (xhdmi->hdcp1x_irq > 0) {
		/* Request the HDCP14 interrupt */
		ret = devm_request_threaded_irq(&pdev->dev, xhdmi->hdcp1x_irq, hdmirx_hdcp_irq_handler, hdmirx_hdcp_irq_thread,
			IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmirxss-hdcp1x-cipher", xhdmi/*dev_id*/);
		if (ret) {
			dev_err(&pdev->dev, "unable to request IRQ %d\n", xhdmi->hdcp1x_irq);
			hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
			return ret;
		}
	}

	/* HDCP 1.4 Timer interrupt */
	if (xhdmi->hdcp1x_timer_irq > 0) {
		/* Request the HDCP14 interrupt */
		ret = devm_request_threaded_irq(&pdev->dev, xhdmi->hdcp1x_timer_irq, hdmirx_hdcp_irq_handler, hdmirx_hdcp_irq_thread,
			IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmirxss-hdcp1x-timer", xhdmi/*dev_id*/);
		if (ret) {
			dev_err(&pdev->dev, "unable to request IRQ %d\n", xhdmi->hdcp1x_timer_irq);
			hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
			return ret;
		}
	}

	/* HDCP 2.2 interrupt, unused currently */
#if 0
	if (xhdmi->hdcp22_irq > 0) {
		/* Request the HDCP22 interrupt */
		ret = devm_request_threaded_irq(&pdev->dev, xhdmi->hdcp22_irq, hdmirx_hdcp_irq_handler, hdmirx_hdcp_irq_thread,
			IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmirxss-hdcp22", xhdmi/*dev_id*/);
		if (ret) {
			dev_err(&pdev->dev, "unable to request IRQ %d\n", xhdmi->hdcp22_irq);
				hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
			hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
			return ret;
		}
	}
#endif
	/* HDCP 2.2 Timer interrupt */
	if (xhdmi->hdcp22_timer_irq > 0) {
		/* Request the HDCP22 timer interrupt */
		ret = devm_request_threaded_irq(&pdev->dev, xhdmi->hdcp22_timer_irq, hdmirx_hdcp_irq_handler, hdmirx_hdcp_irq_thread,
			IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmirxss-hdcp22-timer", xhdmi/*dev_id*/);
		if (ret) {
			dev_err(&pdev->dev, "unable to request IRQ %d\n", xhdmi->hdcp22_timer_irq);
				hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
			hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);

			return ret;
		}
	}

	/* sets pointer to the EDID used by XV_HdmiRxSs_LoadDefaultEdid() */
	XV_HdmiRxSs_SetEdidParam(HdmiRxSsPtr, (u8 *)&xilinx_edid[0], sizeof(xilinx_edid));

	/* Initialize top level and all included sub-cores */
	Status = XV_HdmiRxSs_CfgInitialize(HdmiRxSsPtr, &xhdmi->config, (uintptr_t)xhdmi->iomem);
	if (Status != XST_SUCCESS) {
		dev_err(xhdmi->dev, "initialization failed with error %d\n", Status);
		return -EINVAL;
	}

	/* disable interrupts */
	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	XV_HdmiRxSs_IntrDisable(HdmiRxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	/* retrieve EDID */
	if (request_firmware(&fw_edid, fw_edid_name, xhdmi->dev) == 0) {
		int blocks = fw_edid->size / 128;
		if ((blocks == 0) || (blocks > xhdmi->edid_blocks_max) || (fw_edid->size % 128)) {
			dev_err(xhdmi->dev, "%s must be n * 128 bytes, with 1 <= n <= %d, using Xilinx built-in EDID instead.\n",
				fw_edid_name, xhdmi->edid_blocks_max);
		} else {
			memcpy(xhdmi->edid_user, fw_edid->data, 128 * blocks);
			xhdmi->edid_user_blocks = blocks;
		}
	}
	release_firmware(fw_edid);

	if (xhdmi->edid_user_blocks) {
		dev_info(xhdmi->dev, "Using %d EDID block%s (%d bytes) from '%s'.\n",
			xhdmi->edid_user_blocks, xhdmi->edid_user_blocks > 1? "s":"", 128 * xhdmi->edid_user_blocks, fw_edid_name);
		XV_HdmiRxSs_LoadEdid(HdmiRxSsPtr, (u8 *)&xhdmi->edid_user, 128 * xhdmi->edid_user_blocks);
	} else {
		dev_info(xhdmi->dev, "Using Xilinx built-in EDID.\n");
		XV_HdmiRxSs_LoadDefaultEdid(HdmiRxSsPtr);
	}

	/* RX SS callback setup (from xapp1287/xhdmi_example.c:2146) */
	XV_HdmiRxSs_SetCallback(HdmiRxSsPtr, XV_HDMIRXSS_HANDLER_CONNECT,
		RxConnectCallback, (void *)xhdmi);
	XV_HdmiRxSs_SetCallback(HdmiRxSsPtr, XV_HDMIRXSS_HANDLER_STREAM_DOWN,
		RxStreamDownCallback, (void *)xhdmi);
	XV_HdmiRxSs_SetCallback(HdmiRxSsPtr, XV_HDMIRXSS_HANDLER_STREAM_INIT,
		RxStreamInitCallback, (void *)xhdmi);
	XV_HdmiRxSs_SetCallback(HdmiRxSsPtr, XV_HDMIRXSS_HANDLER_STREAM_UP,
		RxStreamUpCallback, (void *)xhdmi);

	/* get a reference to the XVphy data structure */
	xhdmi->xvphy = xvphy_get_xvphy(xhdmi->phy[0]);

	BUG_ON(!xhdmi->xvphy);

	xvphy_mutex_lock(xhdmi->phy[0]);
	/* the callback is not specific to a single lane, but we need to
	 * provide one of the phy's as reference */
	XVphy_SetHdmiCallback(xhdmi->xvphy, XVPHY_HDMI_HANDLER_RXINIT,
		VphyHdmiRxInitCallback, (void *)xhdmi);
	XVphy_SetHdmiCallback(xhdmi->xvphy, XVPHY_HDMI_HANDLER_RXREADY,
		VphyHdmiRxReadyCallback, (void *)xhdmi);

	xvphy_mutex_unlock(xhdmi->phy[0]);

	platform_set_drvdata(pdev, xhdmi);

	/* Initialize V4L2 subdevice */
	subdev = &xhdmi->subdev;
	v4l2_subdev_init(subdev, &xhdmi_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xhdmi_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xhdmi);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	/* Initialize V4L2 media entity */
	xhdmi->pad.flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xhdmi_media_ops;
	ret = media_entity_pads_init(&subdev->entity, 1/*npads*/, &xhdmi->pad);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to init media entity\n");
		hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
		goto error_irq;
	}

	v4l2_ctrl_handler_init(&xhdmi->ctrl_handler, 0/*controls*/);
	subdev->ctrl_handler = &xhdmi->ctrl_handler;
	ret = v4l2_ctrl_handler_setup(&xhdmi->ctrl_handler);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to set controls\n");
		hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
		goto error_irq;
	}

	/* assume detected format */
	xhdmi->detected_format.width = 1280;
	xhdmi->detected_format.height = 720;
	xhdmi->detected_format.field = V4L2_FIELD_NONE;
	xhdmi->detected_format.colorspace = V4L2_COLORSPACE_REC709;
	xhdmi->detected_format.code = MEDIA_BUS_FMT_RBG888_1X24;
	xhdmi->detected_format.colorspace = V4L2_COLORSPACE_SRGB;
	xhdmi->detected_format.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	xhdmi->detected_format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	xhdmi->detected_format.quantization = V4L2_QUANTIZATION_DEFAULT;

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);
		goto error;
	}

	hdmi_mutex_unlock(&xhdmi->xhdmi_mutex);

	/* enable interrupts */
	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	XV_HdmiRxSs_IntrEnable(HdmiRxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);
	
	/* probe has succeeded for this instance, increment instance index */
	instance++;
    dev_info(xhdmi->dev, "hdmi-rx probe successful\n");

	/* return success */
	return 0;

error:
	v4l2_ctrl_handler_free(&xhdmi->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
error_irq:

error_phy:
	printk(KERN_INFO "xhdmirx_probe() error_phy:\n");
	index = 0;
	/* release the lanes that we did get, if we did not get all lanes */
	if (xhdmi->phy[index]) {
		printk(KERN_INFO "phy_exit() xhdmi->phy[%d] = %p\n", index, xhdmi->phy[index]);
		phy_exit(xhdmi->phy[index]);
		xhdmi->phy[index] = NULL;
	}
error_resource:
	printk(KERN_INFO "xhdmirx_probe() error_resource:\n");
	return ret;
}

static int xhdmi_remove(struct platform_device *pdev)
{
	struct xhdmi_device *xhdmi = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xhdmi->subdev;
	unsigned long flags;

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	XV_HdmiRxSs_IntrDisable(&xhdmi->xv_hdmirxss);
	xhdmi->teardown = 1;
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	cancel_delayed_work(&xhdmi->delayed_work_enable_hotplug);
	destroy_workqueue(xhdmi->work_queue);

	sysfs_remove_group(&pdev->dev.kobj, &attr_group);
	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xhdmi->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	clk_disable_unprepare(xhdmi->clk);
	hdmi_dbg("removed.\n");
	return 0;
}

static SIMPLE_DEV_PM_OPS(xhdmi_pm_ops, xhdmi_pm_suspend, xhdmi_pm_resume);

static const struct of_device_id xhdmi_of_id_table[] = {
	{ .compatible = "xlnx,v-hdmi-rx-ss-3.0" },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xhdmi_of_id_table);

static struct platform_driver xhdmi_driver = {
	.driver = {
		.name		= "xilinx-hdmi-rx",
		.pm		= &xhdmi_pm_ops,
		.of_match_table	= xhdmi_of_id_table,
	},
	.probe			= xhdmi_probe,
	.remove			= xhdmi_remove,
};

module_platform_driver(xhdmi_driver);

MODULE_DESCRIPTION("Xilinx HDMI RXSS V4L2 driver");
MODULE_AUTHOR("Leon Woestenberg <leon@sidebranch.com>");
MODULE_LICENSE("GPL v2");

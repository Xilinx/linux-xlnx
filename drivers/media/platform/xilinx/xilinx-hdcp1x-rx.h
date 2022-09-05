/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx HDCP1X driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 *
 * Author: Jagadeesh Banisetti <jagadeesh.banisetti@xilinx.com>
 */

#ifndef __XILINX_HDCP1X_RX_H__
#define __XILINX_HDCP1X_RX_H__

#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>

/* HDCP1X over a specified protocol */
enum xhdcp1x_rx_protocol {
	XHDCP1X_NONE = 0,
	XHDCP1X_DP = 1,
	XHDCP1X_HDMI = 2
};

enum xhdcp1x_rx_events {
	XHDCP1X_RX_AKSV_RCVD = 0x01,
	XHDCP1X_RX_RO_PRIME_READ_DONE = 0x02,
	XHDCP1X_RX_CIPHER_EVENT_RCVD = 0x04
	/* TODO: Add for HDMI events */
};

enum xhdcp1x_rx_notifications {
	XHDCP1X_RX_NOTIFY_AUTHENTICATED = 1,
	XHDCP1X_RX_NOTIFY_UN_AUTHENTICATED = 2,
	XHDCP1X_RX_NOTIFY_SET_CP_IRQ = 3
};

enum xhdcp1x_rx_handler_type {
	XHDCP1X_RX_RD_HANDLER = 1,
	XHDCP1X_RX_WR_HANDLER = 2,
	XHDCP1X_RX_NOTIFICATION_HANDLER = 3,
};

#if IS_ENABLED(CONFIG_VIDEO_XILINX_HDCP1X_RX)
void *xhdcp1x_rx_init(struct device *dev, void *interface_ref,
		      void __iomem *interface_base, bool is_repeater);
int xhdcp1x_rx_enable(void *ref, u8 lane_count);
int xhdcp1x_rx_disable(void *ref);
int xhdcp1x_rx_set_callback(void *ref, u32 handler_type, void *handler);
int xhdcp1x_rx_handle_intr(void *ref);
int xhdcp1x_rx_push_events(void *ref, u32 events);
int xhdcp1x_rx_set_keyselect(void *ref, u8 keyselect);
int xhdcp1x_rx_load_bksv(void *ref);
#else
static inline void *xhdcp1x_rx_init(struct device *dev, void *interface_ref,
				    void __iomem *interface_base, bool is_repeater)
{
	return ERR_PTR(-EINVAL);
}

static inline int xhdcp1x_rx_enable(void *ref, u8 lane_count)
{
	return -EINVAL;
}

static inline int xhdcp1x_rx_disable(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_rx_set_callback(void *ref, u32 handler_type, void *handler)
{
	return -EINVAL;
}

static inline int xhdcp1x_rx_handle_intr(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_rx_push_events(void *ref, u32 events)
{
	return -EINVAL;
}

static inline int xhdcp1x_rx_set_keyselect(void *ref, u8 keyselect)
{
	return -EINVAL;
}

static inline int xhdcp1x_rx_load_bksv(void *ref)
{
	return -EINVAL;
}
#endif /* CONFIG_VIDEO_XILINX_HDCP1X_RX */
#endif /* __XILINX_HDCP1X_RX_H__ */

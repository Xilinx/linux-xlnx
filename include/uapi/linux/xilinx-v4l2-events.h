/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx V4L2 SCD Driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *
 */

#ifndef __UAPI_XILINX_V4L2_EVENTS_H__
#define __UAPI_XILINX_V4L2_EVENTS_H__

#include <linux/videodev2.h>

/*
 * Events
 *
 * V4L2_EVENT_XLNXSCD: Scene Change Detection
 */
#define V4L2_EVENT_XLNXSCD_CLASS	(V4L2_EVENT_PRIVATE_START | 0x300)
#define V4L2_EVENT_XLNXSCD		(V4L2_EVENT_XLNXSCD_CLASS | 0x1)

#endif /* __UAPI_XILINX_V4L2_EVENTS_H__ */

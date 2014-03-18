/*
 * Xilinx Video IP Pipeline
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __XILINX_VIPP_H__
#define __XILINX_VIPP_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#include "xilinx-dma.h"

/**
 * struct xvip_pipeline - Xilinx Video IP device structure
 * @v4l2_dev: V4L2 device
 * @media_dev: media device
 * @pipe: media pipeline
 * @dev: (OF) device
 * @notifier: V4L2 asynchronous subdevs notifier
 * @entities: entities in the pipeline as a list of xvip_pipeline_entity
 * @num_subdevs: number of subdevs in the pipeline
 * @dma: DMA channels at the pipeline output and input
 * @num_dmas: number of DMA engines in the pipeline
 * @lock: protects the pipeline @stream_count
 * @stream_count: number of DMA engines currently streaming
 * @ctrl_handler: control handler
 */
struct xvip_pipeline {
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct media_pipeline pipe;
	struct device *dev;

	struct v4l2_async_notifier notifier;
	struct list_head entities;
	unsigned int num_subdevs;

	struct xvip_dma dma[2];
	unsigned int num_dmas;

	struct mutex lock;
	unsigned int stream_count;

	struct v4l2_ctrl_handler ctrl_handler;
};

int xvip_pipeline_set_stream(struct xvip_pipeline *xvipp, bool on);

#endif /* __XILINX_VIPP_H__ */

/*
 *  imageon_rx driver internal defines and structures
 *
 *  Derived from cx18-driver.h
 *
 *  Copyright 2011 Tandberg Telecom AS.  All rights reserved.
 *
 *  This program is free software; you may redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef IMAGEON_RX_DRIVER_H
#define IMAGEON_RX_DRIVER_H

#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/amba/xilinx_dma.h>
#include <linux/debugfs.h>

#include <linux/i2c.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-dma-sg.h>

struct imageon_rx_stream {
	struct video_device vdev;
	struct media_pad pad;
	struct vb2_queue q;
	struct i2c_adapter *i2c_adap;
	struct v4l2_subdev *sd_adv7611;
	struct mutex lock;
	spinlock_t spinlock;
	u32 input;
	u32 width, height, bpp, pack_fmt;
	u32 stride;

	struct dma_chan *chan;
	struct xilinx_vdma_config dma_config;

	struct list_head queued_buffers;
};

/* Struct to hold info about imageon_rx cards */
struct imageon_rx {
	struct v4l2_device v4l2_dev;
	struct vb2_alloc_ctx	*alloc_ctx;

	/* device nodes */
	struct media_device mdev;
	struct imageon_rx_stream stream;

	int hotplug_gpio;

	void __iomem *base;

	u8 edid_data[256];
};

static inline struct imageon_rx *to_imageon_rx(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct imageon_rx, v4l2_dev);
}

static inline struct imageon_rx_stream
			*imageon_rx_file_to_stream(struct file *file)
{
	struct imageon_rx *imageon_rx = video_drvdata(file);
	return &imageon_rx->stream;
}

static inline struct imageon_rx *imageon_rx_stream_to_imageon_rx(
	struct imageon_rx_stream *s)
{
	return container_of(s, struct imageon_rx, stream);
}

int imageon_rx_nodes_register(struct imageon_rx *imageon_rx);

#define IMAGEON_RX_BYTES_PER_PIXEL_YUYV 4
#define IMAGEON_RX_BYTES_PER_PIXEL_RGB32 4

enum {
	IMAGEON_RX_VID_PACK_FMT_RGB32,
/*
	IMAGEON_RX_VID_PACK_FMT_YUYV,*/
};

#endif

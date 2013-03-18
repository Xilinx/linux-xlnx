/*
 * Analog Devices ADV7511 HDMI Transmitter Device Driver
 *
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ADV7511_H
#define ADV7511_H

/* notify events */
#define ADV7511_MONITOR_DETECT 0
#define ADV7511_EDID_DETECT 1

#define VIDIOC_SUBDEV_G_EDID _IOWR('V', 192, struct v4l2_subdev_edid)

struct v4l2_subdev_edid {
	/* Pad for which to get/set the EDID blocks. */
	__u32 pad;
	/*
		Read the EDID from starting with this block.
		Must be 0 when setting the EDID.
	*/
	__u32 start_block;
	/*
		The number of blocks to get or set.
		Must be less or equal to 256 (the maximum number of blocks as defined
		by the standard). When you set the EDID and blocks is 0, then the EDID
		is disabled or erased.
	*/
	__u32 blocks;
	/*
		Pointer to memory that contains the EDID.
		The minimum size is blocks * 128.
	*/
	__u8 *edid;
	/*
		Reserved for future extensions.
		Applications and drivers must set the array to zero.
	*/
	__u32 reserved[5];
};

struct adv7511_monitor_detect {
	int present;
};

struct adv7511_edid_detect {
	int present;
	int segment;
};

struct adv7511_platform_data {
	uint8_t edid_addr;
	/* I/O expander on ADI adv7511 ez-extender board */
	uint8_t i2c_ex;
};


extern struct v4l2_subdev *adv7511_subdev(struct v4l2_subdev *sd);

#endif

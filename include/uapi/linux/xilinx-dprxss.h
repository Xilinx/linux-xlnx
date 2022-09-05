/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __XILINX_DPRXSS_H__
#define __XILINX_DPRXSS_H__

#include <linux/types.h>

/**
 * struct xdprxss_hdcp1x_keys_ioctl - Hdcp1x Keys structure
 * @size: size of the keys
 * @keys: Pointer to the keys buffer
 */
struct xdprxss_hdcp1x_keys_ioctl {
	__u32 size;
	void const *keys;
};

/* This ioctl is used to set the hdcp1x keys into IP */
#define XILINX_DPRXSS_HDCP_KEY_WRITE \
	_IOW('X', BASE_VIDIOC_PRIVATE + 0, struct xdprxss_hdcp1x_keys_ioctl)

#endif

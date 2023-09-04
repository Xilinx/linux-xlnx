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

struct xdprxss_hdcp2x_keys_ioctl {
	__u32 size_lc128;
	__u32 size_private;
	void const *key_lc128;
	void const *key_private;
};

/* This ioctl is used to set the hdcp1x keys into IP */
#define XILINX_DPRXSS_HDCP_KEY_WRITE \
	_IOW('X', BASE_VIDIOC_PRIVATE + 0, struct xdprxss_hdcp1x_keys_ioctl)

#define XILINX_DPRXSS_HDCP2X_KEY_WRITE \
	_IOW('X', BASE_VIDIOC_PRIVATE + 1, struct xdprxss_hdcp2x_keys_ioctl)

#endif

/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __XILINX_HDMIRXSS_H__
#define __XILINX_HDMIRXSS_H__

#include <linux/types.h>

/**
 * struct xhdmirxss_hdcp1x_keys_ioctl - HDCP 1x Keys structure
 * @size: size of the keys
 * @keys: Pointer to the keys buffer
 */
struct xhdmirxss_hdcp1x_keys_ioctl {
	__u32 size;
	void const *keys;
};

/* This ioctl is used to set the HDCP1x keys into IP */
#define XILINX_HDMIRXSS_HDCP_KEY_WRITE \
	_IOW('X', BASE_VIDIOC_PRIVATE + 1, struct xhdmirxss_hdcp1x_keys_ioctl)

#endif

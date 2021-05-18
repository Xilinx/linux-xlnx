/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __XILINX_HDMIRXSS_H__
#define __XILINX_HDMIRXSS_H__

#include <linux/types.h>

#define XHDCP_IOCTL	'X'

/**
 * struct xhdmirxss_hdcp1x_keys_ioctl - HDCP 1x Keys structure
 * @size: size of the keys
 * @keys: Pointer to the keys buffer
 */
struct xhdmirxss_hdcp1x_keys_ioctl {
	__u32 size;
	void const *keys;
};

/**
 * struct xhdmirxss_hdcp2x_keys_ioctl - HDCP 2x Keys structure
 * @lc128key: Pointer to the lc128 buffer
 * @privatekey: Pointer to the privatekey
 */
/* Reference:
 * https://www.digital-cp.com/sites/default/files/specifications/HDCP%20on%20HDMI%20Specification%20Rev2_3.pdf
 */
struct xhdmirxss_hdcp2x_keys_ioctl {
	void const *lc128key;
	void const *privatekey;
};

/* This ioctl is used to set the HDCP1x keys into IP */
#define XILINX_HDMIRXSS_HDCP_KEY_WRITE \
	_IOW(XHDCP_IOCTL, BASE_VIDIOC_PRIVATE + 1, struct xhdmirxss_hdcp1x_keys_ioctl)

/* This ioctl is used to set the HDCP2x keys into IP */
#define XILINX_HDMIRXSS_HDCP22_KEY_WRITE \
	_IOW(XHDCP_IOCTL, BASE_VIDIOC_PRIVATE + 2, struct xhdmirxss_hdcp2x_keys_ioctl)

#endif

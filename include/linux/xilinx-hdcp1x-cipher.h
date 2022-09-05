/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx HDCP1X Cipher driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 *
 * Author: Jagadeesh Banisetti <jagadeesh.banisetti@xilinx.com>
 */

#ifndef __XILINX_HDCP1X_CIPHER_H__
#define __XILINX_HDCP1X_CIPHER_H__

#include <linux/device.h>
#include <linux/io.h>
#include <linux/types.h>

#if IS_ENABLED(CONFIG_XLNX_HDCP1X_CIPHER)
void *xhdcp1x_cipher_init(struct device *dev, void __iomem *hdcp1x_base);
int xhdcp1x_cipher_reset(void *ref);
int xhdcp1x_cipher_enable(void *ref);
int xhdcp1x_cipher_disable(void *ref);
int xhdcp1x_cipher_set_num_lanes(void *ref, u8 num_lanes);
int xhdcp1x_cipher_set_keyselect(void *ref, u8 keyselect);
int xhdcp1x_cipher_load_bksv(void *ref, u8 *buf);
int xhdcp1x_cipher_set_remoteksv(void *ref, u64 ksv);
int xhdcp1x_cipher_get_ro(void *ref, u16 *ro);
int xhdcp1x_cipher_set_b(void *ref, u64 an, bool is_repeater);
int xhdcp1x_cipher_is_request_complete(void *ref);
int xhdcp1x_cipher_set_link_state_check(void *ref, bool is_enabled);
int xhdcp1x_cipher_get_interrupts(void *ref, u32 *interrupts);
int xhdcp1x_cipher_is_linkintegrity_failed(void *ref);
#else
static inline void *xhdcp1x_cipher_init(struct device *dev,
					void __iomem *hdcp1x_base)
{
	return NULL;
}

static inline int xhdcp1x_cipher_reset(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_enable(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_disable(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_set_num_lanes(void *ref, u8 num_lanes)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_set_keyselect(void *ref, u8 keyselect)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_load_bksv(void *ref, u8 *buf)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_set_remoteksv(void *ref, u64 ksv)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_get_ro(void *ref, u16 *ro)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_set_b(void *ref, u64 value, bool is_repeater)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_is_request_complete(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_set_link_state_check(void *ref, u8 is_enabled)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_get_interrupts(void *ref, u32 *interrupts)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_is_linkintegrity_failed(void *ref)
{
	return -EINVAL;
}
#endif /* CONFIG_XLNX_HDCP1X_CIPHER */

#endif /* __XILINX_HDCP1X_CIPHER_H__ */

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

/* HDCP Cipher register offsets */
#define XHDCP1X_CIPHER_REG_VERSION		0x00
#define XHDCP1X_CIPHER_REG_TYPE			0x04
#define XHDCP1X_CIPHER_REG_SCRATCH		0x08
#define XHDCP1X_CIPHER_REG_CONTROL		0x0C
#define XHDCP1X_CIPHER_REG_STATUS		0x10
#define XHDCP1X_CIPHER_REG_INTERRUPT_MASK	0x14
#define XHDCP1X_CIPHER_REG_INTERRUPT_STATUS	0x18
#define XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H	0x20
#define XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L	0x24
#define XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL	0x2C
#define XHDCP1X_CIPHER_REG_KEYMGMT_STATUS	0x30
#define XHDCP1X_CIPHER_REG_KSV_LOCAL_H		0x38
#define XHDCP1X_CIPHER_REG_KSV_LOCAL_L		0x3C
#define XHDCP1X_CIPHER_REG_KSV_REMOTE_H		0x40
#define XHDCP1X_CIPHER_REG_KSV_REMOTE_L		0x44
#define XHDCP1X_CIPHER_REG_KM_H			0x48
#define XHDCP1X_CIPHER_REG_KM_L			0x4C
#define XHDCP1X_CIPHER_REG_CIPHER_CONTROL	0x50
#define XHDCP1X_CIPHER_REG_CIPHER_STATUS	0x54
#define XHDCP1X_CIPHER_REG_CIPHER_BX		0x58
#define XHDCP1X_CIPHER_REG_CIPHER_BY		0x5C
#define XHDCP1X_CIPHER_REG_CIPHER_BZ		0x60
#define XHDCP1X_CIPHER_REG_CIPHER_KX		0x64
#define XHDCP1X_CIPHER_REG_CIPHER_KY		0x68
#define XHDCP1X_CIPHER_REG_CIPHER_KZ		0x6C
#define XHDCP1X_CIPHER_REG_CIPHER_MI_H		0x70
#define XHDCP1X_CIPHER_REG_CIPHER_MI_L		0x74
#define XHDCP1X_CIPHER_REG_CIPHER_RI		0x78
#define XHDCP1X_CIPHER_REG_CIPHER_RO		0x7C
#define XHDCP1X_CIPHER_REG_CIPHER_MO_H		0x80
#define XHDCP1X_CIPHER_REG_CIPHER_MO_L		0x84
#define XHDCP1X_CIPHER_REG_BLANK_VALUE		0xBC
#define XHDCP1X_CIPHER_REG_BLANK_SEL		0xC0

/* HDCP Cipher register bit mask definitions */
#define XHDCP1X_CIPHER_BITMASK_TYPE_PROTOCOL			GENMASK(1, 0)
#define XHDCP1X_CIPHER_BITMASK_TYPE_DIRECTION			BIT(2)
#define XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE			BIT(0)
#define XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE			BIT(1)
#define XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES		GENMASK(6, 4)
#define XHDCP1X_CIPHER_BITMASK_CONTROL_RESET			BIT(31)
#define XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL		BIT(0)
#define XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE		BIT(1)
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_LOCAL_KSV	BIT(0)
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_BEGIN_KM		BIT(1)
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_ABORT_KM		BIT(2)
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_SET_SELECT	GENMASK(18, 16)
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KSV_READY		BIT(0)
#define XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KM_READY		BIT(1)
#define XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE	BIT(0)
#define XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_REQUEST		GENMASK(10, 8)
#define XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_XOR_IN_PROG	BIT(0)
#define XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_REQUEST_IN_PROG	GENMASK(10, 8)
#define XHDCP1X_CIPHER_BITMASK_BLANK_VALUE			GENMASK(31, 0)
#define XHDCP1X_CIPHER_BITMASK_BLANK_SEL			BIT(0)

/* HDCP Cipher register bit value definitions */
#define XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_DP			0
#define XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_HDMI			1
#define XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_MASK		BIT(2)
#define XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_RX			0
#define XHDCP1X_CIPHER_VALUE_TYPE_DIRECTION_TX			1
#define XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_BLOCK	BIT(8)
#define XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_REKEY	BIT(9)
#define XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_RNG		BIT(10)

#define XHDCP1X_CIPHER_SIZE_LOCAL_KSV				5
#define XHDCP1X_CIPHER_KSV_RETRIES				1024
#define XHDCP1X_CIPHER_SHIFT_NUM_LANES				4
#define XHDCP1X_CIPHER_MAX_LANES				4
#define XHDCP1X_CIPHER_INTR_ALL					GENMASK(31, 0)
#define XHDCP1X_CIPHER_KEYSELECT_MAX_VALUE			8
#define XHDCP1X_CIPHER_SHIFT_KEYMGMT_CONTROL_SET_SELECT		16
#define XHDCP1X_CIPHER_NUM_LANES_1				1
#define XHDCP1X_CIPHER_NUM_LANES_2				2
#define XHDCP1X_CIPHER_NUM_LANES_4				4
#define XHDCP1X_CIPHER_BITMASK_CIPHER_BX			GENMASK(27, 0)
#define XHDCP1X_CIPHER_BITMASK_CIPHER_BY			GENMASK(27, 0)
#define XHDCP1X_CIPHER_BITMASK_CIPHER_BZ_REPEATER		BIT(8)
#define XHDCP1X_CIPHER_BITMASK_CIPHER_BZ			GENMASK(7, 0)
#define XHDCP1X_CIPHER_SHIFT_CIPHER_B				28
#define XHDCP1X_CIPHER_VALUE_SHIFT				32
#define XHDCP1X_CIPHER_DWORD_VALUE				0xFFFFFFFFul
#define XHDCP1X_CIPHER_SET_B					0x0FFFFFFFul
#define XHDCP1X_CIPHER_DEFAULT_STREAMMAP			0x01ul
#define XHDCP1X_CIPHER_KSV_VAL					0xFF

enum xhdcp1x_cipher_request_type {
	XHDCP1X_CIPHER_REQUEST_BLOCK = 0,
	XHDCP1X_CIPHER_REQUEST_REKEY = 1,
	XHDCP1X_CIPHER_REQUEST_RNG = 2,
	XHDCP1X_CIPHER_REQUEST_MAX = 3,
};

/**
 * struct xhdcp1x_cipher - hdcp1x cipher driver structure
 * @interface_base: iommu base of interface driver
 * @dev: Platform data
 * @is_tx: flag for tx, 1 for tx and 0 for rx
 * @is_hdmi: flag for HDMI, 1 for HDMI and 0 for DP
 * @num_lanes: number of active lanes in interface driver, possible lanes are 1, 2 and 4
 */
struct xhdcp1x_cipher {
	void __iomem *interface_base;
	struct device *dev;
	u8 is_tx;
	u8 is_hdmi;
	u8 num_lanes;
};

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
int xhdcp1x_cipher_set_ri(void *ref, bool enable);
int xhdcp1x_cipher_is_request_to_change_ri(void *ref);
int xhdcp1x_cipher_get_ri(void *ref, u16 *ri);
int xhdcp1x_cipher_load_aksv(void *ref, u8 *buf);
int xhdcp1x_cipher_do_request(void *ref, enum xhdcp1x_cipher_request_type request);
u64 xhdcp1x_cipher_get_localksv(struct xhdcp1x_cipher *cipher);
int xhdcp1x_cipher_getencryption(void *ref);
int xhdcp1x_cipher_disableencryption(void *ref, u64 streammap);
int xhdcp1x_cipher_setb(void *ref, u32 bx, u32 by, u32 bz);
int xhdcp1x_cipher_enable_encryption(void *ref, u64 streammap);
int xhdcp1x_cipher_set_ri_update(void *ref, int is_enabled);
u64 xhdcp1x_cipher_get_mi(void *ref);
u64 xhdcp1x_cipher_get_mo(void *ref);
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

static inline int xhdcp1x_cipher_get_ri(void *ref, u16 *ri)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_is_request_to_change_ri(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_set_ri(void *ref, bool enable)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_load_aksv(void *ref, u8 *buf)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_do_request(void *ref, enum xhdcp1x_cipher_request_type request)
{
	return -EINVAL;
}

static inline u64 xhdcp1x_cipher_get_localksv(struct xhdcp1x_cipher *cipher)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_getencryption(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_disableencryption(void *ref, u64 streammap)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_setb(void *ref, u32 bx, u32 by, u32 bz)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_enable_encryption(void *ref, u64 streammap)
{
	return -EINVAL;
}

static inline u64 xhdcp1x_cipher_get_mi(void *ref)
{
	return -EINVAL;
}

static inline u64 xhdcp1x_cipher_get_mo(void *ref)
{
	return -EINVAL;
}

static inline int xhdcp1x_cipher_set_ri_update(void *ref, int is_enabled)
{
	return -EINVAL;
}
#endif /* CONFIG_XLNX_HDCP1X_CIPHER */

#endif /* __XILINX_HDCP1X_CIPHER_H__ */

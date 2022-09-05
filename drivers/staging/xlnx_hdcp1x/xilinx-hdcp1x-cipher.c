// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP1X Cipher driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 *
 * Author: Jagadeesh Banisetti <jagadeesh.banisetti@xilinx.com>
 */

#include <linux/xilinx-hdcp1x-cipher.h>

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

/********************** Static function definations ***************************/
static void xhdcp1x_cipher_write(struct xhdcp1x_cipher *cipher,
				 int offset, u32 val)
{
	writel(val, cipher->interface_base + offset);
}

static u32 xhdcp1x_cipher_read(struct xhdcp1x_cipher *cipher, int offset)
{
	return readl(cipher->interface_base + offset);
}

static void xhdcp1x_cipher_set_mask(struct xhdcp1x_cipher *cipher, int offset,
				    u32 set_mask)
{
	u32 value;

	value = xhdcp1x_cipher_read(cipher, offset);
	value |= set_mask;
	xhdcp1x_cipher_write(cipher, offset, value);
}

static void xhdcp1x_cipher_clr_mask(struct xhdcp1x_cipher *cipher, int offset,
				    u32 clr_mask)
{
	u32 value;

	value = xhdcp1x_cipher_read(cipher, offset);
	value &= ~clr_mask;
	xhdcp1x_cipher_write(cipher, offset, value);
}

static int xhdcp1x_cipher_is_enabled(struct xhdcp1x_cipher *cipher)
{
	return (xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL) &
			XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE);
}

static u8 xhdcp1x_cipher_is_localksv_ready(struct xhdcp1x_cipher *cipher)
{
	return (xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_KEYMGMT_STATUS) &
				    XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KSV_READY);
}

static u8 xhdcp1x_cipher_is_km_ready(struct xhdcp1x_cipher *cipher)
{
	return (xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_KEYMGMT_STATUS) &
				    XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KM_READY);
}

static u64 xhdcp1x_cipher_get_localksv(struct xhdcp1x_cipher *cipher)
{
	u64 ksv;
	u32 val;
	u32 guard = XHDCP1X_CIPHER_KSV_RETRIES;

	if (!xhdcp1x_cipher_is_enabled(cipher))
		return 0;

	/* Check if the local ksv is not available */
	val = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_KEYMGMT_STATUS);
	val &= XHDCP1X_CIPHER_BITMASK_KEYMGMT_STATUS_KSV_READY;

	if (val)
		return 0;

	/* Abort any running KM calculation just in case */
	xhdcp1x_cipher_set_mask(cipher,
				XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL,
				XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_ABORT_KM);
	xhdcp1x_cipher_clr_mask(cipher,
				XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL,
				XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_ABORT_KM);

	/* Load the local ksv */
	xhdcp1x_cipher_set_mask(cipher,
				XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL,
				XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_LOCAL_KSV);
	xhdcp1x_cipher_clr_mask(cipher,
				XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL,
				XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_LOCAL_KSV);

	while ((!xhdcp1x_cipher_is_localksv_ready(cipher)) && (--guard > 0))
		;

	if (!guard)
		return 0;

	ksv = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_KSV_LOCAL_H);
	ksv = (ksv & 0xFF) << 32;
	ksv |= xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_KSV_LOCAL_L);

	return ksv;
}

static void xhdcp1x_cipher_config_lanes(struct xhdcp1x_cipher *cipher)
{
	u32 value;

	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL);
	value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES;
	value |= FIELD_PREP(XHDCP1X_CIPHER_BITMASK_CONTROL_NUM_LANES,
			   cipher->num_lanes);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CONTROL, value);
}

static int xhdcp1x_cipher_do_request(void *ref,
				     enum xhdcp1x_cipher_request_type request)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value;

	if (!cipher)
		return -EINVAL;

	if (request < XHDCP1X_CIPHER_REQUEST_BLOCK ||
	    request >= XHDCP1X_CIPHER_REQUEST_MAX)
		return -EINVAL;

	if (!xhdcp1x_cipher_is_enabled(cipher))
		return -EINVAL;

	/* Determine if there is a request in progress */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_STATUS);
	value &= XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_REQUEST_IN_PROG;
	if (value)
		return -EBUSY;

	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	/* Set the appropriate request bit and ensure that KM is always used */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	value &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_REQUEST;
	value |= (XHDCP1X_CIPHER_VALUE_CIPHER_CONTROL_REQUEST_BLOCK << request);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL, value);

	/* Ensure that the request bit(s) get cleared for next time */
	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_REQUEST);

	return 0;
}

/********************** Public function definitions ***************************/
/**
 * xhdcp1x_cipher_init - Create and initialize the cipher driver instance
 * @dev: Pointer to platform structure
 * @hdcp1x_base: Pointer to interface iomem base
 * This function instantiate the cipher driver and initializes it.
 *
 * Return: void reference to cipher driver instance on success, error otherwise
 */
void *xhdcp1x_cipher_init(struct device *dev, void __iomem *hdcp1x_base)
{
	struct xhdcp1x_cipher *cipher;
	u32 reg;

	if (!dev || !hdcp1x_base)
		return ERR_PTR(-EINVAL);

	cipher = devm_kzalloc(dev, sizeof(*cipher), GFP_KERNEL);
	if (!cipher)
		return ERR_PTR(-ENOMEM);

	cipher->dev = dev;
	cipher->interface_base = hdcp1x_base;
	cipher->num_lanes = XHDCP1X_CIPHER_MAX_LANES;

	reg = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_TYPE);
	cipher->is_tx = reg & XHDCP1X_CIPHER_BITMASK_TYPE_DIRECTION;
	cipher->is_hdmi = (reg & XHDCP1X_CIPHER_BITMASK_TYPE_PROTOCOL) &
			   XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_HDMI;

	xhdcp1x_cipher_reset(cipher);

	return (void *)cipher;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_init);

/**
 * xhdcp1x_cipher_reset - Reset cipher
 * @ref: reference to cipher instance
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_reset(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher)
		return -EINVAL;

	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_RESET);
	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_RESET);

	/* Ensure all interrupts are disabled and cleared */
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_INTERRUPT_MASK,
			     XHDCP1X_CIPHER_INTR_ALL);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_INTERRUPT_STATUS,
			     XHDCP1X_CIPHER_INTR_ALL);

	if (!cipher->is_hdmi)
		xhdcp1x_cipher_config_lanes(cipher);

	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_reset);

/**
 * xhdcp1x_cipher_enable - Enable cipher
 * @ref: reference to cipher instance
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_enable(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value;

	if (!cipher)
		return -EINVAL;

	if (xhdcp1x_cipher_is_enabled(cipher))
		return -EBUSY;

	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	/* Ensure that all encryption is disabled for now */
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, 0);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, 0);

	/* Ensure that XOR is disabled on tx and enabled for rx to start */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	if (cipher->is_tx)
		value &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
	else
		value |= XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL, value);

	/* Enable it */
	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE);

	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_enable);

/**
 * xhdcp1x_cipher_disable - Disable cipher
 * @ref: reference to cipher instance
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_disable(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher)
		return -EINVAL;

	/* Ensure all interrupts are disabled */
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_INTERRUPT_MASK,
			     XHDCP1X_CIPHER_INTR_ALL);

	/* Enable bypass operation */
	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_ENABLE);

	/* Ensure that all encryption is disabled for now */
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, 0x00);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, 0x00);

	/* Ensure that XOR is disabled */
	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE);

	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_disable);

/**
 * xhdcp1x_cipher_set_num_lanes - Set number of active lanes in cipher
 * @ref: reference to cipher instance
 * @num_lanes: Number of active lanes
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_set_num_lanes(void *ref, u8 num_lanes)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher)
		return -EINVAL;

	if (num_lanes != XHDCP1X_CIPHER_NUM_LANES_1 &&
	    num_lanes != XHDCP1X_CIPHER_NUM_LANES_2 &&
	    num_lanes != XHDCP1X_CIPHER_NUM_LANES_4)
		return -EINVAL;

	cipher->num_lanes = num_lanes;

	xhdcp1x_cipher_config_lanes(cipher);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_set_num_lanes);

/**
 * xhdcp1x_cipher_set_keyselect - Selects the key vector to read from keymgmt block
 * @ref: reference to cipher instance
 * @keyselect: key vector number
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_set_keyselect(void *ref, u8 keyselect)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value;

	if (!cipher)
		return -EINVAL;

	if (keyselect > XHDCP1X_CIPHER_KEYSELECT_MAX_VALUE)
		return -EINVAL;

	/* Update the device */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL);
	value &= ~XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_SET_SELECT;
	value |= (keyselect << XHDCP1X_CIPHER_SHIFT_KEYMGMT_CONTROL_SET_SELECT);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL, value);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_set_keyselect);

/**
 * xhdcp1x_cipher_load_bksv - load local ksv from cipher to buf
 * @ref: reference to cipher instance
 * @buf: 5 byte buffer to store the local KSV
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_load_bksv(void *ref, u8 *buf)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u64 my_ksv;
	u32 is_enabled;

	if (!cipher || !buf)
		return -EINVAL;

	is_enabled = xhdcp1x_cipher_is_enabled(cipher);

	xhdcp1x_cipher_enable(cipher);
	my_ksv = xhdcp1x_cipher_get_localksv(cipher);
	if (!is_enabled)
		xhdcp1x_cipher_disable(cipher);
	if (!my_ksv)
		return -EAGAIN;
	memcpy(buf, &my_ksv, XHDCP1X_CIPHER_SIZE_LOCAL_KSV);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_load_bksv);

/**
 * xhdcp1x_cipher_set_remoteksv - set remote device KSV into cipher
 * @ref: reference to cipher instance
 * @ksv: remote device KSV
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_set_remoteksv(void *ref, u64 ksv)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 guard = XHDCP1X_CIPHER_KSV_RETRIES;
	u32 value;

	if (!cipher || !ksv)
		return -EINVAL;

	if (!xhdcp1x_cipher_is_enabled(cipher))
		return -EINVAL;

	/* Read local ksv to put things into a known state */
	xhdcp1x_cipher_get_localksv(cipher);

	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	value = (u32)ksv;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_KSV_REMOTE_L, value);
	value = (ksv >> 32) & 0xFF;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_KSV_REMOTE_H, value);

	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	/* Trigger the calculation of theKM */
	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL,
				XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_BEGIN_KM);
	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_KEYMGMT_CONTROL,
				XHDCP1X_CIPHER_BITMASK_KEYMGMT_CONTROL_BEGIN_KM);

	/* Wait until KM is available */
	while ((!xhdcp1x_cipher_is_km_ready(cipher)) && (--guard > 0))
		;

	if (!guard)
		return -EAGAIN;

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_set_remoteksv);

/**
 * xhdcp1x_cipher_get_ro - Read Ro from cipher
 * @ref: reference to cipher instance
 * @ro: reference to ro data
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_get_ro(void *ref, u16 *ro)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher || !ro)
		return -EINVAL;

	if (!xhdcp1x_cipher_is_enabled(cipher))
		return -EINVAL;

	*ro = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_RO);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_get_ro);

/**
 * xhdcp1x_cipher_set_b - Set B value into cipher
 * @ref: reference to cipher instance
 * @an: An value
 * @is_repeater: repeater flag
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_set_b(void *ref, u64 an, bool is_repeater)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value, x, y, z;

	if (!cipher)
		return -EINVAL;

	if (!xhdcp1x_cipher_is_enabled(cipher))
		return -EINVAL;

	x = (u32)(an & XHDCP1X_CIPHER_BITMASK_CIPHER_BX);
	an >>= XHDCP1X_CIPHER_SHIFT_CIPHER_B;
	y = (u32)(an & XHDCP1X_CIPHER_BITMASK_CIPHER_BY);
	an >>= XHDCP1X_CIPHER_SHIFT_CIPHER_B;
	z = (u32)an;
	if (is_repeater)
		z |= XHDCP1X_CIPHER_BITMASK_CIPHER_BZ_REPEATER;
	z &= XHDCP1X_CIPHER_BITMASK_CIPHER_BZ;

	xhdcp1x_cipher_clr_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	value = x & XHDCP1X_CIPHER_BITMASK_CIPHER_BX;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_BX, value);
	value = y & XHDCP1X_CIPHER_BITMASK_CIPHER_BY;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_BY, value);
	value = z & XHDCP1X_CIPHER_BITMASK_CIPHER_BZ;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_BZ, value);

	xhdcp1x_cipher_set_mask(cipher, XHDCP1X_CIPHER_REG_CONTROL,
				XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE);

	xhdcp1x_cipher_do_request(cipher, XHDCP1X_CIPHER_REQUEST_BLOCK);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_set_b);

/**
 * xhdcp1x_cipher_is_request_complete - check requested operation is completed
 * @ref: reference to cipher instance
 *
 * Return: 1 on request completion, 0 otherwise
 */
int xhdcp1x_cipher_is_request_complete(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher)
		return -EINVAL;

	return !(xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_STATUS) &
			XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_REQUEST_IN_PROG);
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_is_request_complete);

/**
 * xhdcp1x_cipher_set_link_state_check - Enable/Disable link status check
 * @ref: reference to cipher instance
 * @is_enabled: 1 for enable, 0 for disable
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_set_link_state_check(void *ref, bool is_enabled)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher)
		return -EINVAL;

	if (cipher->is_hdmi || cipher->is_tx)
		return -EINVAL;

	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_INTERRUPT_STATUS,
			     XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL);

	if (is_enabled)
		xhdcp1x_cipher_clr_mask(cipher,
					XHDCP1X_CIPHER_REG_INTERRUPT_MASK,
					XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL);
	else
		xhdcp1x_cipher_set_mask(cipher,
					XHDCP1X_CIPHER_REG_INTERRUPT_MASK,
					XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_set_link_state_check);

/**
 * xhdcp1x_cipher_get_interrupts - Read and clear the interrupts and return same
 * @ref: reference to cipher instance
 * @interrupts: reference to interrupts data
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_get_interrupts(void *ref, u32 *interrupts)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher || !interrupts)
		return -EINVAL;

	/* Read and clear the interrupts */
	*interrupts = xhdcp1x_cipher_read(cipher,
					  XHDCP1X_CIPHER_REG_INTERRUPT_STATUS);
	*interrupts &= xhdcp1x_cipher_read(cipher,
					   XHDCP1X_CIPHER_REG_INTERRUPT_MASK);
	if (*interrupts)
		xhdcp1x_cipher_write(cipher,
				     XHDCP1X_CIPHER_REG_INTERRUPT_STATUS,
				     *interrupts);
	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_get_interrupts);

/**
 * xhdcp1x_cipher_is_linkintegrity_failed - Check if link integrity failed
 * @ref: reference to cipher instance
 *
 * Return: 1 if link integrity failed, 0/error value otherwise
 */
int xhdcp1x_cipher_is_linkintegrity_failed(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value;

	if (!cipher)
		return -EINVAL;

	if (xhdcp1x_cipher_is_enabled(cipher)) {
		value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_STATUS);
		if (value & XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_is_linkintegrity_failed);

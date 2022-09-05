// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP1X Cipher driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 *
 * Author: Jagadeesh Banisetti <jagadeesh.banisetti@xilinx.com>
 */

#include <linux/bitfield.h>
#include <linux/xlnx/xilinx-hdcp1x-cipher.h>

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

u64 xhdcp1x_cipher_get_localksv(struct xhdcp1x_cipher *cipher)
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

int xhdcp1x_cipher_do_request(void *ref,
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
 * xhdcp1x_cipher_set_ri - Enable/Disable Ri Update Check
 * @ref: reference to cipher instance
 * @enable: Flag (True / False)
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_set_ri(void *ref, bool enable)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher)
		return -EINVAL;

	if (!cipher->is_hdmi)
		return -EINVAL;

	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_INTERRUPT_STATUS,
			     XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE);

	if (enable)
		xhdcp1x_cipher_clr_mask(cipher,
					XHDCP1X_CIPHER_REG_INTERRUPT_MASK,
					XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE);
	else
		xhdcp1x_cipher_set_mask(cipher,
					XHDCP1X_CIPHER_REG_INTERRUPT_MASK,
					XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_set_ri);

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
 * xhdcp1x_cipher_is_request_to_change_ri - Check if ri update is required
 * @ref: reference to cipher instance
 *
 * Return: 0 if ri update change is needed, error value otherwise
 */
int xhdcp1x_cipher_is_request_to_change_ri(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value;

	if (!cipher || !xhdcp1x_cipher_is_enabled(cipher))
		return -EINVAL;

	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_STATUS);
	if (!(value & XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_is_request_to_change_ri);

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

/**
 * xhdcp1x_cipher_get_ri - Read Ri from cipher
 * @ref: reference to cipher instance
 * @ri: reference to ri data
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp1x_cipher_get_ri(void *ref, u16 *ri)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!cipher || !ri || !xhdcp1x_cipher_is_enabled(cipher))
		return -EINVAL;

	*ri = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_RI);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_get_ri);

/**
 * xhdcp1x_cipher_load_aksv - load local ksv from cipher to buf
 * @ref: reference to cipher instance
 * @buf: 5 byte buffer to store the local KSV
 *
 * Return: 0 on success, error otherwise.
 */
int xhdcp1x_cipher_load_aksv(void *ref, u8 *buf)
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
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_load_aksv);

/**
 * xhdcp1x_cipher_getencryption - This Function retrives the current encryption Stream Map.
 * @ref: reference to cipher instance
 *
 * Return: streammap based on request, 0/error value otherwise.
 */
int xhdcp1x_cipher_getencryption(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value = 0;
	u64 streammap = 0;

	if (!cipher)
		return -EINVAL;

	if (!(xhdcp1x_cipher_is_enabled(cipher)))
		return streammap;

	streammap = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H);
	streammap <<= XHDCP1X_CIPHER_VALUE_SHIFT;
	streammap |= xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L);

	/* Determine if there is a request in progress */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_STATUS);
	value &= XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_XOR_IN_PROG;

	if (!streammap && value)
		streammap = XHDCP1X_CIPHER_DEFAULT_STREAMMAP;

	return streammap;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_getencryption);

/**
 * xhdcp1x_cipher_disableencryption - This function disables encryption on a set of streams.
 * @ref: reference to cipher instance
 * @streammap: Streammap for display Audio/video content encyption
 *
 * Return: 0 for success, error value otherwise.
 */
int xhdcp1x_cipher_disableencryption(void *ref, u64 streammap)
{
	u32 value = 0, checkxor = 1;
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;

	if (!(xhdcp1x_cipher_is_enabled(cipher)))
		return -EINVAL;

	if (!streammap)
		return 0;

	/* Clear the Register update bit */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL);
	value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CONTROL, value);

	/* Update the LS 32-bits */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L);
	value &= ~((u32)(streammap & XHDCP1X_CIPHER_DWORD_VALUE));
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, value);
	if (value)
		checkxor = 0;

	/* Update the MS 32-bits */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H);
	value &= ~((u32)((streammap >> XHDCP1X_CIPHER_VALUE_SHIFT) & XHDCP1X_CIPHER_DWORD_VALUE));
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, value);
	if (value)
		checkxor = 0;

	if (cipher->is_hdmi == XHDCP1X_CIPHER_VALUE_TYPE_PROTOCOL_HDMI)
		checkxor = true;

	if (checkxor) {
		value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
		value &= ~XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
		xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL, value);
	}
	/* Set the register update bit */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL);
	value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CONTROL, value);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_disableencryption);

/**
 * xhdcp1x_cipher_setb - This function writes the contents of the B register in BM0.
 * @ref: reference to cipher instance
 * @bx: is the value to be written to bx.
 * @by: is the value to be written to by.
 * @bz: is the value to be written to bz.
 *
 * @return:
 *		- SUCCESS if successful.
 *		- NON_ZERO otherwise.
 */
int xhdcp1x_cipher_setb(void *ref, u32 bx, u32 by, u32 bz)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value = 0;

	if (!(xhdcp1x_cipher_is_enabled(cipher)))
		return -EINVAL;

	/* Clear the register update bit */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL);
	value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CONTROL, value);

	/* Update the Bx */
	value = (bx & XHDCP1X_CIPHER_SET_B);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_BX, value);

	/* Update the By */
	value = (by & XHDCP1X_CIPHER_SET_B);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_BY, value);

	/* Update the Bz */
	value = (bz & XHDCP1X_CIPHER_SET_B);
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_BZ, value);

	/* Set the register update bit */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL);
	value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CONTROL, value);

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_setb);

/**
 * xhdcp1x_cipher_enable_encryption - This function enables the encryption on a set of streams.
 * @ref: reference to cipher instance
 * @streammap: Streammap for display Audio/video content encyption
 *
 * Return: 0 for succes, error value otherwise.
 */
int xhdcp1x_cipher_enable_encryption(void *ref, u64 streammap)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u32 value;

	if (!cipher)
		return -EINVAL;

	if (!(xhdcp1x_cipher_is_enabled(cipher)))
		return -EINVAL;

	/* Check for nothing to do */
	if (!streammap)
		return 0;

	/* Clear the register update bit */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL);
	value &= ~XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CONTROL, value);

	/* Update the LS 32-bits */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L);
	value |= ((u32)(streammap & XHDCP1X_CIPHER_DWORD_VALUE));
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_L, value);

	/* Write the MS 32-bits */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H);
	value |= ((u32)((streammap >> XHDCP1X_CIPHER_VALUE_SHIFT) &
			 XHDCP1X_CIPHER_DWORD_VALUE));
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_ENCRYPT_ENABLE_H, value);
	/* Ensure that the XOR is enabled */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL);
	value |= XHDCP1X_CIPHER_BITMASK_CIPHER_CONTROL_XOR_ENABLE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CIPHER_CONTROL, value);

	/* Set the register update bit */
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CONTROL);
	value |= XHDCP1X_CIPHER_BITMASK_CONTROL_UPDATE;
	xhdcp1x_cipher_write(cipher, XHDCP1X_CIPHER_REG_CONTROL, value);
	/* Check if XORInProgress bit is set in the status register*/
	value = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_STATUS);
	if ((value & XHDCP1X_CIPHER_BITMASK_CIPHER_STATUS_XOR_IN_PROG)) {
		/* Do nothing for now. We can depend on the Cipher
		 * to set the XorInProgress in status register when
		 * we receive protected content.
		 */
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_enable_encryption);

/**
 * xhdcp1x_cipher_get_mi: This function reads the contents of the Mi/An register of BM0.
 * @ref: is the device to query.
 *
 * @return: The contents of the register.
 */
u64 xhdcp1x_cipher_get_mi(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u64 mi = 0;

	/* Check that it is not disabled */
	if (!(xhdcp1x_cipher_is_enabled(cipher)))
		return -EINVAL;

	/* Update Mi */
	mi = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_MI_H);
	mi <<= XHDCP1X_CIPHER_VALUE_SHIFT;
	mi |= xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_MI_L);

	return mi;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_get_mi);

/**
 * xhdcp1x_cipher_get_mo: This function reads the contents of the Mo register of the device.
 * @ref: reference to cipher instance.
 *
 * @return: The contents of the Mo register.
 */
u64 xhdcp1x_cipher_get_mo(void *ref)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	u64 mo = 0;

	/* Check that it is not disabled */
	if (!(xhdcp1x_cipher_is_enabled(cipher)))
		return -EINVAL;

	/* Determine Mo */
	mo = xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_MO_H);
	mo <<= XHDCP1X_CIPHER_VALUE_SHIFT;
	mo |= xhdcp1x_cipher_read(cipher, XHDCP1X_CIPHER_REG_CIPHER_MO_L);

	return mo;
}

/**
 * xhdcp1x_cipher_set_ri_update: This function enables the interrupt for RI updates
 * @ref: reference to cipher instance.
 * @is_enabled: flag to enable/disable RI updation
 *
 * @return: 0 for success, error value otherwise.
 */
int xhdcp1x_cipher_set_ri_update(void *ref, int is_enabled)
{
	struct xhdcp1x_cipher *cipher = (struct xhdcp1x_cipher *)ref;
	int status = 0, value = 0;

	if (!cipher)
		return -EINVAL;

	if (!xhdcp1x_cipher_is_enabled(cipher))
		return -EINVAL;

	xhdcp1x_cipher_write(cipher,
			     XHDCP1X_CIPHER_REG_INTERRUPT_STATUS,
			     XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE);

	value = xhdcp1x_cipher_read(cipher,
				    XHDCP1X_CIPHER_REG_INTERRUPT_MASK);

	if (is_enabled)
		value &= ~XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE;
	else
		value |= XHDCP1X_CIPHER_BITMASK_INTERRUPT_RI_UPDATE;

	xhdcp1x_cipher_write(cipher,
			     XHDCP1X_CIPHER_REG_INTERRUPT_MASK, value);

	return status;
}
EXPORT_SYMBOL_GPL(xhdcp1x_cipher_set_ri_update);

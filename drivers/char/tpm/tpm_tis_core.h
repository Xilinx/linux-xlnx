/*
 * Copyright (C) 2005, 2006 IBM Corporation
 * Copyright (C) 2014, 2015 Intel Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2, revision 1.0.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __TPM_TIS_CORE_H__
#define __TPM_TIS_CORE_H__

#include "tpm.h"

enum tis_access {
	TPM_ACCESS_VALID = 0x80,
	TPM_ACCESS_ACTIVE_LOCALITY = 0x20,
	TPM_ACCESS_REQUEST_PENDING = 0x04,
	TPM_ACCESS_REQUEST_USE = 0x02,
};

enum tis_status {
	TPM_STS_VALID = 0x80,
	TPM_STS_COMMAND_READY = 0x40,
	TPM_STS_GO = 0x20,
	TPM_STS_DATA_AVAIL = 0x10,
	TPM_STS_DATA_EXPECT = 0x08,
};

enum tis_int_flags {
	TPM_GLOBAL_INT_ENABLE = 0x80000000,
	TPM_INTF_BURST_COUNT_STATIC = 0x100,
	TPM_INTF_CMD_READY_INT = 0x080,
	TPM_INTF_INT_EDGE_FALLING = 0x040,
	TPM_INTF_INT_EDGE_RISING = 0x020,
	TPM_INTF_INT_LEVEL_LOW = 0x010,
	TPM_INTF_INT_LEVEL_HIGH = 0x008,
	TPM_INTF_LOCALITY_CHANGE_INT = 0x004,
	TPM_INTF_STS_VALID_INT = 0x002,
	TPM_INTF_DATA_AVAIL_INT = 0x001,
};

enum tis_defaults {
	TIS_MEM_LEN = 0x5000,
	TIS_SHORT_TIMEOUT = 750,	/* ms */
	TIS_LONG_TIMEOUT = 2000,	/* 2 sec */
};

/* Some timeout values are needed before it is known whether the chip is
 * TPM 1.0 or TPM 2.0.
 */
#define TIS_TIMEOUT_A_MAX	max(TIS_SHORT_TIMEOUT, TPM2_TIMEOUT_A)
#define TIS_TIMEOUT_B_MAX	max(TIS_LONG_TIMEOUT, TPM2_TIMEOUT_B)
#define TIS_TIMEOUT_C_MAX	max(TIS_SHORT_TIMEOUT, TPM2_TIMEOUT_C)
#define TIS_TIMEOUT_D_MAX	max(TIS_SHORT_TIMEOUT, TPM2_TIMEOUT_D)

#define	TPM_ACCESS(l)			(0x0000 | ((l) << 12))
#define	TPM_INT_ENABLE(l)		(0x0008 | ((l) << 12))
#define	TPM_INT_VECTOR(l)		(0x000C | ((l) << 12))
#define	TPM_INT_STATUS(l)		(0x0010 | ((l) << 12))
#define	TPM_INTF_CAPS(l)		(0x0014 | ((l) << 12))
#define	TPM_STS(l)			(0x0018 | ((l) << 12))
#define	TPM_STS3(l)			(0x001b | ((l) << 12))
#define	TPM_DATA_FIFO(l)		(0x0024 | ((l) << 12))

#define	TPM_DID_VID(l)			(0x0F00 | ((l) << 12))
#define	TPM_RID(l)			(0x0F04 | ((l) << 12))

enum tpm_tis_flags {
	TPM_TIS_ITPM_POSSIBLE		= BIT(0),
};

struct tpm_tis_data {
	u16 manufacturer_id;
	int locality;
	int irq;
	bool irq_tested;
	unsigned int flags;
	wait_queue_head_t int_queue;
	wait_queue_head_t read_queue;
	const struct tpm_tis_phy_ops *phy_ops;
};

struct tpm_tis_phy_ops {
	int (*read_bytes)(struct tpm_tis_data *data, u32 addr, u16 len,
			  u8 *result);
	int (*write_bytes)(struct tpm_tis_data *data, u32 addr, u16 len,
			   u8 *value);
	int (*read16)(struct tpm_tis_data *data, u32 addr, u16 *result);
	int (*read32)(struct tpm_tis_data *data, u32 addr, u32 *result);
	int (*write32)(struct tpm_tis_data *data, u32 addr, u32 src);
};

static inline int tpm_tis_read_bytes(struct tpm_tis_data *data, u32 addr,
				     u16 len, u8 *result)
{
	return data->phy_ops->read_bytes(data, addr, len, result);
}

static inline int tpm_tis_read8(struct tpm_tis_data *data, u32 addr, u8 *result)
{
	return data->phy_ops->read_bytes(data, addr, 1, result);
}

static inline int tpm_tis_read16(struct tpm_tis_data *data, u32 addr,
				 u16 *result)
{
	return data->phy_ops->read16(data, addr, result);
}

static inline int tpm_tis_read32(struct tpm_tis_data *data, u32 addr,
				 u32 *result)
{
	return data->phy_ops->read32(data, addr, result);
}

static inline int tpm_tis_write_bytes(struct tpm_tis_data *data, u32 addr,
				      u16 len, u8 *value)
{
	return data->phy_ops->write_bytes(data, addr, len, value);
}

static inline int tpm_tis_write8(struct tpm_tis_data *data, u32 addr, u8 value)
{
	return data->phy_ops->write_bytes(data, addr, 1, &value);
}

static inline int tpm_tis_write32(struct tpm_tis_data *data, u32 addr,
				  u32 value)
{
	return data->phy_ops->write32(data, addr, value);
}

void tpm_tis_remove(struct tpm_chip *chip);
int tpm_tis_core_init(struct device *dev, struct tpm_tis_data *priv, int irq,
		      const struct tpm_tis_phy_ops *phy_ops,
		      acpi_handle acpi_dev_handle);

#ifdef CONFIG_PM_SLEEP
int tpm_tis_resume(struct device *dev);
#endif

#endif

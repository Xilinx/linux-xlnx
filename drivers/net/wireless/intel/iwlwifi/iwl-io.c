/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
 *
 * Portions of this file are derived from the ipw3945 project.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/export.h>

#include "iwl-drv.h"
#include "iwl-io.h"
#include "iwl-csr.h"
#include "iwl-debug.h"
#include "iwl-prph.h"
#include "iwl-fh.h"

void iwl_write8(struct iwl_trans *trans, u32 ofs, u8 val)
{
	trace_iwlwifi_dev_iowrite8(trans->dev, ofs, val);
	iwl_trans_write8(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write8);

void iwl_write32(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite32(trans->dev, ofs, val);
	iwl_trans_write32(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write32);

void iwl_write64(struct iwl_trans *trans, u64 ofs, u64 val)
{
	trace_iwlwifi_dev_iowrite64(trans->dev, ofs, val);
	iwl_trans_write32(trans, ofs, val & 0xffffffff);
	iwl_trans_write32(trans, ofs + 4, val >> 32);
}
IWL_EXPORT_SYMBOL(iwl_write64);

u32 iwl_read32(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read32(trans, ofs);

	trace_iwlwifi_dev_ioread32(trans->dev, ofs, val);
	return val;
}
IWL_EXPORT_SYMBOL(iwl_read32);

#define IWL_POLL_INTERVAL 10	/* microseconds */

int iwl_poll_bit(struct iwl_trans *trans, u32 addr,
		 u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read32(trans, addr) & mask) == (bits & mask))
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
IWL_EXPORT_SYMBOL(iwl_poll_bit);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg)
{
	u32 value = 0x5a5a5a5a;
	unsigned long flags;
	if (iwl_trans_grab_nic_access(trans, &flags)) {
		value = iwl_read32(trans, reg);
		iwl_trans_release_nic_access(trans, &flags);
	}

	return value;
}
IWL_EXPORT_SYMBOL(iwl_read_direct32);

void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		iwl_write32(trans, reg, value);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_direct32);

void iwl_write_direct64(struct iwl_trans *trans, u64 reg, u64 value)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		iwl_write64(trans, reg, value);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_direct64);

int iwl_poll_direct_bit(struct iwl_trans *trans, u32 addr, u32 mask,
			int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_direct32(trans, addr) & mask) == mask)
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
IWL_EXPORT_SYMBOL(iwl_poll_direct_bit);

u32 iwl_read_prph_no_grab(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read_prph(trans, ofs);
	trace_iwlwifi_dev_ioread_prph32(trans->dev, ofs, val);
	return val;
}
IWL_EXPORT_SYMBOL(iwl_read_prph_no_grab);

void iwl_write_prph_no_grab(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite_prph32(trans->dev, ofs, val);
	iwl_trans_write_prph(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write_prph_no_grab);

void iwl_write_prph64_no_grab(struct iwl_trans *trans, u64 ofs, u64 val)
{
	trace_iwlwifi_dev_iowrite_prph64(trans->dev, ofs, val);
	iwl_write_prph_no_grab(trans, ofs, val & 0xffffffff);
	iwl_write_prph_no_grab(trans, ofs + 4, val >> 32);
}
IWL_EXPORT_SYMBOL(iwl_write_prph64_no_grab);

u32 iwl_read_prph(struct iwl_trans *trans, u32 ofs)
{
	unsigned long flags;
	u32 val = 0x5a5a5a5a;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		val = iwl_read_prph_no_grab(trans, ofs);
		iwl_trans_release_nic_access(trans, &flags);
	}
	return val;
}
IWL_EXPORT_SYMBOL(iwl_read_prph);

void iwl_write_prph(struct iwl_trans *trans, u32 ofs, u32 val)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		iwl_write_prph_no_grab(trans, ofs, val);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_prph);

int iwl_poll_prph_bit(struct iwl_trans *trans, u32 addr,
		      u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_prph(trans, addr) & mask) == (bits & mask))
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

void iwl_set_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		iwl_write_prph_no_grab(trans, ofs,
				       iwl_read_prph_no_grab(trans, ofs) |
				       mask);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_prph);

void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 ofs,
			    u32 bits, u32 mask)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		iwl_write_prph_no_grab(trans, ofs,
				       (iwl_read_prph_no_grab(trans, ofs) &
					mask) | bits);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_mask_prph);

void iwl_clear_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	unsigned long flags;
	u32 val;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		val = iwl_read_prph_no_grab(trans, ofs);
		iwl_write_prph_no_grab(trans, ofs, (val & ~mask));
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_clear_bits_prph);

void iwl_force_nmi(struct iwl_trans *trans)
{
	if (trans->cfg->device_family != IWL_DEVICE_FAMILY_8000) {
		iwl_write_prph(trans, DEVICE_SET_NMI_REG,
			       DEVICE_SET_NMI_VAL_DRV);
		iwl_write_prph(trans, DEVICE_SET_NMI_REG,
			       DEVICE_SET_NMI_VAL_HW);
	} else {
		iwl_write_prph(trans, DEVICE_SET_NMI_8000_REG,
			       DEVICE_SET_NMI_8000_VAL);
		iwl_write_prph(trans, DEVICE_SET_NMI_REG,
			       DEVICE_SET_NMI_VAL_DRV);
	}
}
IWL_EXPORT_SYMBOL(iwl_force_nmi);

static const char *get_rfh_string(int cmd)
{
#define IWL_CMD(x) case x: return #x
#define IWL_CMD_MQ(arg, reg, q) { if (arg == reg(q)) return #reg; }

	int i;

	for (i = 0; i < IWL_MAX_RX_HW_QUEUES; i++) {
		IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_BA_LSB, i);
		IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_WIDX, i);
		IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_RIDX, i);
		IWL_CMD_MQ(cmd, RFH_Q_URBD_STTS_WPTR_LSB, i);
	}

	switch (cmd) {
	IWL_CMD(RFH_RXF_DMA_CFG);
	IWL_CMD(RFH_GEN_CFG);
	IWL_CMD(RFH_GEN_STATUS);
	IWL_CMD(FH_TSSR_TX_STATUS_REG);
	IWL_CMD(FH_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
#undef IWL_CMD_MQ
}

struct reg {
	u32 addr;
	bool is64;
};

static int iwl_dump_rfh(struct iwl_trans *trans, char **buf)
{
	int i, q;
	int num_q = trans->num_rx_queues;
	static const u32 rfh_tbl[] = {
		RFH_RXF_DMA_CFG,
		RFH_GEN_CFG,
		RFH_GEN_STATUS,
		FH_TSSR_TX_STATUS_REG,
		FH_TSSR_TX_ERROR_REG,
	};
	static const struct reg rfh_mq_tbl[] = {
		{ RFH_Q0_FRBDCB_BA_LSB, true },
		{ RFH_Q0_FRBDCB_WIDX, false },
		{ RFH_Q0_FRBDCB_RIDX, false },
		{ RFH_Q0_URBD_STTS_WPTR_LSB, true },
	};

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (buf) {
		int pos = 0;
		/*
		 * Register (up to 34 for name + 8 blank/q for MQ): 40 chars
		 * Colon + space: 2 characters
		 * 0X%08x: 10 characters
		 * New line: 1 character
		 * Total of 53 characters
		 */
		size_t bufsz = ARRAY_SIZE(rfh_tbl) * 53 +
			       ARRAY_SIZE(rfh_mq_tbl) * 53 * num_q + 40;

		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;

		pos += scnprintf(*buf + pos, bufsz - pos,
				"RFH register values:\n");

		for (i = 0; i < ARRAY_SIZE(rfh_tbl); i++)
			pos += scnprintf(*buf + pos, bufsz - pos,
				"%40s: 0X%08x\n",
				get_rfh_string(rfh_tbl[i]),
				iwl_read_prph(trans, rfh_tbl[i]));

		for (i = 0; i < ARRAY_SIZE(rfh_mq_tbl); i++)
			for (q = 0; q < num_q; q++) {
				u32 addr = rfh_mq_tbl[i].addr;

				addr += q * (rfh_mq_tbl[i].is64 ? 8 : 4);
				pos += scnprintf(*buf + pos, bufsz - pos,
					"%34s(q %2d): 0X%08x\n",
					get_rfh_string(addr), q,
					iwl_read_prph(trans, addr));
			}

		return pos;
	}
#endif

	IWL_ERR(trans, "RFH register values:\n");
	for (i = 0; i < ARRAY_SIZE(rfh_tbl); i++)
		IWL_ERR(trans, "  %34s: 0X%08x\n",
			get_rfh_string(rfh_tbl[i]),
			iwl_read_prph(trans, rfh_tbl[i]));

	for (i = 0; i < ARRAY_SIZE(rfh_mq_tbl); i++)
		for (q = 0; q < num_q; q++) {
			u32 addr = rfh_mq_tbl[i].addr;

			addr += q * (rfh_mq_tbl[i].is64 ? 8 : 4);
			IWL_ERR(trans, "  %34s(q %d): 0X%08x\n",
				get_rfh_string(addr), q,
				iwl_read_prph(trans, addr));
		}

	return 0;
}

static const char *get_fh_string(int cmd)
{
	switch (cmd) {
	IWL_CMD(FH_RSCSR_CHNL0_STTS_WPTR_REG);
	IWL_CMD(FH_RSCSR_CHNL0_RBDCB_BASE_REG);
	IWL_CMD(FH_RSCSR_CHNL0_WPTR);
	IWL_CMD(FH_MEM_RCSR_CHNL0_CONFIG_REG);
	IWL_CMD(FH_MEM_RSSR_SHARED_CTRL_REG);
	IWL_CMD(FH_MEM_RSSR_RX_STATUS_REG);
	IWL_CMD(FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV);
	IWL_CMD(FH_TSSR_TX_STATUS_REG);
	IWL_CMD(FH_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
#undef IWL_CMD
}

int iwl_dump_fh(struct iwl_trans *trans, char **buf)
{
	int i;
	static const u32 fh_tbl[] = {
		FH_RSCSR_CHNL0_STTS_WPTR_REG,
		FH_RSCSR_CHNL0_RBDCB_BASE_REG,
		FH_RSCSR_CHNL0_WPTR,
		FH_MEM_RCSR_CHNL0_CONFIG_REG,
		FH_MEM_RSSR_SHARED_CTRL_REG,
		FH_MEM_RSSR_RX_STATUS_REG,
		FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV,
		FH_TSSR_TX_STATUS_REG,
		FH_TSSR_TX_ERROR_REG
	};

	if (trans->cfg->mq_rx_supported)
		return iwl_dump_rfh(trans, buf);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (buf) {
		int pos = 0;
		size_t bufsz = ARRAY_SIZE(fh_tbl) * 48 + 40;

		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;

		pos += scnprintf(*buf + pos, bufsz - pos,
				"FH register values:\n");

		for (i = 0; i < ARRAY_SIZE(fh_tbl); i++)
			pos += scnprintf(*buf + pos, bufsz - pos,
				"  %34s: 0X%08x\n",
				get_fh_string(fh_tbl[i]),
				iwl_read_direct32(trans, fh_tbl[i]));

		return pos;
	}
#endif

	IWL_ERR(trans, "FH register values:\n");
	for (i = 0; i <  ARRAY_SIZE(fh_tbl); i++)
		IWL_ERR(trans, "  %34s: 0X%08x\n",
			get_fh_string(fh_tbl[i]),
			iwl_read_direct32(trans, fh_tbl[i]));

	return 0;
}

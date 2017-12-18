/**
 * debugfs.c - DesignWare USB3 DRD Controller DebugFS file
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include <linux/usb/ch9.h>

#include "core.h"
#include "gadget.h"
#include "io.h"
#include "debug.h"

#define dump_register(nm)				\
{							\
	.name	= __stringify(nm),			\
	.offset	= DWC3_ ##nm,				\
}

#define dump_ep_register_set(n)			\
	{					\
		.name = "DEPCMDPAR2("__stringify(n)")",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMDPAR2,	\
	},					\
	{					\
		.name = "DEPCMDPAR1("__stringify(n)")",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMDPAR1,	\
	},					\
	{					\
		.name = "DEPCMDPAR0("__stringify(n)")",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMDPAR0,	\
	},					\
	{					\
		.name = "DEPCMD("__stringify(n)")",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMD,		\
	}


static const struct debugfs_reg32 dwc3_regs[] = {
	dump_register(GSBUSCFG0),
	dump_register(GSBUSCFG1),
	dump_register(GTXTHRCFG),
	dump_register(GRXTHRCFG),
	dump_register(GCTL),
	dump_register(GEVTEN),
	dump_register(GSTS),
	dump_register(GUCTL1),
	dump_register(GSNPSID),
	dump_register(GGPIO),
	dump_register(GUID),
	dump_register(GUCTL),
	dump_register(GBUSERRADDR0),
	dump_register(GBUSERRADDR1),
	dump_register(GPRTBIMAP0),
	dump_register(GPRTBIMAP1),
	dump_register(GHWPARAMS0),
	dump_register(GHWPARAMS1),
	dump_register(GHWPARAMS2),
	dump_register(GHWPARAMS3),
	dump_register(GHWPARAMS4),
	dump_register(GHWPARAMS5),
	dump_register(GHWPARAMS6),
	dump_register(GHWPARAMS7),
	dump_register(GDBGFIFOSPACE),
	dump_register(GDBGLTSSM),
	dump_register(GPRTBIMAP_HS0),
	dump_register(GPRTBIMAP_HS1),
	dump_register(GPRTBIMAP_FS0),
	dump_register(GPRTBIMAP_FS1),

	dump_register(GUSB2PHYCFG(0)),
	dump_register(GUSB2PHYCFG(1)),
	dump_register(GUSB2PHYCFG(2)),
	dump_register(GUSB2PHYCFG(3)),
	dump_register(GUSB2PHYCFG(4)),
	dump_register(GUSB2PHYCFG(5)),
	dump_register(GUSB2PHYCFG(6)),
	dump_register(GUSB2PHYCFG(7)),
	dump_register(GUSB2PHYCFG(8)),
	dump_register(GUSB2PHYCFG(9)),
	dump_register(GUSB2PHYCFG(10)),
	dump_register(GUSB2PHYCFG(11)),
	dump_register(GUSB2PHYCFG(12)),
	dump_register(GUSB2PHYCFG(13)),
	dump_register(GUSB2PHYCFG(14)),
	dump_register(GUSB2PHYCFG(15)),

	dump_register(GUSB2I2CCTL(0)),
	dump_register(GUSB2I2CCTL(1)),
	dump_register(GUSB2I2CCTL(2)),
	dump_register(GUSB2I2CCTL(3)),
	dump_register(GUSB2I2CCTL(4)),
	dump_register(GUSB2I2CCTL(5)),
	dump_register(GUSB2I2CCTL(6)),
	dump_register(GUSB2I2CCTL(7)),
	dump_register(GUSB2I2CCTL(8)),
	dump_register(GUSB2I2CCTL(9)),
	dump_register(GUSB2I2CCTL(10)),
	dump_register(GUSB2I2CCTL(11)),
	dump_register(GUSB2I2CCTL(12)),
	dump_register(GUSB2I2CCTL(13)),
	dump_register(GUSB2I2CCTL(14)),
	dump_register(GUSB2I2CCTL(15)),

	dump_register(GUSB2PHYACC(0)),
	dump_register(GUSB2PHYACC(1)),
	dump_register(GUSB2PHYACC(2)),
	dump_register(GUSB2PHYACC(3)),
	dump_register(GUSB2PHYACC(4)),
	dump_register(GUSB2PHYACC(5)),
	dump_register(GUSB2PHYACC(6)),
	dump_register(GUSB2PHYACC(7)),
	dump_register(GUSB2PHYACC(8)),
	dump_register(GUSB2PHYACC(9)),
	dump_register(GUSB2PHYACC(10)),
	dump_register(GUSB2PHYACC(11)),
	dump_register(GUSB2PHYACC(12)),
	dump_register(GUSB2PHYACC(13)),
	dump_register(GUSB2PHYACC(14)),
	dump_register(GUSB2PHYACC(15)),

	dump_register(GUSB3PIPECTL(0)),
	dump_register(GUSB3PIPECTL(1)),
	dump_register(GUSB3PIPECTL(2)),
	dump_register(GUSB3PIPECTL(3)),
	dump_register(GUSB3PIPECTL(4)),
	dump_register(GUSB3PIPECTL(5)),
	dump_register(GUSB3PIPECTL(6)),
	dump_register(GUSB3PIPECTL(7)),
	dump_register(GUSB3PIPECTL(8)),
	dump_register(GUSB3PIPECTL(9)),
	dump_register(GUSB3PIPECTL(10)),
	dump_register(GUSB3PIPECTL(11)),
	dump_register(GUSB3PIPECTL(12)),
	dump_register(GUSB3PIPECTL(13)),
	dump_register(GUSB3PIPECTL(14)),
	dump_register(GUSB3PIPECTL(15)),

	dump_register(GTXFIFOSIZ(0)),
	dump_register(GTXFIFOSIZ(1)),
	dump_register(GTXFIFOSIZ(2)),
	dump_register(GTXFIFOSIZ(3)),
	dump_register(GTXFIFOSIZ(4)),
	dump_register(GTXFIFOSIZ(5)),
	dump_register(GTXFIFOSIZ(6)),
	dump_register(GTXFIFOSIZ(7)),
	dump_register(GTXFIFOSIZ(8)),
	dump_register(GTXFIFOSIZ(9)),
	dump_register(GTXFIFOSIZ(10)),
	dump_register(GTXFIFOSIZ(11)),
	dump_register(GTXFIFOSIZ(12)),
	dump_register(GTXFIFOSIZ(13)),
	dump_register(GTXFIFOSIZ(14)),
	dump_register(GTXFIFOSIZ(15)),
	dump_register(GTXFIFOSIZ(16)),
	dump_register(GTXFIFOSIZ(17)),
	dump_register(GTXFIFOSIZ(18)),
	dump_register(GTXFIFOSIZ(19)),
	dump_register(GTXFIFOSIZ(20)),
	dump_register(GTXFIFOSIZ(21)),
	dump_register(GTXFIFOSIZ(22)),
	dump_register(GTXFIFOSIZ(23)),
	dump_register(GTXFIFOSIZ(24)),
	dump_register(GTXFIFOSIZ(25)),
	dump_register(GTXFIFOSIZ(26)),
	dump_register(GTXFIFOSIZ(27)),
	dump_register(GTXFIFOSIZ(28)),
	dump_register(GTXFIFOSIZ(29)),
	dump_register(GTXFIFOSIZ(30)),
	dump_register(GTXFIFOSIZ(31)),

	dump_register(GRXFIFOSIZ(0)),
	dump_register(GRXFIFOSIZ(1)),
	dump_register(GRXFIFOSIZ(2)),
	dump_register(GRXFIFOSIZ(3)),
	dump_register(GRXFIFOSIZ(4)),
	dump_register(GRXFIFOSIZ(5)),
	dump_register(GRXFIFOSIZ(6)),
	dump_register(GRXFIFOSIZ(7)),
	dump_register(GRXFIFOSIZ(8)),
	dump_register(GRXFIFOSIZ(9)),
	dump_register(GRXFIFOSIZ(10)),
	dump_register(GRXFIFOSIZ(11)),
	dump_register(GRXFIFOSIZ(12)),
	dump_register(GRXFIFOSIZ(13)),
	dump_register(GRXFIFOSIZ(14)),
	dump_register(GRXFIFOSIZ(15)),
	dump_register(GRXFIFOSIZ(16)),
	dump_register(GRXFIFOSIZ(17)),
	dump_register(GRXFIFOSIZ(18)),
	dump_register(GRXFIFOSIZ(19)),
	dump_register(GRXFIFOSIZ(20)),
	dump_register(GRXFIFOSIZ(21)),
	dump_register(GRXFIFOSIZ(22)),
	dump_register(GRXFIFOSIZ(23)),
	dump_register(GRXFIFOSIZ(24)),
	dump_register(GRXFIFOSIZ(25)),
	dump_register(GRXFIFOSIZ(26)),
	dump_register(GRXFIFOSIZ(27)),
	dump_register(GRXFIFOSIZ(28)),
	dump_register(GRXFIFOSIZ(29)),
	dump_register(GRXFIFOSIZ(30)),
	dump_register(GRXFIFOSIZ(31)),

	dump_register(GEVNTADRLO(0)),
	dump_register(GEVNTADRHI(0)),
	dump_register(GEVNTSIZ(0)),
	dump_register(GEVNTCOUNT(0)),

	dump_register(GHWPARAMS8),
	dump_register(DCFG),
	dump_register(DCTL),
	dump_register(DEVTEN),
	dump_register(DSTS),
	dump_register(DGCMDPAR),
	dump_register(DGCMD),
	dump_register(DALEPENA),

	dump_ep_register_set(0),
	dump_ep_register_set(1),
	dump_ep_register_set(2),
	dump_ep_register_set(3),
	dump_ep_register_set(4),
	dump_ep_register_set(5),
	dump_ep_register_set(6),
	dump_ep_register_set(7),
	dump_ep_register_set(8),
	dump_ep_register_set(9),
	dump_ep_register_set(10),
	dump_ep_register_set(11),
	dump_ep_register_set(12),
	dump_ep_register_set(13),
	dump_ep_register_set(14),
	dump_ep_register_set(15),
	dump_ep_register_set(16),
	dump_ep_register_set(17),
	dump_ep_register_set(18),
	dump_ep_register_set(19),
	dump_ep_register_set(20),
	dump_ep_register_set(21),
	dump_ep_register_set(22),
	dump_ep_register_set(23),
	dump_ep_register_set(24),
	dump_ep_register_set(25),
	dump_ep_register_set(26),
	dump_ep_register_set(27),
	dump_ep_register_set(28),
	dump_ep_register_set(29),
	dump_ep_register_set(30),
	dump_ep_register_set(31),

	dump_register(OCFG),
	dump_register(OCTL),
	dump_register(OEVT),
	dump_register(OEVTEN),
	dump_register(OSTS),
};

static int dwc3_mode_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			reg;

	spin_lock_irqsave(&dwc->lock, flags);
	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (DWC3_GCTL_PRTCAP(reg)) {
	case DWC3_GCTL_PRTCAP_HOST:
		seq_printf(s, "host\n");
		break;
	case DWC3_GCTL_PRTCAP_DEVICE:
		seq_printf(s, "device\n");
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		seq_printf(s, "OTG\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %08x\n", DWC3_GCTL_PRTCAP(reg));
	}

	return 0;
}

static int dwc3_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_mode_show, inode->i_private);
}

static ssize_t dwc3_mode_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			mode = 0;
	char			buf[32];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "host", 4))
		mode |= DWC3_GCTL_PRTCAP_HOST;

	if (!strncmp(buf, "device", 6))
		mode |= DWC3_GCTL_PRTCAP_DEVICE;

	if (!strncmp(buf, "otg", 3))
		mode |= DWC3_GCTL_PRTCAP_OTG;

	if (mode) {
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_set_mode(dwc, mode);
		spin_unlock_irqrestore(&dwc->lock, flags);
	}
	return count;
}

static const struct file_operations dwc3_mode_fops = {
	.open			= dwc3_mode_open,
	.write			= dwc3_mode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int dwc3_testmode_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			reg;

	spin_lock_irqsave(&dwc->lock, flags);
	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= DWC3_DCTL_TSTCTRL_MASK;
	reg >>= 1;
	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (reg) {
	case 0:
		seq_printf(s, "no test\n");
		break;
	case TEST_J:
		seq_printf(s, "test_j\n");
		break;
	case TEST_K:
		seq_printf(s, "test_k\n");
		break;
	case TEST_SE0_NAK:
		seq_printf(s, "test_se0_nak\n");
		break;
	case TEST_PACKET:
		seq_printf(s, "test_packet\n");
		break;
	case TEST_FORCE_EN:
		seq_printf(s, "test_force_enable\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %d\n", reg);
	}

	return 0;
}

static int dwc3_testmode_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_testmode_show, inode->i_private);
}

static ssize_t dwc3_testmode_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			testmode = 0;
	char			buf[32];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "test_j", 6))
		testmode = TEST_J;
	else if (!strncmp(buf, "test_k", 6))
		testmode = TEST_K;
	else if (!strncmp(buf, "test_se0_nak", 12))
		testmode = TEST_SE0_NAK;
	else if (!strncmp(buf, "test_packet", 11))
		testmode = TEST_PACKET;
	else if (!strncmp(buf, "test_force_enable", 17))
		testmode = TEST_FORCE_EN;
	else
		testmode = 0;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc3_gadget_set_test_mode(dwc, testmode);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return count;
}

static const struct file_operations dwc3_testmode_fops = {
	.open			= dwc3_testmode_open,
	.write			= dwc3_testmode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int dwc3_link_state_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	enum dwc3_link_state	state;
	u32			reg;

	spin_lock_irqsave(&dwc->lock, flags);
	reg = dwc3_readl(dwc->regs, DWC3_DSTS);
	state = DWC3_DSTS_USBLNKST(reg);
	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (state) {
	case DWC3_LINK_STATE_U0:
		seq_printf(s, "U0\n");
		break;
	case DWC3_LINK_STATE_U1:
		seq_printf(s, "U1\n");
		break;
	case DWC3_LINK_STATE_U2:
		seq_printf(s, "U2\n");
		break;
	case DWC3_LINK_STATE_U3:
		seq_printf(s, "U3\n");
		break;
	case DWC3_LINK_STATE_SS_DIS:
		seq_printf(s, "SS.Disabled\n");
		break;
	case DWC3_LINK_STATE_RX_DET:
		seq_printf(s, "Rx.Detect\n");
		break;
	case DWC3_LINK_STATE_SS_INACT:
		seq_printf(s, "SS.Inactive\n");
		break;
	case DWC3_LINK_STATE_POLL:
		seq_printf(s, "Poll\n");
		break;
	case DWC3_LINK_STATE_RECOV:
		seq_printf(s, "Recovery\n");
		break;
	case DWC3_LINK_STATE_HRESET:
		seq_printf(s, "HRESET\n");
		break;
	case DWC3_LINK_STATE_CMPLY:
		seq_printf(s, "Compliance\n");
		break;
	case DWC3_LINK_STATE_LPBK:
		seq_printf(s, "Loopback\n");
		break;
	case DWC3_LINK_STATE_RESET:
		seq_printf(s, "Reset\n");
		break;
	case DWC3_LINK_STATE_RESUME:
		seq_printf(s, "Resume\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %d\n", state);
	}

	return 0;
}

static int dwc3_link_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_link_state_show, inode->i_private);
}

static ssize_t dwc3_link_state_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	enum dwc3_link_state	state = 0;
	char			buf[32];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "SS.Disabled", 11))
		state = DWC3_LINK_STATE_SS_DIS;
	else if (!strncmp(buf, "Rx.Detect", 9))
		state = DWC3_LINK_STATE_RX_DET;
	else if (!strncmp(buf, "SS.Inactive", 11))
		state = DWC3_LINK_STATE_SS_INACT;
	else if (!strncmp(buf, "Recovery", 8))
		state = DWC3_LINK_STATE_RECOV;
	else if (!strncmp(buf, "Compliance", 10))
		state = DWC3_LINK_STATE_CMPLY;
	else if (!strncmp(buf, "Loopback", 8))
		state = DWC3_LINK_STATE_LPBK;
	else
		return -EINVAL;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc3_gadget_set_link_state(dwc, state);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return count;
}

static const struct file_operations dwc3_link_state_fops = {
	.open			= dwc3_link_state_open,
	.write			= dwc3_link_state_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

struct dwc3_ep_file_map {
	char name[25];
	int (*show)(struct seq_file *s, void *unused);
};

static int dwc3_tx_fifo_queue_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	u32			val;

	spin_lock_irqsave(&dwc->lock, flags);
	val = dwc3_core_fifo_space(dep, DWC3_TXFIFOQ);
	seq_printf(s, "%u\n", val);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_rx_fifo_queue_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	u32			val;

	spin_lock_irqsave(&dwc->lock, flags);
	val = dwc3_core_fifo_space(dep, DWC3_RXFIFOQ);
	seq_printf(s, "%u\n", val);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_tx_request_queue_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	u32			val;

	spin_lock_irqsave(&dwc->lock, flags);
	val = dwc3_core_fifo_space(dep, DWC3_TXREQQ);
	seq_printf(s, "%u\n", val);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_rx_request_queue_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	u32			val;

	spin_lock_irqsave(&dwc->lock, flags);
	val = dwc3_core_fifo_space(dep, DWC3_RXREQQ);
	seq_printf(s, "%u\n", val);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_rx_info_queue_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	u32			val;

	spin_lock_irqsave(&dwc->lock, flags);
	val = dwc3_core_fifo_space(dep, DWC3_RXINFOQ);
	seq_printf(s, "%u\n", val);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_descriptor_fetch_queue_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	u32			val;

	spin_lock_irqsave(&dwc->lock, flags);
	val = dwc3_core_fifo_space(dep, DWC3_DESCFETCHQ);
	seq_printf(s, "%u\n", val);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_event_queue_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	u32			val;

	spin_lock_irqsave(&dwc->lock, flags);
	val = dwc3_core_fifo_space(dep, DWC3_EVENTQ);
	seq_printf(s, "%u\n", val);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_ep_transfer_type_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (!(dep->flags & DWC3_EP_ENABLED) ||
			!dep->endpoint.desc) {
		seq_printf(s, "--\n");
		goto out;
	}

	switch (usb_endpoint_type(dep->endpoint.desc)) {
	case USB_ENDPOINT_XFER_CONTROL:
		seq_printf(s, "control\n");
		break;
	case USB_ENDPOINT_XFER_ISOC:
		seq_printf(s, "isochronous\n");
		break;
	case USB_ENDPOINT_XFER_BULK:
		seq_printf(s, "bulk\n");
		break;
	case USB_ENDPOINT_XFER_INT:
		seq_printf(s, "interrupt\n");
		break;
	default:
		seq_printf(s, "--\n");
	}

out:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static inline const char *dwc3_trb_type_string(struct dwc3_trb *trb)
{
	switch (DWC3_TRBCTL_TYPE(trb->ctrl)) {
	case DWC3_TRBCTL_NORMAL:
		return "normal";
	case DWC3_TRBCTL_CONTROL_SETUP:
		return "control-setup";
	case DWC3_TRBCTL_CONTROL_STATUS2:
		return "control-status2";
	case DWC3_TRBCTL_CONTROL_STATUS3:
		return "control-status3";
	case DWC3_TRBCTL_CONTROL_DATA:
		return "control-data";
	case DWC3_TRBCTL_ISOCHRONOUS_FIRST:
		return "isoc-first";
	case DWC3_TRBCTL_ISOCHRONOUS:
		return "isoc";
	case DWC3_TRBCTL_LINK_TRB:
		return "link";
	default:
		return "UNKNOWN";
	}
}

static int dwc3_ep_trb_ring_show(struct seq_file *s, void *unused)
{
	struct dwc3_ep		*dep = s->private;
	struct dwc3		*dwc = dep->dwc;
	unsigned long		flags;
	int			i;

	spin_lock_irqsave(&dwc->lock, flags);
	if (dep->number <= 1) {
		seq_printf(s, "--\n");
		goto out;
	}

	seq_printf(s, "enqueue pointer %d\n", dep->trb_enqueue);
	seq_printf(s, "dequeue pointer %d\n", dep->trb_dequeue);
	seq_printf(s, "\n--------------------------------------------------\n\n");
	seq_printf(s, "buffer_addr,size,type,ioc,isp_imi,csp,chn,lst,hwo\n");

	for (i = 0; i < DWC3_TRB_NUM; i++) {
		struct dwc3_trb *trb = &dep->trb_pool[i];

		seq_printf(s, "%08x%08x,%d,%s,%d,%d,%d,%d,%d,%d\n",
				trb->bph, trb->bpl, trb->size,
				dwc3_trb_type_string(trb),
				!!(trb->ctrl & DWC3_TRB_CTRL_IOC),
				!!(trb->ctrl & DWC3_TRB_CTRL_ISP_IMI),
				!!(trb->ctrl & DWC3_TRB_CTRL_CSP),
				!!(trb->ctrl & DWC3_TRB_CTRL_CHN),
				!!(trb->ctrl & DWC3_TRB_CTRL_LST),
				!!(trb->ctrl & DWC3_TRB_CTRL_HWO));
	}

out:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static struct dwc3_ep_file_map map[] = {
	{ "tx_fifo_queue", dwc3_tx_fifo_queue_show, },
	{ "rx_fifo_queue", dwc3_rx_fifo_queue_show, },
	{ "tx_request_queue", dwc3_tx_request_queue_show, },
	{ "rx_request_queue", dwc3_rx_request_queue_show, },
	{ "rx_info_queue", dwc3_rx_info_queue_show, },
	{ "descriptor_fetch_queue", dwc3_descriptor_fetch_queue_show, },
	{ "event_queue", dwc3_event_queue_show, },
	{ "transfer_type", dwc3_ep_transfer_type_show, },
	{ "trb_ring", dwc3_ep_trb_ring_show, },
};

static int dwc3_endpoint_open(struct inode *inode, struct file *file)
{
	const char		*file_name = file_dentry(file)->d_iname;
	struct dwc3_ep_file_map	*f_map;
	int			i;

	for (i = 0; i < ARRAY_SIZE(map); i++) {
		f_map = &map[i];

		if (strcmp(f_map->name, file_name) == 0)
			break;
	}

	return single_open(file, f_map->show, inode->i_private);
}

static const struct file_operations dwc3_endpoint_fops = {
	.open			= dwc3_endpoint_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static void dwc3_debugfs_create_endpoint_file(struct dwc3_ep *dep,
		struct dentry *parent, int type)
{
	struct dentry		*file;
	struct dwc3_ep_file_map	*ep_file = &map[type];

	file = debugfs_create_file(ep_file->name, S_IRUGO, parent, dep,
			&dwc3_endpoint_fops);
}

static void dwc3_debugfs_create_endpoint_files(struct dwc3_ep *dep,
		struct dentry *parent)
{
	int			i;

	for (i = 0; i < ARRAY_SIZE(map); i++)
		dwc3_debugfs_create_endpoint_file(dep, parent, i);
}

static void dwc3_debugfs_create_endpoint_dir(struct dwc3_ep *dep,
		struct dentry *parent)
{
	struct dentry		*dir;

	dir = debugfs_create_dir(dep->name, parent);
	if (IS_ERR_OR_NULL(dir))
		return;

	dwc3_debugfs_create_endpoint_files(dep, dir);
}

static void dwc3_debugfs_create_endpoint_dirs(struct dwc3 *dwc,
		struct dentry *parent)
{
	int			i;

	for (i = 0; i < dwc->num_in_eps; i++) {
		u8		epnum = (i << 1) | 1;
		struct dwc3_ep	*dep = dwc->eps[epnum];

		if (!dep)
			continue;

		dwc3_debugfs_create_endpoint_dir(dep, parent);
	}

	for (i = 0; i < dwc->num_out_eps; i++) {
		u8		epnum = (i << 1);
		struct dwc3_ep	*dep = dwc->eps[epnum];

		if (!dep)
			continue;

		dwc3_debugfs_create_endpoint_dir(dep, parent);
	}
}

void dwc3_debugfs_init(struct dwc3 *dwc)
{
	struct dentry		*root;
	struct dentry           *file;

	root = debugfs_create_dir(dev_name(dwc->dev), NULL);
	if (IS_ERR_OR_NULL(root)) {
		if (!root)
			dev_err(dwc->dev, "Can't create debugfs root\n");
		return;
	}
	dwc->root = root;

	dwc->regset = kzalloc(sizeof(*dwc->regset), GFP_KERNEL);
	if (!dwc->regset) {
		debugfs_remove_recursive(root);
		return;
	}

	dwc->regset->regs = dwc3_regs;
	dwc->regset->nregs = ARRAY_SIZE(dwc3_regs);
	dwc->regset->base = dwc->regs - DWC3_GLOBALS_REGS_START;

	file = debugfs_create_regset32("regdump", S_IRUGO, root, dwc->regset);
	if (!file)
		dev_dbg(dwc->dev, "Can't create debugfs regdump\n");

	if (IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE)) {
		file = debugfs_create_file("mode", S_IRUGO | S_IWUSR, root,
				dwc, &dwc3_mode_fops);
		if (!file)
			dev_dbg(dwc->dev, "Can't create debugfs mode\n");
	}

	if (IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE) ||
			IS_ENABLED(CONFIG_USB_DWC3_GADGET)) {
		file = debugfs_create_file("testmode", S_IRUGO | S_IWUSR, root,
				dwc, &dwc3_testmode_fops);
		if (!file)
			dev_dbg(dwc->dev, "Can't create debugfs testmode\n");

		file = debugfs_create_file("link_state", S_IRUGO | S_IWUSR,
				root, dwc, &dwc3_link_state_fops);
		if (!file)
			dev_dbg(dwc->dev, "Can't create debugfs link_state\n");

		dwc3_debugfs_create_endpoint_dirs(dwc, root);
	}
}

void dwc3_debugfs_exit(struct dwc3 *dwc)
{
	debugfs_remove_recursive(dwc->root);
	kfree(dwc->regset);
}

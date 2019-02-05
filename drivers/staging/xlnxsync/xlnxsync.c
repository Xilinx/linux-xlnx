// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Synchronizer IP driver
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Author: Vishal Sagar <vishal.sagar@xilinx.com>
 *
 * This driver is used to control the Xilinx Synchronizer IP
 * to achieve sub frame latency for encode and decode with VCU.
 * This is done by monitoring the address lines for specific values.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/xlnxsync.h>

/* Register offsets and bit masks */
#define XLNXSYNC_CTRL_REG		0x00
#define XLNXSYNC_ISR_REG		0x04
#define XLNXSYNC_L_START_LO_REG		0x08
#define XLNXSYNC_L_START_HI_REG		0x0C
#define XLNXSYNC_C_START_LO_REG		0x20
#define XLNXSYNC_C_START_HI_REG		0x24
#define XLNXSYNC_L_END_LO_REG		0x38
#define XLNXSYNC_L_END_HI_REG		0x3C
#define XLNXSYNC_C_END_LO_REG		0x50
#define XLNXSYNC_C_END_HI_REG		0x54
#define XLNXSYNC_L_MARGIN_REG		0x68
#define XLNXSYNC_C_MARGIN_REG		0x74
#define XLNXSYNC_IER_REG		0x80
#define XLNXSYNC_DBG_REG		0x84

#define XLNXSYNC_CTRL_ENCDEC_MASK	BIT(0)
#define XLNXSYNC_CTRL_ENABLE_MASK	BIT(1)
#define XLNXSYNC_CTRL_INTR_EN_MASK	BIT(2)

#define XLNXSYNC_ISR_SYNC_FAIL_MASK	BIT(0)
#define XLNXSYNC_ISR_WDG_ERR_MASK	BIT(1)
#define XLNXSYNC_ISR_LDONE_SHIFT	(2)
#define XLNXSYNC_ISR_LDONE_MASK		GENMASK(3, 2)
#define XLNXSYNC_ISR_LSKIP_MASK		BIT(4)
#define XLNXSYNC_ISR_LVALID_MASK	BIT(5)
#define XLNXSYNC_ISR_CDONE_SHIFT	(6)
#define XLNXSYNC_ISR_CDONE_MASK		GENMASK(7, 6)
#define XLNXSYNC_ISR_CSKIP_MASK		BIT(8)
#define XLNXSYNC_ISR_CVALID_MASK	BIT(9)

/* bit 44 of start address */
#define XLNXSYNC_FB_VALID_MASK		BIT(12)
#define XLNXSYNC_FB_HI_ADDR_MASK	GENMASK(11, 0)

#define XLNXSYNC_IER_SYNC_FAIL_MASK	BIT(0)
#define XLNXSYNC_IER_WDG_ERR_MASK	BIT(1)
#define XLNXSYNC_IER_LVALID_MASK	BIT(5)
#define XLNXSYNC_IER_CVALID_MASK	BIT(9)

#define XLNXSYNC_IER_ALL_MASK		(XLNXSYNC_IER_SYNC_FAIL_MASK |\
					 XLNXSYNC_IER_WDG_ERR_MASK |\
					 XLNXSYNC_IER_LVALID_MASK |\
					 XLNXSYNC_IER_CVALID_MASK)

/* Other macros */
#define XLNXSYNC_CHAN_OFFSET		0x100

#define XLNXSYNC_DEVNAME_LEN		(32)

#define XLNXSYNC_DRIVER_NAME		"xlnxsync"

#define XLNXSYNC_DEV_MAX		256

/* Used to keep track of sync devices */
static DEFINE_IDA(xs_ida);

/**
 * struct xlnxsync_device - Xilinx Synchronizer struct
 * @miscdev: Miscellaneous device struct
 * @config: IP config struct
 * @dev: Pointer to device
 * @iomem: Pointer to the register space
 * @irq: IRQ number
 * @irq_lock: Spinlock used to protect access to sync and watchdog error
 * @wait_event: wait queue for error events
 * @sync_err: Capture synchronization error per channel
 * @wdg_err: Capture watchdog error per channel
 * @l_done: Luma done result array
 * @c_done: Chroma done result array
 * @axi_clk: Pointer to clock structure for axilite clock
 * @p_clk: Pointer to clock structure for producer clock
 * @c_clk: Pointer to clock structure for consumer clock
 * @minor: device id count
 *
 * This structure contains the device driver related parameters
 */
struct xlnxsync_device {
	struct miscdevice miscdev;
	struct xlnxsync_config config;
	struct device *dev;
	void __iomem *iomem;
	int irq;
	/* irq_lock is used to protect access to sync_err and wdg_err */
	spinlock_t irq_lock;
	wait_queue_head_t wait_event;
	bool sync_err[XLNXSYNC_MAX_ENC_CHANNEL];
	bool wdg_err[XLNXSYNC_MAX_ENC_CHANNEL];
	bool l_done[XLNXSYNC_MAX_ENC_CHANNEL][XLNXSYNC_BUF_PER_CHANNEL];
	bool c_done[XLNXSYNC_MAX_ENC_CHANNEL][XLNXSYNC_BUF_PER_CHANNEL];
	struct clk *axi_clk;
	struct clk *p_clk;
	struct clk *c_clk;
	int minor;
};

static inline struct xlnxsync_device *to_xlnxsync_device(struct file *file)
{
	struct miscdevice *miscdev = file->private_data;

	return container_of(miscdev, struct xlnxsync_device, miscdev);
}

static inline u32 xlnxsync_read(struct xlnxsync_device *dev, u32 chan, u32 reg)
{
	return ioread32(dev->iomem + (chan * XLNXSYNC_CHAN_OFFSET) + reg);
}

static inline void xlnxsync_write(struct xlnxsync_device *dev, u32 chan,
				  u32 reg, u32 val)
{
	iowrite32(val, dev->iomem + (chan * XLNXSYNC_CHAN_OFFSET) + reg);
}

static inline void xlnxsync_clr(struct xlnxsync_device *dev, u32 chan, u32 reg,
				u32 clr)
{
	xlnxsync_write(dev, chan, reg, xlnxsync_read(dev, chan, reg) & ~clr);
}

static inline void xlnxsync_set(struct xlnxsync_device *dev, u32 chan, u32 reg,
				u32 set)
{
	xlnxsync_write(dev, chan, reg, xlnxsync_read(dev, chan, reg) | set);
}

static bool xlnxsync_is_buf_done(struct xlnxsync_device *dev,
				 u32 channel, u32 buf)
{
	u32 luma_valid, chroma_valid;

	luma_valid = xlnxsync_read(dev, channel,
				   XLNXSYNC_L_START_HI_REG + (buf << 3)) &
				   XLNXSYNC_FB_VALID_MASK;
	chroma_valid = xlnxsync_read(dev, channel,
				     XLNXSYNC_C_START_HI_REG + (buf << 3)) &
				     XLNXSYNC_FB_VALID_MASK;
	if (!luma_valid && !chroma_valid)
		return true;

	return false;
}

static void xlnxsync_reset_chan(struct xlnxsync_device *dev, u32 chan)
{
	u32 i;

	xlnxsync_write(dev, chan, XLNXSYNC_CTRL_REG, 0);
	xlnxsync_write(dev, chan, XLNXSYNC_IER_REG, 0);
	for (i = 0; i < XLNXSYNC_BUF_PER_CHANNEL; i++) {
		xlnxsync_write(dev, chan,
			       XLNXSYNC_L_START_LO_REG + (i << 3), 0);
		xlnxsync_write(dev, chan,
			       XLNXSYNC_L_START_HI_REG + (i << 3), 0);
		xlnxsync_write(dev, chan,
			       XLNXSYNC_C_START_LO_REG + (i << 3), 0);
		xlnxsync_write(dev, chan,
			       XLNXSYNC_C_START_HI_REG + (i << 3), 0);
		xlnxsync_write(dev, chan,
			       XLNXSYNC_L_END_LO_REG + (i << 3), 0);
		xlnxsync_write(dev, chan,
			       XLNXSYNC_L_END_HI_REG + (i << 3), 0);
		xlnxsync_write(dev, chan,
			       XLNXSYNC_C_END_LO_REG + (i << 3), 0);
		xlnxsync_write(dev, chan,
			       XLNXSYNC_C_END_HI_REG + (i << 3), 0);
	}
	xlnxsync_write(dev, chan, XLNXSYNC_L_MARGIN_REG, 0);
	xlnxsync_write(dev, chan, XLNXSYNC_C_MARGIN_REG, 0);
}

static int xlnxsync_config_channel(struct xlnxsync_device *dev,
				   void __user *arg)
{
	struct xlnxsync_chan_config cfg;
	int ret, i = 0;

	ret = copy_from_user(&cfg, arg, sizeof(cfg));
	if (ret) {
		dev_err(dev->dev, "%s : Failed to copy from user\n", __func__);
		return ret;
	}

	if (cfg.channel_id >= dev->config.max_channels &&
	    cfg.channel_id != XLNXSYNC_AUTO_SEARCH) {
		dev_err(dev->dev, "%s : Incorrect channel id %d\n",
			__func__, cfg.channel_id);
		return -EINVAL;
	}

	dev_dbg(dev->dev, "Channel id = %d, FB id = %d IsMono = %d\n",
		cfg.channel_id, cfg.fb_id, cfg.ismono);
	dev_dbg(dev->dev, "Luma Start Addr = 0x%llx End Addr = 0x%llx Margin = 0x%08x\n",
		cfg.luma_start_address, cfg.luma_end_address, cfg.luma_margin);
	dev_dbg(dev->dev, "Chroma Start Addr = 0x%llx End Addr = 0x%llx Margin = 0x%08x\n",
		cfg.chroma_start_address, cfg.chroma_end_address,
		cfg.chroma_margin);

	if (cfg.channel_id == XLNXSYNC_AUTO_SEARCH) {
		ret = -EBUSY;
		for (i = 0; i < dev->config.max_channels; i++) {
			u32 val;

			val = xlnxsync_read(dev, i, XLNXSYNC_CTRL_REG);
			if (!(val & XLNXSYNC_CTRL_ENABLE_MASK)) {
				cfg.channel_id = i;
				ret = 0;
				dev_dbg(dev->dev,
					"Channel id auto assigned = %d\n", i);
				break;
			}
		}

		if (ret) {
			dev_dbg(dev->dev, "Unable to find free channel\n");
			return ret;
		}
	}

	if (cfg.fb_id == XLNXSYNC_AUTO_SEARCH) {
		/* When fb_id is 0xFF auto search for free fb in a channel */
		dev_dbg(dev->dev, "%s : auto search free fb\n", __func__);
		for (i = 0; i < XLNXSYNC_BUF_PER_CHANNEL; i++) {
			if (xlnxsync_is_buf_done(dev, cfg.channel_id, i))
				break;
			dev_dbg(dev->dev, "Channel %d FB %d is busy\n",
				cfg.channel_id, i);
		}

		if (i == XLNXSYNC_BUF_PER_CHANNEL)
			return -EBUSY;

	} else if (cfg.fb_id < XLNXSYNC_BUF_PER_CHANNEL) {
		/* If fb_id is specified, check its availability */
		if (!(xlnxsync_is_buf_done(dev, cfg.channel_id, cfg.fb_id))) {
			dev_dbg(dev->dev,
				"%s : FB %d in channel %d is busy!\n",
				__func__, i, cfg.channel_id);
			return -EBUSY;
		}
		i = cfg.fb_id;
		dev_dbg(dev->dev, "%s : Configure fb %d\n", __func__, i);
	} else {
		/* Invalid fb_id passed */
		dev_err(dev->dev, "Invalid FB id %d for configuration!\n",
			cfg.fb_id);
		return -EINVAL;
	}

	/* Start Address */
	xlnxsync_write(dev, cfg.channel_id,
		       XLNXSYNC_L_START_LO_REG + (i << 3),
		       lower_32_bits(cfg.luma_start_address));
	xlnxsync_write(dev, cfg.channel_id,
		       XLNXSYNC_L_START_HI_REG + (i << 3),
		       upper_32_bits(cfg.luma_start_address) &
		       XLNXSYNC_FB_HI_ADDR_MASK);

	/* End Address */
	xlnxsync_write(dev, cfg.channel_id,
		       XLNXSYNC_L_END_LO_REG + (i << 3),
		       lower_32_bits(cfg.luma_end_address));
	xlnxsync_write(dev, cfg.channel_id,
		       XLNXSYNC_L_END_HI_REG + (i << 3),
		       upper_32_bits(cfg.luma_end_address));

	/* Set margin */
	xlnxsync_write(dev, cfg.channel_id,
		       XLNXSYNC_L_MARGIN_REG + (i << 2),
		       cfg.luma_margin);

	if (!cfg.ismono) {
		dev_dbg(dev->dev, "%s : Not monochrome. Program Chroma\n",
			__func__);
		/* Chroma Start Address */
		xlnxsync_write(dev, cfg.channel_id,
			       XLNXSYNC_C_START_LO_REG + (i << 3),
			       lower_32_bits(cfg.chroma_start_address));
		xlnxsync_write(dev, cfg.channel_id,
			       XLNXSYNC_C_START_HI_REG + (i << 3),
			       upper_32_bits(cfg.chroma_start_address) &
			       XLNXSYNC_FB_HI_ADDR_MASK);
		/* Chroma End Address */
		xlnxsync_write(dev, cfg.channel_id,
			       XLNXSYNC_C_END_LO_REG + (i << 3),
			       lower_32_bits(cfg.chroma_end_address));
		xlnxsync_write(dev, cfg.channel_id,
			       XLNXSYNC_C_END_HI_REG + (i << 3),
			       upper_32_bits(cfg.chroma_end_address));
		/* Chroma Margin */
		xlnxsync_write(dev, cfg.channel_id,
			       XLNXSYNC_C_MARGIN_REG + (i << 2),
			       cfg.chroma_margin);
		/* Set the Valid bit */
		xlnxsync_set(dev, cfg.channel_id,
			     XLNXSYNC_C_START_HI_REG + (i << 3),
			     XLNXSYNC_FB_VALID_MASK);
	}

	/* Set the Valid bit */
	xlnxsync_set(dev, cfg.channel_id,
		     XLNXSYNC_L_START_HI_REG + (i << 3),
		     XLNXSYNC_FB_VALID_MASK);

	return 0;
}

static int xlnxsync_get_channel_status(struct xlnxsync_device *dev,
				       void __user *arg)
{
	int ret;
	u32 mask = 0, i, j;
	unsigned long flags;

	for (i = 0; i < dev->config.max_channels; i++) {
		/* Update Buffers status */
		for (j = 0; j < XLNXSYNC_BUF_PER_CHANNEL; j++) {
			if (xlnxsync_is_buf_done(dev, i, j)) {
				mask |= 1 << ((i << XLNXSYNC_BUF_PER_CHANNEL)
					      + j);
			}
		}

		/* Update channel enable status */
		if (xlnxsync_read(dev, i, XLNXSYNC_CTRL_REG) &
		    XLNXSYNC_CTRL_ENABLE_MASK)
			mask |= XLNXSYNC_CHX_ENB_MASK(i);

		/* Update channel error status */
		spin_lock_irqsave(&dev->irq_lock, flags);

		if (dev->sync_err[i])
			mask |= XLNXSYNC_CHX_SYNC_ERR_MASK(i);

		if (dev->wdg_err[i])
			mask |= XLNXSYNC_CHX_WDG_ERR_MASK(i);

		spin_unlock_irqrestore(&dev->irq_lock, flags);
	}

	ret = copy_to_user(arg, &mask, sizeof(mask));
	if (ret) {
		dev_err(dev->dev, "%s: failed to copy result data to user\n",
			__func__);
		return ret;
	}
	dev_dbg(dev->dev, "%s - Channel status = 0x%08x\n", __func__, mask);
	return ret;
}

static int xlnxsync_enable(struct xlnxsync_device *dev, u32 channel,
			   bool enable)
{
	/* check channel v/s max from dt */
	if (channel >= dev->config.max_channels) {
		dev_err(dev->dev, "Invalid channel %d. Max channels = %d!\n",
			channel, dev->config.max_channels);
		return -EINVAL;
	}

	if (enable) {
		dev_dbg(dev->dev, "Enabling %d channel\n", channel);
		xlnxsync_set(dev, channel, XLNXSYNC_IER_REG,
			     XLNXSYNC_IER_ALL_MASK);
		xlnxsync_set(dev, channel, XLNXSYNC_CTRL_REG,
			     XLNXSYNC_CTRL_ENABLE_MASK |
			     XLNXSYNC_CTRL_INTR_EN_MASK);
	} else {
		dev_dbg(dev->dev, "Disabling %d channel\n", channel);
		xlnxsync_clr(dev, channel, XLNXSYNC_CTRL_REG,
			     XLNXSYNC_CTRL_ENABLE_MASK |
			     XLNXSYNC_CTRL_INTR_EN_MASK);
		xlnxsync_clr(dev, channel, XLNXSYNC_IER_REG,
			     XLNXSYNC_IER_ALL_MASK);
	}

	return 0;
}

static int xlnxsync_get_config(struct xlnxsync_device *dev, void __user *arg)
{
	struct xlnxsync_config cfg;
	int ret;

	cfg.encode = dev->config.encode;
	cfg.max_channels = dev->config.max_channels;

	dev_dbg(dev->dev, "IP Config : encode = %d max_channels = %d\n",
		cfg.encode, cfg.max_channels);
	ret = copy_to_user(arg, &cfg, sizeof(cfg));
	if (ret) {
		dev_err(dev->dev, "%s: failed to copy result data to user\n",
			__func__);
		return ret;
	}

	return 0;
}

static int xlnxsync_clr_chan_err(struct xlnxsync_device *dev,
				 void __user *arg)
{
	struct xlnxsync_clr_err errcfg;
	int ret;
	unsigned long flags;

	ret = copy_from_user(&errcfg, arg, sizeof(errcfg));
	if (ret) {
		dev_err(dev->dev, "%s : Failed to copy from user\n", __func__);
		return ret;
	}

	if (errcfg.channel_id >= dev->config.max_channels) {
		dev_err(dev->dev, "%s : Incorrect channel id %d\n",
			__func__, errcfg.channel_id);
		return -EINVAL;
	}

	dev_dbg(dev->dev, "%s : Clearing %d channel errors\n",
		__func__, errcfg.channel_id);
	/* Clear channel error status */
	spin_lock_irqsave(&dev->irq_lock, flags);
	if (dev->sync_err[errcfg.channel_id])
		dev->sync_err[errcfg.channel_id] = false;

	if (dev->wdg_err[errcfg.channel_id])
		dev->wdg_err[errcfg.channel_id] = false;

	spin_unlock_irqrestore(&dev->irq_lock, flags);

	return 0;
}

static int xlnxsync_get_fbdone_status(struct xlnxsync_device *dev,
				      void __user *arg)
{
	struct xlnxsync_fbdone fbdone_stat;
	int ret, i, j;

	for (i = 0; i < dev->config.max_channels; i++)
		for (j = 0; j < XLNXSYNC_BUF_PER_CHANNEL; j++)
			if (dev->l_done[i][j] && dev->c_done[i][j])
				fbdone_stat.status[i][j] = true;

	ret = copy_to_user(arg, &fbdone_stat, sizeof(fbdone_stat));
	if (ret)
		dev_err(dev->dev, "%s: failed to copy result data to user\n",
			__func__);

	return ret;
}

static int xlnxsync_clr_fbdone_status(struct xlnxsync_device *dev,
				      void __user *arg)
{
	struct xlnxsync_fbdone fbd;
	int ret, i, j;
	unsigned long flags;

	ret = copy_from_user(&fbd, arg, sizeof(fbd));
	if (ret) {
		dev_err(dev->dev, "%s : Failed to copy from user\n", __func__);
		return ret;
	}

	/* Clear channel error status */
	spin_lock_irqsave(&dev->irq_lock, flags);

	for (i = 0; i < dev->config.max_channels; i++) {
		for (j = 0; j < XLNXSYNC_BUF_PER_CHANNEL; j++) {
			if (fbd.status[i][j]) {
				dev->l_done[i][j] = false;
				dev->c_done[i][j] = false;
				fbd.status[i][j] = false;
			}
		}
	}

	spin_unlock_irqrestore(&dev->irq_lock, flags);

	return 0;
}

static long xlnxsync_ioctl(struct file *fptr, unsigned int cmd,
			   unsigned long data)
{
	int ret = -EINVAL;
	u32 channel = data;
	void __user *arg = (void __user *)data;
	struct xlnxsync_device *xlnxsync_dev = to_xlnxsync_device(fptr);

	dev_dbg(xlnxsync_dev->dev, "ioctl = 0x%08x\n", cmd);

	switch (cmd) {
	case XLNXSYNC_GET_CFG:
		ret = xlnxsync_get_config(xlnxsync_dev, arg);
		break;
	case XLNXSYNC_GET_CHAN_STATUS:
		ret = xlnxsync_get_channel_status(xlnxsync_dev, arg);
		break;
	case XLNXSYNC_SET_CHAN_CONFIG:
		ret = xlnxsync_config_channel(xlnxsync_dev, arg);
		break;
	case XLNXSYNC_CHAN_ENABLE:
		ret = xlnxsync_enable(xlnxsync_dev, channel, true);
		break;
	case XLNXSYNC_CHAN_DISABLE:
		ret = xlnxsync_enable(xlnxsync_dev, channel, false);
		if (ret < 0)
			return ret;
		xlnxsync_reset_chan(xlnxsync_dev, channel);
		break;
	case XLNXSYNC_CLR_CHAN_ERR:
		ret = xlnxsync_clr_chan_err(xlnxsync_dev, arg);
		break;
	case XLNXSYNC_GET_CHAN_FBDONE_STAT:
		ret = xlnxsync_get_fbdone_status(xlnxsync_dev, arg);
		break;
	case XLNXSYNC_CLR_CHAN_FBDONE_STAT:
		ret = xlnxsync_clr_fbdone_status(xlnxsync_dev, arg);
		break;
	}

	return ret;
}

static __poll_t xlnxsync_poll(struct file *fptr, poll_table *wait)
{
	u32 i, j;
	bool err_event, framedone_event;
	__poll_t ret = 0;
	unsigned long flags;
	struct xlnxsync_device *dev = to_xlnxsync_device(fptr);

	ret = poll_requested_events(wait);

	dev_dbg_ratelimited(dev->dev, "%s : entered req_events = 0x%x!\n",
			    __func__, ret);

	if (!(ret & (POLLPRI | POLLIN)))
		return 0;

	poll_wait(fptr, &dev->wait_event, wait);

	spin_lock_irqsave(&dev->irq_lock, flags);
	err_event = false;
	for (i = 0; i < dev->config.max_channels && !err_event; i++) {
		if (dev->sync_err[i] || dev->wdg_err[i])
			err_event = true;
	}

	framedone_event = false;
	for (i = 0; i < dev->config.max_channels && !framedone_event; i++) {
		for (j = 0; j < XLNXSYNC_BUF_PER_CHANNEL; j++) {
			if (dev->l_done[i][j] && dev->c_done[i][j])
				framedone_event = true;
		}
	}
	spin_unlock_irqrestore(&dev->irq_lock, flags);

	if (err_event) {
		dev_dbg_ratelimited(dev->dev, "%s : error event occurred!\n",
				    __func__);
		ret |= POLLPRI;
	}

	if (framedone_event) {
		dev_dbg_ratelimited(dev->dev, "%s : framedone event occurred!\n",
				    __func__);
		ret |= POLLIN;
	}

	return ret;
}

static const struct file_operations xlnxsync_fops = {
	.unlocked_ioctl = xlnxsync_ioctl,
	.poll = xlnxsync_poll,
};

static irqreturn_t xlnxsync_irq_handler(int irq, void *data)
{
	struct xlnxsync_device *xlnxsync = (struct xlnxsync_device *)data;
	u32 val, i;
	bool err_event;
	bool framedone_event;
	unsigned long flags;

	spin_lock_irqsave(&xlnxsync->irq_lock, flags);
	err_event = false;
	framedone_event = false;
	for (i = 0; i < xlnxsync->config.max_channels; i++) {
		u32 j, buf_index;

		val = xlnxsync_read(xlnxsync, i, XLNXSYNC_ISR_REG);
		xlnxsync_write(xlnxsync, i, XLNXSYNC_ISR_REG, val);

		if (val & XLNXSYNC_ISR_SYNC_FAIL_MASK)
			xlnxsync->sync_err[i] = true;
		if (val & XLNXSYNC_ISR_WDG_ERR_MASK)
			xlnxsync->wdg_err[i] = true;
		if (xlnxsync->sync_err[i] || xlnxsync->wdg_err[i])
			err_event = true;

		if (val & XLNXSYNC_ISR_LDONE_MASK) {
			buf_index = (val & XLNXSYNC_ISR_LDONE_MASK) >>
				XLNXSYNC_ISR_LDONE_SHIFT;

			xlnxsync->l_done[i][buf_index] = true;
		}

		if (val & XLNXSYNC_ISR_CDONE_MASK) {
			buf_index = (val & XLNXSYNC_ISR_CDONE_MASK) >>
			     XLNXSYNC_ISR_CDONE_SHIFT;

			xlnxsync->c_done[i][buf_index] = true;
		}

		for (j = 0; j < XLNXSYNC_BUF_PER_CHANNEL; j++)
			if (xlnxsync->l_done[i][j] && xlnxsync->c_done[i][j])
				framedone_event = true;
	}
	spin_unlock_irqrestore(&xlnxsync->irq_lock, flags);

	if (err_event || framedone_event) {
		dev_dbg_ratelimited(xlnxsync->dev, "%s : error occurred\n",
				    __func__);
		wake_up_interruptible(&xlnxsync->wait_event);
	}

	return IRQ_HANDLED;
}

static int xlnxsync_parse_dt_prop(struct xlnxsync_device *xlnxsync)
{
	struct device_node *node = xlnxsync->dev->of_node;
	int ret;

	xlnxsync->config.encode = of_property_read_bool(node, "xlnx,encode");
	dev_dbg(xlnxsync->dev, "synchronizer type = %s\n",
		xlnxsync->config.encode ? "encode" : "decode");

	ret = of_property_read_u32(node, "xlnx,num-chan",
				   (u32 *)&xlnxsync->config.max_channels);
	if (ret)
		return ret;

	dev_dbg(xlnxsync->dev, "max channels = %d\n",
		xlnxsync->config.max_channels);

	if (xlnxsync->config.max_channels == 0 ||
	    xlnxsync->config.max_channels > XLNXSYNC_MAX_ENC_CHANNEL) {
		dev_err(xlnxsync->dev, "Number of channels should be 1 to 4.\n");
		dev_err(xlnxsync->dev, "Invalid number of channels : %d\n",
			xlnxsync->config.max_channels);
		return -EINVAL;
	}

	if (!xlnxsync->config.encode &&
	    xlnxsync->config.max_channels > XLNXSYNC_MAX_DEC_CHANNEL) {
		dev_err(xlnxsync->dev, "Decode can't have more than 2 channels.\n");
		return -EINVAL;
	}

	return ret;
}

static int xlnxsync_clk_setup(struct xlnxsync_device *xlnxsync)
{
	int ret;

	xlnxsync->axi_clk = devm_clk_get(xlnxsync->dev, "s_axi_ctrl");
	if (IS_ERR(xlnxsync->axi_clk)) {
		ret = PTR_ERR(xlnxsync->axi_clk);
		dev_err(xlnxsync->dev, "failed to get axi_aclk (%d)\n", ret);
		return ret;
	}

	xlnxsync->p_clk = devm_clk_get(xlnxsync->dev, "s_axi_mm_p");
	if (IS_ERR(xlnxsync->p_clk)) {
		ret = PTR_ERR(xlnxsync->p_clk);
		dev_err(xlnxsync->dev, "failed to get p_aclk (%d)\n", ret);
		return ret;
	}

	xlnxsync->c_clk = devm_clk_get(xlnxsync->dev, "s_axi_mm_c");
	if (IS_ERR(xlnxsync->c_clk)) {
		ret = PTR_ERR(xlnxsync->c_clk);
		dev_err(xlnxsync->dev, "failed to get c_aclk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(xlnxsync->axi_clk);
	if (ret) {
		dev_err(xlnxsync->dev, "failed to enable axi_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(xlnxsync->p_clk);
	if (ret) {
		dev_err(xlnxsync->dev, "failed to enable p_clk (%d)\n", ret);
		goto err_pclk;
	}

	ret = clk_prepare_enable(xlnxsync->c_clk);
	if (ret) {
		dev_err(xlnxsync->dev, "failed to enable c_clk (%d)\n", ret);
		goto err_cclk;
	}

	return ret;

err_cclk:
	clk_disable_unprepare(xlnxsync->p_clk);
err_pclk:
	clk_disable_unprepare(xlnxsync->axi_clk);

	return ret;
}

static int xlnxsync_probe(struct platform_device *pdev)
{
	struct xlnxsync_device *xlnxsync;
	/* struct device *dc; */
	struct resource *res;
	int ret;

	xlnxsync = devm_kzalloc(&pdev->dev, sizeof(*xlnxsync), GFP_KERNEL);
	if (!xlnxsync)
		return -ENOMEM;

	xlnxsync->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get resource.\n");
		return -ENODEV;
	}

	xlnxsync->iomem = devm_ioremap_nocache(xlnxsync->dev, res->start,
					       resource_size(res));
	if (!xlnxsync->iomem) {
		dev_err(&pdev->dev, "ip register mapping failed.\n");
		return -ENOMEM;
	}

	ret = xlnxsync_parse_dt_prop(xlnxsync);
	if (ret < 0)
		return ret;

	xlnxsync->irq = irq_of_parse_and_map(xlnxsync->dev->of_node, 0);
	if (!xlnxsync->irq) {
		dev_err(xlnxsync->dev, "Unable to parse and get irq.\n");
		return -EINVAL;
	}
	ret = devm_request_threaded_irq(xlnxsync->dev, xlnxsync->irq, NULL,
					xlnxsync_irq_handler, IRQF_ONESHOT,
					dev_name(xlnxsync->dev), xlnxsync);

	if (ret) {
		dev_err(xlnxsync->dev, "Err = %d Interrupt handler reg failed!\n",
			ret);
		return ret;
	}

	ret = xlnxsync_clk_setup(xlnxsync);
	if (ret) {
		dev_err(xlnxsync->dev, "clock setup failed!\n");
		return ret;
	}

	init_waitqueue_head(&xlnxsync->wait_event);
	spin_lock_init(&xlnxsync->irq_lock);

	xlnxsync->miscdev.minor = MISC_DYNAMIC_MINOR;
	xlnxsync->miscdev.name = devm_kzalloc(&pdev->dev, XLNXSYNC_DEVNAME_LEN,
					      GFP_KERNEL);
	if (!xlnxsync->miscdev.name) {
		ret = -ENOMEM;
		goto clk_err;
	}

	xlnxsync->minor = ida_simple_get(&xs_ida, 0, XLNXSYNC_DEV_MAX,
					 GFP_KERNEL);
	if (xlnxsync->minor < 0) {
		ret = xlnxsync->minor;
		goto clk_err;
	}

	snprintf((char *)xlnxsync->miscdev.name, XLNXSYNC_DEVNAME_LEN, "%s%d",
		 "xlnxsync", xlnxsync->minor);
	xlnxsync->miscdev.fops = &xlnxsync_fops;
	ret = misc_register(&xlnxsync->miscdev);
	if (ret < 0) {
		dev_err(xlnxsync->dev, "driver registration failed!\n");
		goto ida_err;
	}

	platform_set_drvdata(pdev, xlnxsync);

	dev_info(xlnxsync->dev, "Xilinx Synchronizer probe successful!\n");

	return 0;

ida_err:
	ida_simple_remove(&xs_ida, xlnxsync->minor);
clk_err:
	clk_disable_unprepare(xlnxsync->c_clk);
	clk_disable_unprepare(xlnxsync->p_clk);
	clk_disable_unprepare(xlnxsync->axi_clk);

	return ret;
}

static int xlnxsync_remove(struct platform_device *pdev)
{
	struct xlnxsync_device *xlnxsync = platform_get_drvdata(pdev);

	misc_deregister(&xlnxsync->miscdev);
	ida_simple_remove(&xs_ida, xlnxsync->minor);
	clk_disable_unprepare(xlnxsync->c_clk);
	clk_disable_unprepare(xlnxsync->p_clk);
	clk_disable_unprepare(xlnxsync->axi_clk);

	return 0;
}

static const struct of_device_id xlnxsync_of_match[] = {
	/* TODO : Change as per dt */
	{ .compatible = "xlnx,sync-1.0", },
	{ /* end of table*/ }
};
MODULE_DEVICE_TABLE(of, xlnxsync_of_match);

static struct platform_driver xlnxsync_driver = {
	.driver = {
		.name = XLNXSYNC_DRIVER_NAME,
		.of_match_table = xlnxsync_of_match,
	},
	.probe = xlnxsync_probe,
	.remove = xlnxsync_remove,
};

module_platform_driver(xlnxsync_driver);

MODULE_AUTHOR("Vishal Sagar");
MODULE_DESCRIPTION("Xilinx Synchronizer IP Driver");
MODULE_LICENSE("GPL v2");

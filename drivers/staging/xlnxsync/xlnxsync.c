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

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
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
/* Producer Luma/Chroma Start/End Address */
#define XLNXSYNC_PL_START_LO_REG	0x08
#define XLNXSYNC_PL_START_HI_REG	0x0C
#define XLNXSYNC_PC_START_LO_REG	0x20
#define XLNXSYNC_PC_START_HI_REG	0x24
#define XLNXSYNC_PL_END_LO_REG		0x38
#define XLNXSYNC_PL_END_HI_REG		0x3C
#define XLNXSYNC_PC_END_LO_REG		0x50
#define XLNXSYNC_PC_END_HI_REG		0x54
#define XLNXSYNC_L_MARGIN_REG		0x68
#define XLNXSYNC_C_MARGIN_REG		0x74
#define XLNXSYNC_IER_REG		0x80
#define XLNXSYNC_DBG_REG		0x84
/* Consumer Luma/Chroma Start/End Address */
#define XLNXSYNC_CL_START_LO_REG	0x88
#define XLNXSYNC_CL_START_HI_REG	0x8C
#define XLNXSYNC_CC_START_LO_REG	0xA0
#define XLNXSYNC_CC_START_HI_REG	0xA4
#define XLNXSYNC_CL_END_LO_REG		0xB8
#define XLNXSYNC_CL_END_HI_REG		0xBC
#define XLNXSYNC_CC_END_LO_REG		0xD0
#define XLNXSYNC_CC_END_HI_REG		0xD4

/* Luma/Chroma Core offset registers */
#define XLNXSYNC_LCOREOFF_REG		0x400
#define XLNXSYNC_CCOREOFF_REG		0x410
#define XLNXSYNC_COREOFF_NEXT		0x4

#define XLNXSYNC_CTRL_ENCDEC_MASK	BIT(0)
#define XLNXSYNC_CTRL_ENABLE_MASK	BIT(1)
#define XLNXSYNC_CTRL_INTR_EN_MASK	BIT(2)
#define XLNXSYNC_CTRL_SOFTRESET		BIT(3)

#define XLNXSYNC_ISR_SYNC_FAIL_MASK	BIT(0)
#define XLNXSYNC_ISR_WDG_ERR_MASK	BIT(1)
/* Producer related */
#define XLNXSYNC_ISR_PLDONE_SHIFT	(2)
#define XLNXSYNC_ISR_PLDONE_MASK	GENMASK(3, 2)
#define XLNXSYNC_ISR_PLSKIP_MASK	BIT(4)
#define XLNXSYNC_ISR_PLVALID_MASK	BIT(5)
#define XLNXSYNC_ISR_PCDONE_SHIFT	(6)
#define XLNXSYNC_ISR_PCDONE_MASK	GENMASK(7, 6)
#define XLNXSYNC_ISR_PCSKIP_MASK	BIT(8)
#define XLNXSYNC_ISR_PCVALID_MASK	BIT(9)
/* Consumer related */
#define XLNXSYNC_ISR_CLDONE_SHIFT	(10)
#define XLNXSYNC_ISR_CLDONE_MASK	GENMASK(11, 10)
#define XLNXSYNC_ISR_CLSKIP_MASK	BIT(12)
#define XLNXSYNC_ISR_CLVALID_MASK	BIT(13)
#define XLNXSYNC_ISR_CCDONE_SHIFT	(14)
#define XLNXSYNC_ISR_CCDONE_MASK	GENMASK(15, 14)
#define XLNXSYNC_ISR_CCSKIP_MASK	BIT(16)
#define XLNXSYNC_ISR_CCVALID_MASK	BIT(17)

#define XLNXSYNC_ISR_LDIFF		BIT(18)
#define XLNXSYNC_ISR_CDIFF		BIT(19)

/* bit 44 of start address */
#define XLNXSYNC_FB_VALID_MASK		BIT(12)
#define XLNXSYNC_FB_HI_ADDR_MASK	GENMASK(11, 0)

#define XLNXSYNC_IER_SYNC_FAIL_MASK	BIT(0)
#define XLNXSYNC_IER_WDG_ERR_MASK	BIT(1)
/* Producer */
#define XLNXSYNC_IER_PLVALID_MASK	BIT(5)
#define XLNXSYNC_IER_PCVALID_MASK	BIT(9)
/* Consumer */
#define XLNXSYNC_IER_CLVALID_MASK	BIT(13)
#define XLNXSYNC_IER_CCVALID_MASK	BIT(17)
/* Diff */
#define XLNXSYNC_IER_LDIFF		BIT(18)
#define XLNXSYNC_IER_CDIFF		BIT(19)

#define XLNXSYNC_IER_ALL_MASK		(XLNXSYNC_IER_SYNC_FAIL_MASK |\
					 XLNXSYNC_IER_WDG_ERR_MASK |\
					 XLNXSYNC_IER_PLVALID_MASK |\
					 XLNXSYNC_IER_PCVALID_MASK |\
					 XLNXSYNC_IER_CLVALID_MASK |\
					 XLNXSYNC_IER_CCVALID_MASK |\
					 XLNXSYNC_IER_LDIFF |\
					 XLNXSYNC_IER_CDIFF)

/* Other macros */
#define XLNXSYNC_CHAN_OFFSET		0x100

#define XLNXSYNC_DEVNAME_LEN		(32)

#define XLNXSYNC_DRIVER_NAME		"xlnxsync"
#define XLNXSYNC_DRIVER_VERSION		"0.1"

#define XLNXSYNC_DEV_MAX		256

/* Module Parameters */
static struct class *xlnxsync_class;
static dev_t xlnxsync_devt;
/* Used to keep track of sync devices */
static DEFINE_IDA(xs_ida);

/**
 * struct xlnxsync_device - Xilinx Synchronizer struct
 * @chdev: Character device driver struct
 * @dev: Pointer to device
 * @iomem: Pointer to the register space
 * @sync_mutex: Mutex used to serialize ioctl calls
 * @wq_fbdone: wait queue for frame buffer done events
 * @wq_error: wait queue for error events
 * @l_done: Luma done result array
 * @c_done: Chroma done result array
 * @sync_err: Capture synchronization error per channel
 * @wdg_err: Capture watchdog error per channel
 * @ldiff_err: Luma buffer diff > 1
 * @cdiff_err: Chroma buffer diff > 1
 * @axi_clk: Pointer to clock structure for axilite clock
 * @p_clk: Pointer to clock structure for producer clock
 * @c_clk: Pointer to clock structure for consumer clock
 * @user_count: Usage count
 * @reserved: Channel reserved status
 * @irq: IRQ number
 * @irq_lock: Spinlock used to protect access to sync and watchdog error
 * @minor: device id count
 * @config: IP config struct
 *
 * This structure contains the device driver related parameters
 */
struct xlnxsync_device {
	struct cdev chdev;
	struct device *dev;
	void __iomem *iomem;
	/* sync_mutex is used to serialize ioctl calls */
	struct mutex sync_mutex;
	wait_queue_head_t wq_fbdone;
	wait_queue_head_t wq_error;
	bool l_done[XLNXSYNC_MAX_ENC_CHAN][XLNXSYNC_BUF_PER_CHAN][XLNXSYNC_IO];
	bool c_done[XLNXSYNC_MAX_ENC_CHAN][XLNXSYNC_BUF_PER_CHAN][XLNXSYNC_IO];
	bool sync_err[XLNXSYNC_MAX_ENC_CHAN];
	bool wdg_err[XLNXSYNC_MAX_ENC_CHAN];
	bool ldiff_err[XLNXSYNC_MAX_ENC_CHAN];
	bool cdiff_err[XLNXSYNC_MAX_ENC_CHAN];
	struct clk *axi_clk;
	struct clk *p_clk;
	struct clk *c_clk;
	atomic_t user_count;
	bool reserved[XLNXSYNC_MAX_ENC_CHAN];
	int irq;
	/* irq_lock is used to protect access to sync_err and wdg_err */
	spinlock_t irq_lock;
	int minor;
	struct xlnxsync_config config;
};

/**
 * struct xlnxsync_ctx - Synchronizer context struct
 * @dev: Xilinx synchronizer device struct
 * @chan_id: Channel id
 *
 * This structure contains the device driver related parameters
 */
struct xlnxsync_ctx {
	struct xlnxsync_device *dev;
	u32 chan_id;
};

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
				 u32 channel, u32 buf, u32 io)
{
	u32 luma_valid, chroma_valid;
	u32 reg_laddr, reg_caddr;

	switch (io) {
	case XLNXSYNC_PROD:
		reg_laddr = XLNXSYNC_PL_START_HI_REG;
		reg_caddr = XLNXSYNC_PC_START_HI_REG;
		break;
	case XLNXSYNC_CONS:
		reg_laddr = XLNXSYNC_CL_START_HI_REG;
		reg_caddr = XLNXSYNC_CC_START_HI_REG;
		break;
	default:
		return false;
	}

	luma_valid = xlnxsync_read(dev, channel, reg_laddr + (buf << 3)) &
				   XLNXSYNC_FB_VALID_MASK;
	chroma_valid = xlnxsync_read(dev, channel, reg_caddr + (buf << 3)) &
				     XLNXSYNC_FB_VALID_MASK;
	if (!luma_valid && !chroma_valid)
		return true;

	return false;
}

static void xlnxsync_reset_chan(struct xlnxsync_device *dev, u32 chan)
{
	u8 num_retries = 50;

	xlnxsync_set(dev, chan, XLNXSYNC_CTRL_REG, XLNXSYNC_CTRL_SOFTRESET);
	/* Wait for a maximum of ~100ms to flush pending transactions */
	while (num_retries--) {
		if (!(xlnxsync_read(dev, chan, XLNXSYNC_CTRL_REG) &
				XLNXSYNC_CTRL_SOFTRESET))
			break;
		usleep_range(2000, 2100);
	}
}

static void xlnxsync_reset(struct xlnxsync_device *dev)
{
	u32 i;

	for (i = 0; i < dev->config.max_channels; i++)
		xlnxsync_reset_chan(dev, i);
}

static dma_addr_t xlnxsync_get_phy_addr(struct xlnxsync_device *dev,
					u32 fd)
{
	struct dma_buf *dbuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t phy_addr = 0;

	dbuf = dma_buf_get(fd);
	if (IS_ERR(dbuf)) {
		dev_err(dev->dev, "%s : Failed to get dma buf\n", __func__);
		goto get_phy_addr_err;
	}

	attach = dma_buf_attach(dbuf, dev->dev);
	if (IS_ERR(attach)) {
		dev_err(dev->dev, "%s : Failed to attach buf\n", __func__);
		goto fail_attach;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		dev_err(dev->dev, "%s : Failed to attach map\n", __func__);
		goto fail_map;
	}

	phy_addr = sg_dma_address(sgt->sgl);
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);

fail_map:
	dma_buf_detach(dbuf, attach);
fail_attach:
	dma_buf_put(dbuf);
get_phy_addr_err:
	return phy_addr;
}

static int xlnxsync_config_channel(struct xlnxsync_device *dev,
				   void __user *arg)
{
	struct xlnxsync_chan_config cfg;
	int ret, i = 0, j;
	dma_addr_t phy_start_address;
	u64 luma_start_address[XLNXSYNC_IO];
	u64 chroma_start_address[XLNXSYNC_IO];
	u64 luma_end_address[XLNXSYNC_IO];
	u64 chroma_end_address[XLNXSYNC_IO];

	ret = copy_from_user(&cfg, arg, sizeof(cfg));
	if (ret) {
		dev_err(dev->dev, "%s : Failed to copy from user\n", __func__);
		return ret;
	}

	if (cfg.hdr_ver != XLNXSYNC_IOCTL_HDR_VER) {
		dev_err(dev->dev, "%s : ioctl version mismatch\n", __func__);
		dev_err(dev->dev,
			"ioctl ver = 0x%llx expected ver = 0x%llx\n",
			cfg.hdr_ver, (u64)XLNXSYNC_IOCTL_HDR_VER);
		return -EINVAL;
	}

	/* Calculate luma/chroma physical addresses */
	phy_start_address = xlnxsync_get_phy_addr(dev, cfg.dma_fd);
	if (!phy_start_address) {
		dev_err(dev->dev, "%s : Failed to obtain physical address\n",
			__func__);
		return -EINVAL;
	}

	luma_start_address[XLNXSYNC_PROD] =
		cfg.luma_start_offset[XLNXSYNC_PROD] + phy_start_address;
	luma_start_address[XLNXSYNC_CONS] =
		cfg.luma_start_offset[XLNXSYNC_CONS] + phy_start_address;
	chroma_start_address[XLNXSYNC_PROD] =
		cfg.chroma_start_offset[XLNXSYNC_PROD] + phy_start_address;
	chroma_start_address[XLNXSYNC_CONS] =
		cfg. chroma_start_offset[XLNXSYNC_CONS] + phy_start_address;
	luma_end_address[XLNXSYNC_PROD] =
		cfg.luma_end_offset[XLNXSYNC_PROD] + phy_start_address;
	luma_end_address[XLNXSYNC_CONS] =
		cfg.luma_end_offset[XLNXSYNC_CONS] + phy_start_address;
	chroma_end_address[XLNXSYNC_PROD] =
		cfg.chroma_end_offset[XLNXSYNC_PROD] + phy_start_address;
	chroma_end_address[XLNXSYNC_CONS] =
		cfg.chroma_end_offset[XLNXSYNC_CONS] + phy_start_address;

	if (cfg.channel_id >= dev->config.max_channels) {
		dev_err(dev->dev, "%s : Incorrect channel id %d\n",
			__func__, cfg.channel_id);
		return -EINVAL;
	}

	dev_dbg(dev->dev, "Channel id = %d", cfg.channel_id);
	dev_dbg(dev->dev, "Producer address\n");
	dev_dbg(dev->dev, "Luma Start Addr = 0x%llx End Addr = 0x%llx Margin = 0x%08x\n",
		luma_start_address[XLNXSYNC_PROD],
		luma_end_address[XLNXSYNC_PROD], cfg.luma_margin);
	dev_dbg(dev->dev, "Chroma Start Addr = 0x%llx End Addr = 0x%llx Margin = 0x%08x\n",
		chroma_start_address[XLNXSYNC_PROD],
		chroma_end_address[XLNXSYNC_PROD], cfg.chroma_margin);
	dev_dbg(dev->dev, "FB id = %d IsMono = %d\n",
		cfg.fb_id[XLNXSYNC_PROD], cfg.ismono[XLNXSYNC_PROD]);
	dev_dbg(dev->dev, "Consumer address\n");
	dev_dbg(dev->dev, "Luma Start Addr = 0x%llx End Addr = 0x%llx\n",
		luma_start_address[XLNXSYNC_CONS],
		luma_end_address[XLNXSYNC_CONS]);
	dev_dbg(dev->dev, "Chroma Start Addr = 0x%llx End Addr = 0x%llx\n",
		chroma_start_address[XLNXSYNC_CONS],
		chroma_end_address[XLNXSYNC_CONS]);
	dev_dbg(dev->dev, "FB id = %d IsMono = %d\n",
		cfg.fb_id[XLNXSYNC_CONS], cfg.ismono[XLNXSYNC_CONS]);

	for (j = 0; j < XLNXSYNC_IO; j++) {
		u32 l_start_reg, l_end_reg, c_start_reg, c_end_reg;

		if (cfg.fb_id[j] == XLNXSYNC_AUTO_SEARCH) {
			/*
			 * When fb_id is 0xFF auto search for free fb
			 * in a channel
			 */
			dev_dbg(dev->dev, "%s : auto search free fb\n",
				__func__);
			for (i = 0; i < XLNXSYNC_BUF_PER_CHAN; i++) {
				if (xlnxsync_is_buf_done(dev, cfg.channel_id, i,
							 j))
					break;
				dev_dbg(dev->dev, "Channel %d %s FB %d is busy\n",
					cfg.channel_id, j ? "prod" : "cons", i);
			}

			if (i == XLNXSYNC_BUF_PER_CHAN)
				return -EBUSY;

		} else if (cfg.fb_id[j] >= 0 &&
			   cfg.fb_id[j] < XLNXSYNC_BUF_PER_CHAN) {
			/* If fb_id is specified, check its availability */
			if (!(xlnxsync_is_buf_done(dev, cfg.channel_id,
						   cfg.fb_id[j], j))) {
				dev_dbg(dev->dev,
					"%s : %s FB %d in channel %d is busy!\n",
					__func__, j ? "prod" : "cons",
					i, cfg.channel_id);
				return -EBUSY;
			}
			dev_dbg(dev->dev, "%s : Configure fb %d\n",
				__func__, i);
		} else {
			/* Invalid fb_id passed */
			dev_err(dev->dev, "Invalid FB id %d for configuration!\n",
				cfg.fb_id[j]);
			return -EINVAL;
		}

		if (j == XLNXSYNC_PROD) {
			l_start_reg = XLNXSYNC_PL_START_LO_REG;
			l_end_reg = XLNXSYNC_PL_END_LO_REG;
			c_start_reg = XLNXSYNC_PC_START_LO_REG;
			c_end_reg = XLNXSYNC_PC_END_LO_REG;
		} else {
			l_start_reg = XLNXSYNC_CL_START_LO_REG;
			l_end_reg = XLNXSYNC_CL_END_LO_REG;
			c_start_reg = XLNXSYNC_CC_START_LO_REG;
			c_end_reg = XLNXSYNC_CC_END_LO_REG;
		}

		/* Start Address */
		xlnxsync_write(dev, cfg.channel_id, l_start_reg + (i << 3),
			       lower_32_bits(luma_start_address[j]));

		xlnxsync_write(dev, cfg.channel_id,
			       (l_start_reg + 4) + (i << 3),
			       upper_32_bits(luma_start_address[j]) &
			       XLNXSYNC_FB_HI_ADDR_MASK);

		/* End Address */
		xlnxsync_write(dev, cfg.channel_id, l_end_reg + (i << 3),
			       lower_32_bits(luma_end_address[j]));
		xlnxsync_write(dev, cfg.channel_id, l_end_reg + 4 + (i << 3),
			       upper_32_bits(luma_end_address[j]));

		/* Set margin */
		xlnxsync_write(dev, cfg.channel_id,
			       XLNXSYNC_L_MARGIN_REG + (i << 2),
			       cfg.luma_margin);

		if (!cfg.ismono[j]) {
			dev_dbg(dev->dev, "%s : Not monochrome. Program Chroma\n",
				__func__);

			/* Chroma Start Address */
			xlnxsync_write(dev, cfg.channel_id,
				       c_start_reg + (i << 3),
				       lower_32_bits(chroma_start_address[j]));

			xlnxsync_write(dev, cfg.channel_id,
				       c_start_reg + 4 + (i << 3),
				       upper_32_bits(chroma_start_address[j]) &
				       XLNXSYNC_FB_HI_ADDR_MASK);

			/* Chroma End Address */
			xlnxsync_write(dev, cfg.channel_id,
				       c_end_reg + (i << 3),
				       lower_32_bits(chroma_end_address[j]));

			xlnxsync_write(dev, cfg.channel_id,
				       c_end_reg + 4 + (i << 3),
				       upper_32_bits(chroma_end_address[j]));

			/* Chroma Margin */
			xlnxsync_write(dev, cfg.channel_id,
				       XLNXSYNC_C_MARGIN_REG + (i << 2),
				       cfg.chroma_margin);

			/* Set the Valid bit */
			xlnxsync_set(dev, cfg.channel_id,
				     c_start_reg + 4 + (i << 3),
				     XLNXSYNC_FB_VALID_MASK);
		}

		/* Set the Valid bit */
		xlnxsync_set(dev, cfg.channel_id, l_start_reg + 4 + (i << 3),
			     XLNXSYNC_FB_VALID_MASK);
	}

	for (i = 0; i < XLNXSYNC_MAX_CORES; i++) {
		iowrite32(cfg.luma_core_offset[i],
			  dev->iomem + XLNXSYNC_LCOREOFF_REG +
			  (i * XLNXSYNC_COREOFF_NEXT));

		iowrite32(cfg.chroma_core_offset[i],
			  dev->iomem + XLNXSYNC_CCOREOFF_REG +
			  (i * XLNXSYNC_COREOFF_NEXT));
	}

	return 0;
}

static int xlnxsync_get_channel_status(struct xlnxsync_device *dev,
				       void __user *arg)
{
	int ret;
	u32 i, j, k;
	unsigned long flags;
	struct xlnxsync_stat status;

	for (i = 0; i < dev->config.max_channels; i++) {
		/* Update Buffers status */
		for (j = 0; j < XLNXSYNC_BUF_PER_CHAN; j++) {
			for (k = 0; k < XLNXSYNC_IO; k++) {
				if (xlnxsync_is_buf_done(dev, i, j, k))
					status.fbdone[i][j][k] = true;
				else
					status.fbdone[i][j][k] = false;
			}
		}

		/* Update channel enable status */
		if (xlnxsync_read(dev, i, XLNXSYNC_CTRL_REG) &
		    XLNXSYNC_CTRL_ENABLE_MASK)
			status.enable[i] = true;

		/* Update channel error status */
		spin_lock_irqsave(&dev->irq_lock, flags);
		status.sync_err[i] = dev->sync_err[i];
		status.wdg_err[i] = dev->wdg_err[i];
		status.ldiff_err[i] = dev->ldiff_err[i];
		status.cdiff_err[i] = dev->cdiff_err[i];
		spin_unlock_irqrestore(&dev->irq_lock, flags);
	}

	status.hdr_ver = XLNXSYNC_IOCTL_HDR_VER;

	ret = copy_to_user(arg, &status, sizeof(status));
	if (ret)
		dev_err(dev->dev, "%s: failed to copy result data to user\n",
			__func__);
	return ret;
}

static int xlnxsync_enable(struct xlnxsync_device *dev, u32 channel,
			   bool enable)
{
	if (dev->config.hdr_ver != XLNXSYNC_IOCTL_HDR_VER) {
		dev_err(dev->dev, "ioctl not supported!\n");
		return -EINVAL;
	}

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
		xlnxsync_reset_chan(dev, channel);
		xlnxsync_clr(dev, channel, XLNXSYNC_CTRL_REG,
			     XLNXSYNC_CTRL_ENABLE_MASK |
			     XLNXSYNC_CTRL_INTR_EN_MASK);
		xlnxsync_clr(dev, channel, XLNXSYNC_IER_REG,
			     XLNXSYNC_IER_ALL_MASK);
		dev->reserved[channel] = false;
	}

	return 0;
}

static int xlnxsync_get_config(struct xlnxsync_device *dev, void __user *arg)
{
	struct xlnxsync_config cfg;
	int ret;

	cfg.encode = dev->config.encode;
	cfg.max_channels = dev->config.max_channels;
	cfg.hdr_ver = XLNXSYNC_IOCTL_HDR_VER;

	dev_dbg(dev->dev, "IP Config : encode = %d max_channels = %d\n",
		cfg.encode, cfg.max_channels);
	dev_dbg(dev->dev, "ioctl version = 0x%llx\n", cfg.hdr_ver);
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

	if (errcfg.hdr_ver != XLNXSYNC_IOCTL_HDR_VER) {
		dev_err(dev->dev, "%s : ioctl version mismatch\n", __func__);
		dev_err(dev->dev,
			"ioctl ver = 0x%llx expected ver = 0x%llx\n",
			errcfg.hdr_ver, (u64)XLNXSYNC_IOCTL_HDR_VER);
		return -EINVAL;
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

	if (dev->ldiff_err[errcfg.channel_id])
		dev->ldiff_err[errcfg.channel_id] = false;

	if (dev->cdiff_err[errcfg.channel_id])
		dev->cdiff_err[errcfg.channel_id] = false;

	spin_unlock_irqrestore(&dev->irq_lock, flags);

	return 0;
}

static int xlnxsync_get_fbdone_status(struct xlnxsync_device *dev,
				      void __user *arg)
{
	struct xlnxsync_fbdone fbdone_stat;
	int ret, i, j, k;

	fbdone_stat.hdr_ver = XLNXSYNC_IOCTL_HDR_VER;

	for (i = 0; i < dev->config.max_channels; i++)
		for (j = 0; j < XLNXSYNC_BUF_PER_CHAN; j++)
			for (k = 0; k < XLNXSYNC_IO; k++)
				if (dev->l_done[i][j][k] &&
				    dev->c_done[i][j][k])
					fbdone_stat.status[i][j][k] = true;

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
	int ret, i, j, k;
	unsigned long flags;

	ret = copy_from_user(&fbd, arg, sizeof(fbd));
	if (ret) {
		dev_err(dev->dev, "%s : Failed to copy from user\n", __func__);
		return ret;
	}

	if (fbd.hdr_ver != XLNXSYNC_IOCTL_HDR_VER) {
		dev_err(dev->dev, "%s : ioctl version mismatch\n", __func__);
		dev_err(dev->dev,
			"ioctl ver = 0x%llx expected ver = 0x%llx\n",
			fbd.hdr_ver, (u64)XLNXSYNC_IOCTL_HDR_VER);
		return -EINVAL;
	}

	/* Clear channel error status */
	spin_lock_irqsave(&dev->irq_lock, flags);

	for (i = 0; i < dev->config.max_channels; i++) {
		for (j = 0; j < XLNXSYNC_BUF_PER_CHAN; j++) {
			for (k = 0; k < XLNXSYNC_IO; k++) {
				fbd.status[i][j][k] = false;
				dev->l_done[i][j][k] = false;
				dev->c_done[i][j][k] = false;
			}
		}
	}

	spin_unlock_irqrestore(&dev->irq_lock, flags);

	return 0;
}

static int xlnxsync_reserve_get_channel(struct xlnxsync_device *dev,
					void __user *arg)
{
	int ret;
	u8 i;

	if (dev->config.hdr_ver != XLNXSYNC_IOCTL_HDR_VER) {
		dev_err(dev->dev, "ioctl not supported!\n");
		return -EINVAL;
	}

	for (i = 0; i < dev->config.max_channels; i++) {
		if (!dev->reserved[i])
			break;
	}

	if (i == dev->config.max_channels) {
		ret = -EBUSY;
		dev_dbg(dev->dev, "No channel is free!\n");
		return ret;
	}

	dev_dbg(dev->dev, "Reserving channel %d\n", i);
	dev->reserved[i] = true;
	ret = copy_to_user(arg, &i, sizeof(i));
	if (ret) {
		dev_err(dev->dev, "%s: failed to copy result data to user\n",
			__func__);
		ret = -EFAULT;
	}

	return ret;
}

static long xlnxsync_ioctl(struct file *fptr, unsigned int cmd,
			   unsigned long data)
{
	int ret = -EINVAL;
	u32 channel = data;
	void __user *arg = (void __user *)data;
	struct xlnxsync_ctx *ctx = fptr->private_data;
	struct xlnxsync_device *xlnxsync_dev;

	xlnxsync_dev = ctx->dev;
	if (!xlnxsync_dev) {
		pr_err("%s: File op error\n", __func__);
		return -EIO;
	}

	dev_dbg(xlnxsync_dev->dev, "ioctl = 0x%08x\n", cmd);

	if (mutex_lock_interruptible(&xlnxsync_dev->sync_mutex))
		return -ERESTARTSYS;

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
		ctx->chan_id = channel;
		ret = xlnxsync_enable(xlnxsync_dev, channel, true);
		break;
	case XLNXSYNC_CHAN_DISABLE:
		ret = xlnxsync_enable(xlnxsync_dev, channel, false);
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
	case XLNXSYNC_RESERVE_GET_CHAN_ID:
		ret = xlnxsync_reserve_get_channel(xlnxsync_dev, arg);
		break;
	}

	mutex_unlock(&xlnxsync_dev->sync_mutex);

	return ret;
}

static __poll_t xlnxsync_poll(struct file *fptr, poll_table *wait)
{
	u32 j, k;
	bool err_event, framedone_event;
	__poll_t ret = 0, req_events = poll_requested_events(wait);
	unsigned long flags;
	struct xlnxsync_ctx *ctx = fptr->private_data;
	struct xlnxsync_device *dev;

	dev = ctx->dev;
	if (!dev) {
		pr_err("%s: File op error\n", __func__);
		return -EIO;
	}

	dev_dbg_ratelimited(dev->dev, "%s : entered req_events = 0x%x!\n",
			    __func__, req_events);

	if (!(req_events & (POLLPRI | POLLIN)))
		return 0;

	if (req_events & EPOLLPRI) {
		poll_wait(fptr, &dev->wq_error, wait);
		spin_lock_irqsave(&dev->irq_lock, flags);
		err_event = false;
		if (dev->sync_err[ctx->chan_id] || dev->wdg_err[ctx->chan_id] ||
		    dev->ldiff_err[ctx->chan_id] ||
		    dev->cdiff_err[ctx->chan_id])
			err_event = true;
		spin_unlock_irqrestore(&dev->irq_lock, flags);
		dev_dbg_ratelimited(dev->dev, "%s : error event occurred!\n",
				    __func__);
		if (err_event)
			ret |= POLLPRI;
	}

	if (req_events & EPOLLIN) {
		poll_wait(fptr, &dev->wq_fbdone, wait);
		spin_lock_irqsave(&dev->irq_lock, flags);
		framedone_event = false;
		for (j = 0; j < XLNXSYNC_BUF_PER_CHAN; j++) {
			for (k = 0; k < XLNXSYNC_IO; k++) {
				if (dev->l_done[ctx->chan_id][j][k] &&
				    dev->c_done[ctx->chan_id][j][k])
					framedone_event = true;
			}
		}
		spin_unlock_irqrestore(&dev->irq_lock, flags);
		dev_dbg_ratelimited(dev->dev, "%s : framedone event occurred!\n",
				    __func__);
		if (framedone_event)
			ret |= POLLIN;
	}

	return ret;
}

static int xlnxsync_open(struct inode *iptr, struct file *fptr)
{
	struct xlnxsync_device *xlnxsync;
	struct xlnxsync_ctx *ctx;

	xlnxsync = container_of(iptr->i_cdev, struct xlnxsync_device, chdev);
	if (!xlnxsync) {
		pr_err("%s: failed to get xlnxsync driver handle\n", __func__);
		return -EAGAIN;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = xlnxsync;
	fptr->private_data = ctx;

	atomic_inc(&xlnxsync->user_count);
	dev_dbg(xlnxsync->dev, "%s: tid=%d Opened with user count = %d\n",
		__func__, current->pid, atomic_read(&xlnxsync->user_count));

	return 0;
}

static int xlnxsync_release(struct inode *iptr, struct file *fptr)
{
	struct xlnxsync_device *xlnxsync;
	struct xlnxsync_ctx *ctx = fptr->private_data;
	unsigned int i, j;

	xlnxsync = container_of(iptr->i_cdev, struct xlnxsync_device, chdev);
	if (!xlnxsync) {
		pr_err("%s: failed to get xlnxsync driver handle", __func__);
		return -EAGAIN;
	}

	dev_dbg(xlnxsync->dev, "%s: tid=%d user count = %d chan_id = %d\n",
		__func__, current->pid, atomic_read(&xlnxsync->user_count),
		ctx->chan_id);

	if (xlnxsync_read(xlnxsync, ctx->chan_id, XLNXSYNC_CTRL_REG) &
	    XLNXSYNC_CTRL_ENABLE_MASK) {
		dev_dbg(xlnxsync->dev, "Disabling %d channel\n", ctx->chan_id);
		xlnxsync_reset_chan(xlnxsync, ctx->chan_id);
		xlnxsync_clr(xlnxsync, ctx->chan_id, XLNXSYNC_CTRL_REG,
			     XLNXSYNC_CTRL_ENABLE_MASK |
			     XLNXSYNC_CTRL_INTR_EN_MASK);
		xlnxsync_clr(xlnxsync, ctx->chan_id, XLNXSYNC_IER_REG,
			     XLNXSYNC_IER_ALL_MASK);
	}

	xlnxsync->reserved[ctx->chan_id] = false;
	xlnxsync->sync_err[ctx->chan_id] = false;
	xlnxsync->wdg_err[ctx->chan_id] = false;
	xlnxsync->ldiff_err[ctx->chan_id] = false;
	xlnxsync->cdiff_err[ctx->chan_id] = false;
	for (i = 0; i < XLNXSYNC_BUF_PER_CHAN; i++) {
		for (j = 0; j < XLNXSYNC_IO; j++) {
			xlnxsync->l_done[ctx->chan_id][i][j] = false;
			xlnxsync->c_done[ctx->chan_id][i][j] = false;
		}
	}

	if (atomic_dec_and_test(&xlnxsync->user_count)) {
		xlnxsync_reset(xlnxsync);
		dev_dbg(xlnxsync->dev,
			"%s: tid=%d Stopping and clearing device",
			__func__, current->pid);
	}

	kfree(ctx);
	return 0;
}

static const struct file_operations xlnxsync_fops = {
	.open = xlnxsync_open,
	.release = xlnxsync_release,
	.unlocked_ioctl = xlnxsync_ioctl,
	.poll = xlnxsync_poll,
};

static irqreturn_t xlnxsync_irq_handler(int irq, void *data)
{
	struct xlnxsync_device *xlnxsync = (struct xlnxsync_device *)data;
	u32 val, i;
	bool err_event;
	bool framedone_event;

	/*
	 * Use simple spin_lock (instead of spin_lock_irqsave) as interrupt
	 * is registered with irqf_oneshot and !irqf_shared
	 */
	spin_lock(&xlnxsync->irq_lock);
	err_event = false;
	framedone_event = false;
	for (i = 0; i < xlnxsync->config.max_channels; i++) {
		u32 j, k;

		val = xlnxsync_read(xlnxsync, i, XLNXSYNC_ISR_REG);
		xlnxsync_write(xlnxsync, i, XLNXSYNC_ISR_REG, val);

		if (val & XLNXSYNC_ISR_SYNC_FAIL_MASK)
			xlnxsync->sync_err[i] = true;
		if (val & XLNXSYNC_ISR_WDG_ERR_MASK)
			xlnxsync->wdg_err[i] = true;
		if (val & XLNXSYNC_ISR_LDIFF)
			xlnxsync->ldiff_err[i] = true;
		if (val & XLNXSYNC_ISR_CDIFF)
			xlnxsync->cdiff_err[i] = true;
		if (xlnxsync->sync_err[i] || xlnxsync->wdg_err[i] ||
		    xlnxsync->ldiff_err[i] || xlnxsync->cdiff_err[i])
			err_event = true;

		if (val & XLNXSYNC_ISR_PLDONE_MASK) {
			j = (val & XLNXSYNC_ISR_PLDONE_MASK) >>
				XLNXSYNC_ISR_PLDONE_SHIFT;

			xlnxsync->l_done[i][j][XLNXSYNC_PROD] = true;
		}

		if (val & XLNXSYNC_ISR_PCDONE_MASK) {
			j = (val & XLNXSYNC_ISR_PCDONE_MASK) >>
				XLNXSYNC_ISR_PCDONE_SHIFT;

			xlnxsync->c_done[i][j][XLNXSYNC_PROD] = true;
		}

		if (val & XLNXSYNC_ISR_CLDONE_MASK) {
			j = (val & XLNXSYNC_ISR_CLDONE_MASK) >>
			     XLNXSYNC_ISR_CLDONE_SHIFT;

			xlnxsync->l_done[i][j][XLNXSYNC_CONS] = true;
		}

		if (val & XLNXSYNC_ISR_CCDONE_MASK) {
			j = (val & XLNXSYNC_ISR_CCDONE_MASK) >>
			     XLNXSYNC_ISR_CCDONE_SHIFT;

			xlnxsync->c_done[i][j][XLNXSYNC_CONS] = true;
		}

		for (j = 0; j < XLNXSYNC_BUF_PER_CHAN; j++) {
			for (k = 0; k < XLNXSYNC_IO; k++) {
				if (xlnxsync->l_done[i][j][k] &&
				    xlnxsync->c_done[i][j][k])
					framedone_event = true;
			}
		}
	}
	spin_unlock(&xlnxsync->irq_lock);

	if (err_event) {
		dev_dbg_ratelimited(xlnxsync->dev, "%s : error occurred\n",
				    __func__);
		wake_up_interruptible(&xlnxsync->wq_error);
	}

	if (framedone_event) {
		dev_dbg_ratelimited(xlnxsync->dev, "%s : framedone occurred\n",
				    __func__);
		wake_up_interruptible(&xlnxsync->wq_fbdone);
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
	    xlnxsync->config.max_channels > XLNXSYNC_MAX_ENC_CHAN) {
		dev_err(xlnxsync->dev, "Number of channels should be 1 to 4.\n");
		dev_err(xlnxsync->dev, "Invalid number of channels : %d\n",
			xlnxsync->config.max_channels);
		return -EINVAL;
	}

	if (!xlnxsync->config.encode &&
	    xlnxsync->config.max_channels > XLNXSYNC_MAX_DEC_CHAN) {
		dev_err(xlnxsync->dev, "Decode can't have more than 2 channels.\n");
		return -EINVAL;
	}

	return ret;
}

static int xlnxsync_clk_setup(struct xlnxsync_device *xlnxsync)
{
	int ret;

	xlnxsync->axi_clk = devm_clk_get(xlnxsync->dev, "s_axi_ctrl_aclk");
	if (IS_ERR(xlnxsync->axi_clk)) {
		ret = PTR_ERR(xlnxsync->axi_clk);
		dev_err(xlnxsync->dev, "failed to get axi_aclk (%d)\n", ret);
		return ret;
	}

	xlnxsync->p_clk = devm_clk_get(xlnxsync->dev, "s_axi_mm_p_aclk");
	if (IS_ERR(xlnxsync->p_clk)) {
		ret = PTR_ERR(xlnxsync->p_clk);
		dev_err(xlnxsync->dev, "failed to get p_aclk (%d)\n", ret);
		return ret;
	}

	xlnxsync->c_clk = devm_clk_get(xlnxsync->dev, "s_axi_mm_aclk");
	if (IS_ERR(xlnxsync->c_clk)) {
		ret = PTR_ERR(xlnxsync->c_clk);
		dev_err(xlnxsync->dev, "failed to get axi_mm (%d)\n", ret);
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
		dev_err(xlnxsync->dev, "failed to enable axi_mm (%d)\n", ret);
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
	struct device *dc;
	struct resource *res;
	int ret;

	xlnxsync = devm_kzalloc(&pdev->dev, sizeof(*xlnxsync), GFP_KERNEL);
	if (!xlnxsync)
		return -ENOMEM;

	xlnxsync->minor = ida_simple_get(&xs_ida, 0, XLNXSYNC_DEV_MAX,
					 GFP_KERNEL);
	if (xlnxsync->minor < 0)
		return xlnxsync->minor;

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

	xlnxsync->config.hdr_ver = XLNXSYNC_IOCTL_HDR_VER;
	dev_info(xlnxsync->dev, "ioctl header version = 0x%llx\n",
		 xlnxsync->config.hdr_ver);

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

	init_waitqueue_head(&xlnxsync->wq_fbdone);
	init_waitqueue_head(&xlnxsync->wq_error);
	spin_lock_init(&xlnxsync->irq_lock);
	mutex_init(&xlnxsync->sync_mutex);

	cdev_init(&xlnxsync->chdev, &xlnxsync_fops);
	xlnxsync->chdev.owner = THIS_MODULE;
	ret = cdev_add(&xlnxsync->chdev,
		       MKDEV(MAJOR(xlnxsync_devt), xlnxsync->minor), 1);
	if (ret < 0) {
		dev_err(xlnxsync->dev, "cdev_add failed");
		goto clk_err;
	}

	if (!xlnxsync_class) {
		dev_err(xlnxsync->dev, "xvfsync device class not created");
		goto cdev_err;
	}
	dc = device_create(xlnxsync_class, xlnxsync->dev,
			   MKDEV(MAJOR(xlnxsync_devt), xlnxsync->minor),
			   xlnxsync, "xlnxsync%d", xlnxsync->minor);
	if (IS_ERR(dc)) {
		ret = PTR_ERR(dc);
		dev_err(xlnxsync->dev, "Unable to create device");
		goto cdev_err;
	}

	platform_set_drvdata(pdev, xlnxsync);
	dev_info(xlnxsync->dev, "Xilinx Synchronizer probe successful!\n");

	return 0;

cdev_err:
	cdev_del(&xlnxsync->chdev);
clk_err:
	clk_disable_unprepare(xlnxsync->c_clk);
	clk_disable_unprepare(xlnxsync->p_clk);
	clk_disable_unprepare(xlnxsync->axi_clk);
	ida_simple_remove(&xs_ida, xlnxsync->minor);

	return ret;
}

static int xlnxsync_remove(struct platform_device *pdev)
{
	struct xlnxsync_device *xlnxsync = platform_get_drvdata(pdev);

	if (!xlnxsync || !xlnxsync_class)
		return -EIO;

	cdev_del(&xlnxsync->chdev);
	clk_disable_unprepare(xlnxsync->c_clk);
	clk_disable_unprepare(xlnxsync->p_clk);
	clk_disable_unprepare(xlnxsync->axi_clk);
	ida_simple_remove(&xs_ida, xlnxsync->minor);

	return 0;
}

static const struct of_device_id xlnxsync_of_match[] = {
	{ .compatible = "xlnx,sync-ip-1.0", },
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

static int __init xlnxsync_init_mod(void)
{
	int err;

	xlnxsync_class = class_create(THIS_MODULE, XLNXSYNC_DRIVER_NAME);
	if (IS_ERR(xlnxsync_class)) {
		pr_err("%s : Unable to create xlnxsync class", __func__);
		return PTR_ERR(xlnxsync_class);
	}
	err = alloc_chrdev_region(&xlnxsync_devt, 0,
				  XLNXSYNC_DEV_MAX, XLNXSYNC_DRIVER_NAME);
	if (err < 0) {
		pr_err("%s: Unable to get major number for xlnxsync", __func__);
		goto err_class;
	}
	err = platform_driver_register(&xlnxsync_driver);
	if (err < 0) {
		pr_err("%s: Unable to register %s driver",
		       __func__, XLNXSYNC_DRIVER_NAME);
		goto err_pdrv;
	}
	return 0;
err_pdrv:
	unregister_chrdev_region(xlnxsync_devt, XLNXSYNC_DEV_MAX);
err_class:
	class_destroy(xlnxsync_class);
	return err;
}

static void __exit xlnxsync_cleanup_mod(void)
{
	platform_driver_unregister(&xlnxsync_driver);
	unregister_chrdev_region(xlnxsync_devt, XLNXSYNC_DEV_MAX);
	class_destroy(xlnxsync_class);
	xlnxsync_class = NULL;
}
module_init(xlnxsync_init_mod);
module_exit(xlnxsync_cleanup_mod);

MODULE_AUTHOR("Vishal Sagar");
MODULE_DESCRIPTION("Xilinx Synchronizer IP Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(XLNXSYNC_DRIVER_VERSION);

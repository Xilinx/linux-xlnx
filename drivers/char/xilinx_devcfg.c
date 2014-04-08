/*
 * Xilinx Zynq Device Config driver
 *
 * Copyright (c) 2011 - 2013 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/uaccess.h>

extern void zynq_slcr_init_preload_fpga(void);
extern void zynq_slcr_init_postload_fpga(void);

#define DRIVER_NAME "xdevcfg"
#define XDEVCFG_DEVICES 1

/* An array, which is set to true when the device is registered. */
static DEFINE_MUTEX(xdevcfg_mutex);

/* Constant Definitions */
#define XDCFG_CTRL_OFFSET		0x00 /* Control Register */
#define XDCFG_LOCK_OFFSET		0x04 /* Lock Register */
#define XDCFG_INT_STS_OFFSET		0x0C /* Interrupt Status Register */
#define XDCFG_INT_MASK_OFFSET		0x10 /* Interrupt Mask Register */
#define XDCFG_STATUS_OFFSET		0x14 /* Status Register */
#define XDCFG_DMA_SRC_ADDR_OFFSET	0x18 /* DMA Source Address Register */
#define XDCFG_DMA_DEST_ADDR_OFFSET	0x1C /* DMA Destination Address Reg */
#define XDCFG_DMA_SRC_LEN_OFFSET	0x20 /* DMA Source Transfer Length */
#define XDCFG_DMA_DEST_LEN_OFFSET	0x24 /* DMA Destination Transfer */
#define XDCFG_UNLOCK_OFFSET		0x34 /* Unlock Register */
#define XDCFG_MCTRL_OFFSET		0x80 /* Misc. Control Register */

/* Control Register Bit definitions */
#define XDCFG_CTRL_PCFG_PROG_B_MASK	0x40000000 /* Program signal to
						    *  Reset FPGA */
#define XDCFG_CTRL_PCAP_PR_MASK		0x08000000 /* Enable PCAP for PR */
#define XDCFG_CTRL_PCAP_MODE_MASK	0x04000000 /* Enable PCAP */
#define XDCFG_CTRL_PCAP_RATE_EN_MASK  0x02000000 /* Enable PCAP Quad Rate */
#define XDCFG_CTRL_PCFG_AES_EN_MASK	0x00000E00 /* AES Enable Mask */
#define XDCFG_CTRL_SEU_EN_MASK		0x00000100 /* SEU Enable Mask */
#define XDCFG_CTRL_SPNIDEN_MASK		0x00000040 /* Secure Non Invasive
						    *  Debug Enable */
#define XDCFG_CTRL_SPIDEN_MASK		0x00000020 /* Secure Invasive
						    *  Debug Enable */
#define XDCFG_CTRL_NIDEN_MASK		0x00000010 /* Non-Invasive Debug
						    *  Enable */
#define XDCFG_CTRL_DBGEN_MASK		0x00000008 /* Invasive Debug
						    *  Enable */
#define XDCFG_CTRL_DAP_EN_MASK		0x00000007 /* DAP Enable Mask */

/* Lock register bit definitions */

#define XDCFG_LOCK_AES_EN_MASK		0x00000008 /* Lock AES_EN update */
#define XDCFG_LOCK_SEU_MASK		0x00000004 /* Lock SEU_En update */
#define XDCFG_LOCK_DBG_MASK		0x00000001 /* This bit locks
						    *  security config
						    *  including: DAP_En,
						    *  DBGEN,NIDEN, SPNIEN */

/* Miscellaneous Control Register bit definitions */
#define XDCFG_MCTRL_PCAP_LPBK_MASK	0x00000010 /* Internal PCAP loopback */

/* Status register bit definitions */
#define XDCFG_STATUS_PCFG_INIT_MASK	0x00000010 /* FPGA init status */

/* Interrupt Status/Mask Register Bit definitions */
#define XDCFG_IXR_DMA_DONE_MASK		0x00002000 /* DMA Command Done */
#define XDCFG_IXR_D_P_DONE_MASK		0x00001000 /* DMA and PCAP Cmd Done */
#define XDCFG_IXR_PCFG_DONE_MASK	0x00000004 /* FPGA programmed */
#define XDCFG_IXR_ERROR_FLAGS_MASK	0x00F0F860
#define XDCFG_IXR_ALL_MASK		0xF8F7F87F
/* Miscellaneous constant values */
#define XDCFG_DMA_INVALID_ADDRESS	0xFFFFFFFF  /* Invalid DMA address */

static const char * const fclk_name[] = {
	"fclk0",
	"fclk1",
	"fclk2",
	"fclk3"
};
#define NUMFCLKS ARRAY_SIZE(fclk_name)

/**
 * struct xdevcfg_drvdata - Device Configuration driver structure
 *
 * @dev: Pointer to the device structure
 * @cdev: Instance of the cdev structure
 * @devt: Pointer to the dev_t structure
 * @class: Pointer to device class
 * @fclk_class: Pointer to fclk device class
 * @dma_done: The dma_done status bit for the DMA command completion
 * @error_status: The error status captured during the DMA transfer
 * @irq: Interrupt number
 * @clk: Peripheral clock for devcfg
 * @fclk: Array holding references to the FPGA clocks
 * @fclk_exported: Flag inidcating whether an FPGA clock is exported
 * @is_open: The status bit to indicate whether the device is opened
 * @sem: Instance for the mutex
 * @lock: Instance of spinlock
 * @base_address: The virtual device base address of the device registers
 * @is_partial_bitstream: Status bit to indicate partial/full bitstream
 */
struct xdevcfg_drvdata {
	struct device *dev;
	struct cdev cdev;
	dev_t devt;
	struct class *class;
	struct class *fclk_class;
	int irq;
	struct clk *clk;
	struct clk *fclk[NUMFCLKS];
	u8 fclk_exported[NUMFCLKS];
	volatile bool dma_done;
	volatile int error_status;
	bool is_open;
	struct mutex sem;
	spinlock_t lock;
	void __iomem *base_address;
	int ep107;
	bool is_partial_bitstream;
	bool endian_swap;
	char residue_buf[3];
	int residue_len;
};

/**
 * struct fclk_data - FPGA clock data
 * @clk: Pointer to clock
 * @enable: Flag indicating enable status of the clock
 * @rate_rnd: Rate to be rounded for round rate operation
 */
struct fclk_data {
	struct clk *clk;
	int enabled;
	unsigned long rate_rnd;
};

/* Register read/write access routines */
#define xdevcfg_writereg(offset, val)	__raw_writel(val, offset)
#define xdevcfg_readreg(offset)		__raw_readl(offset)

/**
 * xdevcfg_reset_pl() - Reset the programmable logic.
 * @base_address:	The base address of the device.
 *
 * Must be called with PCAP clock enabled
 */
static void xdevcfg_reset_pl(void __iomem *base_address)
{
	/*
	 * Create a rising edge on PCFG_INIT. PCFG_INIT follows PCFG_PROG_B,
	 * so we need to * poll it after setting PCFG_PROG_B to make sure that
	 * the rising edge happens.
	 */
	xdevcfg_writereg(base_address + XDCFG_CTRL_OFFSET,
			(xdevcfg_readreg(base_address + XDCFG_CTRL_OFFSET) |
			 XDCFG_CTRL_PCFG_PROG_B_MASK));
	while (!(xdevcfg_readreg(base_address + XDCFG_STATUS_OFFSET) &
				XDCFG_STATUS_PCFG_INIT_MASK))
		;

	xdevcfg_writereg(base_address + XDCFG_CTRL_OFFSET,
			(xdevcfg_readreg(base_address + XDCFG_CTRL_OFFSET) &
			 ~XDCFG_CTRL_PCFG_PROG_B_MASK));
	while (xdevcfg_readreg(base_address + XDCFG_STATUS_OFFSET) &
			XDCFG_STATUS_PCFG_INIT_MASK)
		;

	xdevcfg_writereg(base_address + XDCFG_CTRL_OFFSET,
			(xdevcfg_readreg(base_address + XDCFG_CTRL_OFFSET) |
			 XDCFG_CTRL_PCFG_PROG_B_MASK));
	while (!(xdevcfg_readreg(base_address + XDCFG_STATUS_OFFSET) &
				XDCFG_STATUS_PCFG_INIT_MASK))
		;
}

/**
 * xdevcfg_irq() - The main interrupt handler.
 * @irq:	The interrupt number.
 * @data:	Pointer to the driver data structure.
 * returns: IRQ_HANDLED after the interrupt is handled.
 **/
static irqreturn_t xdevcfg_irq(int irq, void *data)
{
	u32 intr_status;
	struct xdevcfg_drvdata *drvdata = data;

	spin_lock(&drvdata->lock);

	intr_status = xdevcfg_readreg(drvdata->base_address +
					XDCFG_INT_STS_OFFSET);

	/* Clear the interrupts */
	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_STS_OFFSET,
				intr_status);

	if ((intr_status & XDCFG_IXR_D_P_DONE_MASK) ==
				XDCFG_IXR_D_P_DONE_MASK)
		drvdata->dma_done = 1;

	if ((intr_status & XDCFG_IXR_ERROR_FLAGS_MASK) ==
			XDCFG_IXR_ERROR_FLAGS_MASK)
		drvdata->error_status = 1;

	spin_unlock(&drvdata->lock);

	return IRQ_HANDLED;
}

/**
 * xdevcfg_write() - The is the driver write function.
 *
 * @file:	Pointer to the file structure.
 * @buf:	Pointer to the bitstream location.
 * @count:	The number of bytes to be written.
 * @ppos:	Pointer to the offset value
 * returns:	Success or error status.
 **/
static ssize_t
xdevcfg_write(struct file *file, const char __user *buf, size_t count,
		loff_t *ppos)
{
	char *kbuf;
	int status;
	unsigned long timeout;
	u32 intr_reg, dma_len;
	dma_addr_t dma_addr;
	u32 transfer_length = 0;
	struct xdevcfg_drvdata *drvdata = file->private_data;
	size_t user_count = count;
	int i;

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	status = mutex_lock_interruptible(&drvdata->sem);

	if (status)
		goto err_clk;

	dma_len = count + drvdata->residue_len;
	kbuf = dma_alloc_coherent(drvdata->dev, dma_len, &dma_addr, GFP_KERNEL);
	if (!kbuf) {
		status = -ENOMEM;
		goto err_unlock;
	}

	/* Collect stragglers from last time (0 to 3 bytes) */
	memcpy(kbuf, drvdata->residue_buf, drvdata->residue_len);

	/* Fetch user data, appending to stragglers */
	if (copy_from_user(kbuf + drvdata->residue_len, buf, count)) {
		status = -EFAULT;
		goto error;
	}

	/* Include stragglers in total bytes to be handled */
	count += drvdata->residue_len;

	/* First block contains a header */
	if (*ppos == 0 && count > 4) {
		/* Look for sync word */
		for (i = 0; i < count - 4; i++) {
			if (memcmp(kbuf + i, "\x66\x55\x99\xAA", 4) == 0) {
				pr_debug("Found normal sync word\n");
				drvdata->endian_swap = 0;
				break;
			}
			if (memcmp(kbuf + i, "\xAA\x99\x55\x66", 4) == 0) {
				pr_debug("Found swapped sync word\n");
				drvdata->endian_swap = 1;
				break;
			}
		}
		/* Remove the header, aligning the data on word boundary */
		if (i != count - 4) {
			count -= i;
			memmove(kbuf, kbuf + i, count);
		}
	}

	/* Save stragglers for next time */
	drvdata->residue_len = count % 4;
	count -= drvdata->residue_len;
	memcpy(drvdata->residue_buf, kbuf + count, drvdata->residue_len);

	/* Fixup endianess of the data */
	if (drvdata->endian_swap) {
		for (i = 0; i < count; i += 4) {
			u32 *p = (u32 *)&kbuf[i];
			*p = swab32(*p);
		}
	}

	/* Enable DMA and error interrupts */
	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_STS_OFFSET,
				XDCFG_IXR_ALL_MASK);


	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_MASK_OFFSET,
				(u32) (~(XDCFG_IXR_D_P_DONE_MASK |
				XDCFG_IXR_ERROR_FLAGS_MASK)));

	drvdata->dma_done = 0;
	drvdata->error_status = 0;

	/* Initiate DMA write command */
	if (count < 0x1000)
		xdevcfg_writereg(drvdata->base_address +
			XDCFG_DMA_SRC_ADDR_OFFSET, (u32)(dma_addr + 1));
	else
		xdevcfg_writereg(drvdata->base_address +
			XDCFG_DMA_SRC_ADDR_OFFSET, (u32) dma_addr);

	xdevcfg_writereg(drvdata->base_address + XDCFG_DMA_DEST_ADDR_OFFSET,
				(u32)XDCFG_DMA_INVALID_ADDRESS);
	/* Convert number of bytes to number of words.  */
	if (count % 4)
		transfer_length	= (count / 4 + 1);
	else
		transfer_length	= count / 4;
	xdevcfg_writereg(drvdata->base_address + XDCFG_DMA_SRC_LEN_OFFSET,
				transfer_length);
	xdevcfg_writereg(drvdata->base_address + XDCFG_DMA_DEST_LEN_OFFSET, 0);

	timeout = jiffies + msecs_to_jiffies(1000);

	while (!drvdata->dma_done) {
		if (time_after(jiffies, timeout)) {
				status = -ETIMEDOUT;
				goto error;
		}
	}

	if (drvdata->error_status)
		status = drvdata->error_status;

	/* Disable the DMA and error interrupts */
	intr_reg = xdevcfg_readreg(drvdata->base_address +
					XDCFG_INT_MASK_OFFSET);
	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_MASK_OFFSET,
				intr_reg | (XDCFG_IXR_D_P_DONE_MASK |
				XDCFG_IXR_ERROR_FLAGS_MASK));

	/* If we didn't write correctly, then bail out. */
	if (status) {
		status = -EFAULT;
		goto error;
	}

	*ppos += user_count;
	status = user_count;

error:
	dma_free_coherent(drvdata->dev, dma_len, kbuf, dma_addr);
err_unlock:
	mutex_unlock(&drvdata->sem);
err_clk:
	clk_disable(drvdata->clk);
	return status;
}


/**
 * xdevcfg_read() - The is the driver read function.
 * @file:	Pointer to the file structure.
 * @buf:	Pointer to the bitstream location.
 * @count:	The number of bytes read.
 * @ppos:	Pointer to the offsetvalue
 * returns:	Success or error status.
 */
static ssize_t
xdevcfg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	u32 *kbuf;
	int status;
	unsigned long timeout;
	dma_addr_t dma_addr;
	struct xdevcfg_drvdata *drvdata = file->private_data;
	u32 intr_reg;

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	status = mutex_lock_interruptible(&drvdata->sem);
	if (status)
		goto err_clk;

	/* Get new data from the ICAP, and return was requested. */
	kbuf = dma_alloc_coherent(drvdata->dev, count, &dma_addr, GFP_KERNEL);
	if (!kbuf) {
		status = -ENOMEM;
		goto err_unlock;
	}

	drvdata->dma_done = 0;
	drvdata->error_status = 0;

	/* Enable DMA and error interrupts */
	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_STS_OFFSET,
				XDCFG_IXR_ALL_MASK);

	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_MASK_OFFSET,
				(u32) (~(XDCFG_IXR_D_P_DONE_MASK |
				XDCFG_IXR_ERROR_FLAGS_MASK)));
	/* Initiate DMA read command */
	xdevcfg_writereg(drvdata->base_address + XDCFG_DMA_SRC_ADDR_OFFSET,
				(u32)XDCFG_DMA_INVALID_ADDRESS);
	xdevcfg_writereg(drvdata->base_address + XDCFG_DMA_DEST_ADDR_OFFSET,
				(u32)dma_addr);
	xdevcfg_writereg(drvdata->base_address + XDCFG_DMA_SRC_LEN_OFFSET, 0);
	xdevcfg_writereg(drvdata->base_address + XDCFG_DMA_DEST_LEN_OFFSET,
				count / 4);

	timeout = jiffies + msecs_to_jiffies(1000);

	while (!drvdata->dma_done) {
		if (time_after(jiffies, timeout)) {
			status = -ETIMEDOUT;
			goto error;
		}
	}

	if (drvdata->error_status)
		status = drvdata->error_status;

	/* Disable and clear DMA and error interrupts */
	intr_reg = xdevcfg_readreg(drvdata->base_address +
					XDCFG_INT_MASK_OFFSET);
	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_MASK_OFFSET,
				intr_reg | (XDCFG_IXR_D_P_DONE_MASK |
				XDCFG_IXR_ERROR_FLAGS_MASK));


	/* If we didn't read correctly, then bail out. */
	if (status) {
		status = -EFAULT;
		goto error;
	}

	/* If we fail to return the data to the user, then bail out. */
	if (copy_to_user(buf, kbuf, count)) {
		status = -EFAULT;
		goto error;
	}

	status = count;
error:
	dma_free_coherent(drvdata->dev, count, kbuf, dma_addr);
err_unlock:
	mutex_unlock(&drvdata->sem);
err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_open() - The is the driver open function.
 * @inode:	Pointer to the inode structure of this device.
 * @file:	Pointer to the file structure.
 * returns:	Success or error status.
 */
static int xdevcfg_open(struct inode *inode, struct file *file)
{
	struct xdevcfg_drvdata *drvdata;
	int status;

	drvdata = container_of(inode->i_cdev, struct xdevcfg_drvdata, cdev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	status = mutex_lock_interruptible(&drvdata->sem);
	if (status)
		goto err_clk;

	if (drvdata->is_open) {
		status = -EBUSY;
		goto error;
	}

	file->private_data = drvdata;
	drvdata->is_open = 1;
	drvdata->endian_swap = 0;
	drvdata->residue_len= 0;

	/*
	 * If is_partial_bitstream is set, then PROG_B is not asserted
	 * (xdevcfg_reset_pl function) and also zynq_slcr_init_preload_fpga and
	 * zynq_slcr_init_postload_fpga functions are not invoked.
	 */
	if (!drvdata->is_partial_bitstream)
		zynq_slcr_init_preload_fpga();

	/*
	 * Only do the reset of the PL for Zynq as it causes problems on the
	 * EP107 and the issue is not understood, but not worth investigating
	 * as the emulation platform is very different than silicon and not a
	 * complete implementation. Also, do not reset if it is a partial
	 * bitstream.
	 */
	if ((!drvdata->ep107) && (!drvdata->is_partial_bitstream))
		xdevcfg_reset_pl(drvdata->base_address);

	xdevcfg_writereg(drvdata->base_address + XDCFG_INT_STS_OFFSET,
			XDCFG_IXR_PCFG_DONE_MASK);

error:
	mutex_unlock(&drvdata->sem);
err_clk:
	clk_disable(drvdata->clk);
	return status;
}

/**
 * xdevcfg_release() - The is the driver release function.
 * @inode:	Pointer to the inode structure of this device.
 * @file:	Pointer to the file structure.
 * returns:	Success.
 */
static int xdevcfg_release(struct inode *inode, struct file *file)
{
	struct xdevcfg_drvdata *drvdata = file->private_data;

	if (!drvdata->is_partial_bitstream)
		zynq_slcr_init_postload_fpga();

	if (drvdata->residue_len)
		printk("Did not transfer last %d bytes\n",
			drvdata->residue_len);

	drvdata->is_open = 0;

	return 0;
}

static const struct file_operations xdevcfg_fops = {
	.owner = THIS_MODULE,
	.write = xdevcfg_write,
	.read = xdevcfg_read,
	.open = xdevcfg_open,
	.release = xdevcfg_release,
};

/*
 * The following functions are the routines provided to the user to
 * set/get the status bit value in the control/lock registers.
 */

/**
 * xdevcfg_set_dap_en() - This function sets the DAP bits in the
 * control register with the given value.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	negative error if the string could not be converted
 *		or the size of the buffer.
 */
static ssize_t xdevcfg_set_dap_en(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 ctrl_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	int status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	ctrl_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET);
	spin_lock_irqsave(&drvdata->lock, flags);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_unlock;

	if (mask_bit > 7) {
		status = -EINVAL;
		goto err_unlock;
	}

	xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
			(ctrl_reg_status |
			 (((u32)mask_bit) & XDCFG_CTRL_DAP_EN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_unlock:
	spin_unlock_irqrestore(&drvdata->lock, flags);
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_dap_en_status() - The function returns the DAP_EN bits status in
 * the control register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	Size of the buffer.
 */
static ssize_t xdevcfg_show_dap_en_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 dap_en_status;
	int status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	dap_en_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET) & XDCFG_CTRL_DAP_EN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", dap_en_status);

	return status;
}

static DEVICE_ATTR(enable_dap, 0644, xdevcfg_show_dap_en_status,
				xdevcfg_set_dap_en);

/**
 * xdevcfg_set_dbgen() - This function sets the DBGEN bit in the
 * control register with the given value.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_dbgen(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 ctrl_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	int status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	ctrl_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status | XDCFG_CTRL_DBGEN_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status & (~XDCFG_CTRL_DBGEN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_dbgen_status() - The function returns the DBGEN bit status in
 * the control register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	Size of the buffer.
 */
static ssize_t xdevcfg_show_dbgen_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 dbgen_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	dbgen_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET) & XDCFG_CTRL_DBGEN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (dbgen_status >> 3));

	return status;
}

static DEVICE_ATTR(enable_dbg_in, 0644, xdevcfg_show_dbgen_status,
				xdevcfg_set_dbgen);

/**
 * xdevcfg_set_niden() - This function sets the NIDEN bit in the
 * control register with the given value.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_niden(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 ctrl_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	int status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	ctrl_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status | XDCFG_CTRL_NIDEN_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status & (~XDCFG_CTRL_NIDEN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_niden_status() - The function returns the NIDEN bit status in
 * the control register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	Size of the buffer.
 */
static ssize_t xdevcfg_show_niden_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 niden_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	niden_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET) & XDCFG_CTRL_NIDEN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (niden_status >> 4));

	return status;
}

static DEVICE_ATTR(enable_dbg_nonin, 0644, xdevcfg_show_niden_status,
			xdevcfg_set_niden);

/**
 * xdevcfg_set_spiden() - This function sets the SPIDEN bit in the
 * control register with the given value.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_spiden(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 ctrl_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	int status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	ctrl_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status | XDCFG_CTRL_SPIDEN_MASK));
	else

		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status & (~XDCFG_CTRL_SPIDEN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_spiden_status() - The function returns the SPIDEN bit status in
 * the control register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	Size of the buffer.
 */
static ssize_t xdevcfg_show_spiden_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 spiden_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	spiden_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_CTRL_OFFSET) & XDCFG_CTRL_SPIDEN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (spiden_status >> 5));

	return status;
}

static DEVICE_ATTR(enable_sec_dbg_in, 0644, xdevcfg_show_spiden_status,
				xdevcfg_set_spiden);

/**
 * xdevcfg_set_spniden() - This function sets the SPNIDEN bit in the
 * control register with the given value.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or the size of buffer
 */
static ssize_t xdevcfg_set_spniden(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 ctrl_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	ctrl_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET);
	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status | XDCFG_CTRL_SPNIDEN_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status & (~XDCFG_CTRL_SPNIDEN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_spniden_status() - The function returns the SPNIDEN bit status
 * in the control register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	Size of the buffer.
 */
static ssize_t xdevcfg_show_spniden_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 spniden_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	spniden_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_CTRL_OFFSET) & XDCFG_CTRL_SPNIDEN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (spniden_status >> 6));

	return status;
}

static DEVICE_ATTR(enable_sec_dbg_nonin, 0644, xdevcfg_show_spniden_status,
					xdevcfg_set_spniden);

/**
 * xdevcfg_set_seu() - This function sets the SEU_EN bit in the
 * control register with the given value
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_seu(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 ctrl_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	ctrl_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status | XDCFG_CTRL_SEU_EN_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status & (~XDCFG_CTRL_SEU_EN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_seu_status() - The function returns the SEU_EN bit status
 * in the control register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	size of the buffer.
 */
static ssize_t xdevcfg_show_seu_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 seu_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	seu_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_CTRL_OFFSET) & XDCFG_CTRL_SEU_EN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (seu_status > 8));

	return status;
}

static DEVICE_ATTR(enable_seu, 0644, xdevcfg_show_seu_status, xdevcfg_set_seu);

/**
 * xdevcfg_set_aes() - This function sets the AES_EN bits in the
 * control register with either all 1s or all 0s.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 *
 * The user must send only one bit in the buffer to notify whether he wants to
 * either set or reset these bits.
 */
static ssize_t xdevcfg_set_aes(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 ctrl_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	int status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	ctrl_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_CTRL_OFFSET);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status < 0)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}


	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status |
				 XDCFG_CTRL_PCFG_AES_EN_MASK |
				 XDCFG_CTRL_PCAP_RATE_EN_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(ctrl_reg_status &
				 ~(XDCFG_CTRL_PCFG_AES_EN_MASK |
				 XDCFG_CTRL_PCAP_RATE_EN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_aes_status() - The function returns the AES_EN bit status
 * in the control register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	size of the buffer.
 */
static ssize_t xdevcfg_show_aes_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 aes_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	aes_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_CTRL_OFFSET) & XDCFG_CTRL_PCFG_AES_EN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (aes_status >> 9));

	return status;
}

static DEVICE_ATTR(enable_aes, 0644, xdevcfg_show_aes_status, xdevcfg_set_aes);

/**
 * xdevcfg_set_aes_en_lock() - This function sets the LOCK_AES_EN bit in the
 * lock register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_aes_en_lock(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 aes_en_lock_status;
	unsigned long flags;
	unsigned long mask_bit;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	aes_en_lock_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_LOCK_OFFSET);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_LOCK_OFFSET,
				(aes_en_lock_status | XDCFG_LOCK_AES_EN_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_LOCK_OFFSET,
				(aes_en_lock_status &
				 (~XDCFG_LOCK_AES_EN_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_aes_en_lock_status() - The function returns the LOCK_AES_EN bit
 * status in the lock register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	size of the buffer.
 */
static ssize_t xdevcfg_show_aes_en_lock_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 aes_en_lock_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	aes_en_lock_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_LOCK_OFFSET) & XDCFG_LOCK_AES_EN_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (aes_en_lock_status >> 3));

	return status;
}

static DEVICE_ATTR(aes_en_lock, 0644, xdevcfg_show_aes_en_lock_status,
				xdevcfg_set_aes_en_lock);

/**
 * xdevcfg_set_seu_lock() - This function sets the LOCK_SEU bit in the
 * lock register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_seu_lock(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 seu_lock_status;
	unsigned long flags;
	unsigned long mask_bit;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	seu_lock_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_LOCK_OFFSET);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_LOCK_OFFSET,
				(seu_lock_status | XDCFG_LOCK_SEU_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_LOCK_OFFSET,
				(seu_lock_status  & (~XDCFG_LOCK_SEU_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_seu_lock_status() - The function returns the LOCK_SEU bit
 * status in the lock register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	size of the buffer.
 */
static ssize_t xdevcfg_show_seu_lock_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 seu_lock_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	seu_lock_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_LOCK_OFFSET) & XDCFG_LOCK_SEU_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (seu_lock_status >> 2));

	return status;
}

static DEVICE_ATTR(seu_lock, 0644, xdevcfg_show_seu_lock_status,
					xdevcfg_set_seu_lock);

/**
 * xdevcfg_set_dbg_lock() - This function sets the LOCK_DBG bit in the
 * lock register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_dbg_lock(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u32 lock_reg_status;
	unsigned long flags;
	unsigned long mask_bit;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	lock_reg_status = xdevcfg_readreg(drvdata->base_address +
				XDCFG_LOCK_OFFSET);
	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		goto err_clk;

	if (mask_bit > 1) {
		status = -EINVAL;
		goto err_clk;
	}

	spin_lock_irqsave(&drvdata->lock, flags);

	if (mask_bit)
		xdevcfg_writereg(drvdata->base_address + XDCFG_LOCK_OFFSET,
				(lock_reg_status | XDCFG_LOCK_DBG_MASK));
	else
		xdevcfg_writereg(drvdata->base_address + XDCFG_LOCK_OFFSET,
				(lock_reg_status & (~XDCFG_LOCK_DBG_MASK)));

	spin_unlock_irqrestore(&drvdata->lock, flags);

	clk_disable(drvdata->clk);

	return size;

err_clk:
	clk_disable(drvdata->clk);

	return status;
}

/**
 * xdevcfg_show_dbg_lock_status() - The function returns the LOCK_DBG bit
 * status in the lock register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	size of the buffer.
 */
static ssize_t xdevcfg_show_dbg_lock_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 dbg_lock_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	dbg_lock_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_LOCK_OFFSET) & XDCFG_LOCK_DBG_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", dbg_lock_status);

	return status;
}

static DEVICE_ATTR(dbg_lock, 0644, xdevcfg_show_dbg_lock_status,
				xdevcfg_set_dbg_lock);

/**
 * xdevcfg_show_prog_done_status() - The function returns the PROG_DONE bit
 * status in the interrupt status register.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	size of the buffer.
 */
static ssize_t xdevcfg_show_prog_done_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 prog_done_status;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = clk_enable(drvdata->clk);
	if (status)
		return status;

	prog_done_status = xdevcfg_readreg(drvdata->base_address +
			XDCFG_INT_STS_OFFSET) & XDCFG_IXR_PCFG_DONE_MASK;

	clk_disable(drvdata->clk);

	status = sprintf(buf, "%d\n", (prog_done_status >> 2));

	return status;
}

static DEVICE_ATTR(prog_done, 0644, xdevcfg_show_prog_done_status,
				NULL);

/**
 * xdevcfg_set_is_partial_bitstream() - This function sets the
 * is_partial_bitstream variable. If is_partial_bitstream is set,
 * then PROG_B is not asserted (xdevcfg_reset_pl) and also
 * zynq_slcr_init_preload_fpga and zynq_slcr_init_postload_fpga functions
 * are not invoked.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	-EINVAL if invalid parameter is sent or size
 */
static ssize_t xdevcfg_set_is_partial_bitstream(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long mask_bit;
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = strict_strtoul(buf, 10, &mask_bit);

	if (status)
		return status;

	if (mask_bit > 1)
		return -EINVAL;

	if (mask_bit)
		drvdata->is_partial_bitstream = 1;
	else
		drvdata->is_partial_bitstream = 0;

	return size;
}

/**
 * xdevcfg_show_is_partial_bitstream_status() - The function returns the
 * value of is_partial_bitstream variable.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	size of the buffer.
 */
static ssize_t xdevcfg_show_is_partial_bitstream_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t status;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	status = sprintf(buf, "%d\n", drvdata->is_partial_bitstream);

	return status;
}

static DEVICE_ATTR(is_partial_bitstream, 0644,
				xdevcfg_show_is_partial_bitstream_status,
				xdevcfg_set_is_partial_bitstream);

static const struct attribute *xdevcfg_attrs[] = {
	&dev_attr_prog_done.attr, /* PCFG_DONE bit in Intr Status register */
	&dev_attr_dbg_lock.attr, /* Debug lock bit in Lock register */
	&dev_attr_seu_lock.attr, /* SEU lock bit in Lock register */
	&dev_attr_aes_en_lock.attr, /* AES EN lock bit in Lock register */
	&dev_attr_enable_aes.attr, /* AES EN bit in Control register */
	&dev_attr_enable_seu.attr, /* SEU EN bit in Control register */
	&dev_attr_enable_sec_dbg_nonin.attr, /*SPNIDEN bit in Control register*/
	&dev_attr_enable_sec_dbg_in.attr, /*SPIDEN bit in Control register */
	&dev_attr_enable_dbg_nonin.attr, /* NIDEN bit in Control register */
	&dev_attr_enable_dbg_in.attr, /* DBGEN bit in Control register */
	&dev_attr_enable_dap.attr, /* DAP_EN bits in Control register */
	&dev_attr_is_partial_bitstream.attr, /* Flag for partial bitstream */
	NULL,
};


static const struct attribute_group xdevcfg_attr_group = {
	.attrs = (struct attribute **) xdevcfg_attrs,
};

static ssize_t fclk_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fclk_data *pdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", pdata->enabled);
}

static ssize_t fclk_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long enable;
	int ret;
	struct fclk_data *pdata = dev_get_drvdata(dev);

	ret = kstrtoul(buf, 0, &enable);
	if (ret)
		return -EINVAL;

	enable = !!enable;
	if (enable == pdata->enabled)
		return count;

	if (enable)
		ret = clk_enable(pdata->clk);
	else
		clk_disable(pdata->clk);

	if (ret)
		return ret;

	pdata->enabled = enable;
	return count;
}

static DEVICE_ATTR(enable, 0644, fclk_enable_show, fclk_enable_store);

static ssize_t fclk_set_rate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fclk_data *pdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%lu\n", clk_get_rate(pdata->clk));
}

static ssize_t fclk_set_rate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	unsigned long rate;
	struct fclk_data *pdata = dev_get_drvdata(dev);

	ret = kstrtoul(buf, 0, &rate);
	if (ret)
		return -EINVAL;

	rate = clk_round_rate(pdata->clk, rate);
	ret = clk_set_rate(pdata->clk, rate);

	return ret ? ret : count;
}

static DEVICE_ATTR(set_rate, 0644, fclk_set_rate_show, fclk_set_rate_store);

static ssize_t fclk_round_rate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fclk_data *pdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%lu => %lu\n", pdata->rate_rnd,
			clk_round_rate(pdata->clk, pdata->rate_rnd));
}

static ssize_t fclk_round_rate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	unsigned long rate;
	struct fclk_data *pdata = dev_get_drvdata(dev);

	ret = kstrtoul(buf, 0, &rate);
	if (ret)
		return -EINVAL;

	pdata->rate_rnd = rate;

	return count;
}

static DEVICE_ATTR(round_rate, 0644, fclk_round_rate_show,
		fclk_round_rate_store);

static const struct attribute *fclk_ctrl_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_set_rate.attr,
	&dev_attr_round_rate.attr,
	NULL,
};

static const struct attribute_group fclk_ctrl_attr_grp = {
	.attrs = (struct attribute **)fclk_ctrl_attrs,
};

static ssize_t xdevcfg_fclk_export_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int i, ret;
	struct device *subdev;
	struct fclk_data *fdata;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	for (i = 0; i < NUMFCLKS; i++) {
		if (!strncmp(buf, fclk_name[i], strlen(fclk_name[i])))
			break;
	}

	if (i < NUMFCLKS && !drvdata->fclk_exported[i]) {
		drvdata->fclk_exported[i] = 1;
		subdev = device_create(drvdata->fclk_class, dev, MKDEV(0, 0),
				NULL, fclk_name[i]);
		if (IS_ERR(subdev))
			return PTR_ERR(subdev);
		ret = clk_prepare(drvdata->fclk[i]);
		if (ret)
			return ret;
		fdata = kzalloc(sizeof(*fdata), GFP_KERNEL);
		if (!fdata) {
			ret = -ENOMEM;
			goto err_unprepare;
		}
		fdata->clk = drvdata->fclk[i];
		dev_set_drvdata(subdev, fdata);
		ret = sysfs_create_group(&subdev->kobj, &fclk_ctrl_attr_grp);
		if (ret)
			goto err_free;
	} else {
		return -EINVAL;
	}

	return size;

err_free:
	kfree(fdata);
err_unprepare:
	clk_unprepare(drvdata->fclk[i]);

	return ret;
}

static ssize_t xdevcfg_fclk_export_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t count = 0;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	for (i = 0; i < NUMFCLKS; i++) {
		if (!drvdata->fclk_exported[i])
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"%s\n", fclk_name[i]);
	}
	return count;
}

static DEVICE_ATTR(fclk_export, 0644, xdevcfg_fclk_export_show,
		xdevcfg_fclk_export_store);

static int match_fclk(struct device *dev, const void *data)
{
	struct fclk_data *fdata = dev_get_drvdata(dev);

	return fdata->clk == data;
}

static ssize_t xdevcfg_fclk_unexport_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int i;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	for (i = 0; i < NUMFCLKS; i++) {
		if (!strncmp(buf, fclk_name[i], strlen(fclk_name[i])))
			break;
	}

	if (i < NUMFCLKS && drvdata->fclk_exported[i]) {
		struct fclk_data *fdata;
		struct device *subdev;

		drvdata->fclk_exported[i] = 0;
		subdev = class_find_device(drvdata->fclk_class, NULL,
				drvdata->fclk[i], match_fclk);
		fdata = dev_get_drvdata(subdev);
		if (fdata->enabled)
			clk_disable(fdata->clk);
		clk_unprepare(fdata->clk);
		kfree(fdata);
		device_unregister(subdev);
		put_device(subdev);
	} else {
		return -EINVAL;
	}

	return size;
}

static ssize_t xdevcfg_fclk_unexport_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t count = 0;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	for (i = 0; i < NUMFCLKS; i++) {
		if (drvdata->fclk_exported[i])
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"%s\n", fclk_name[i]);
	}
	return count;
}

static DEVICE_ATTR(fclk_unexport, 0644, xdevcfg_fclk_unexport_show,
		xdevcfg_fclk_unexport_store);

static const struct attribute *fclk_exp_attrs[] = {
	&dev_attr_fclk_export.attr,
	&dev_attr_fclk_unexport.attr,
	NULL,
};

static const struct attribute_group fclk_exp_attr_grp = {
	.attrs = (struct attribute **)fclk_exp_attrs,
};

static void xdevcfg_fclk_init(struct device *dev)
{
	int i;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	for (i = 0; i < NUMFCLKS; i++) {
		drvdata->fclk[i] = clk_get(dev, fclk_name[i]);
		if (IS_ERR(drvdata->fclk[i])) {
			dev_warn(dev, "fclk not found\n");
			return;
		}
	}

	drvdata->fclk_class = class_create(THIS_MODULE, "fclk");
	if (IS_ERR(drvdata->fclk_class)) {
		dev_warn(dev, "failed to create fclk class\n");
		return;
	}

	if (sysfs_create_group(&dev->kobj, &fclk_exp_attr_grp))
		dev_warn(dev, "failed to create sysfs entries\n");
}

static void xdevcfg_fclk_remove(struct device *dev)
{
	int i;
	struct xdevcfg_drvdata *drvdata = dev_get_drvdata(dev);

	for (i = 0; i < NUMFCLKS; i++) {
		if (drvdata->fclk_exported[i]) {
			struct fclk_data *fdata;
			struct device *subdev;

			drvdata->fclk_exported[i] = 0;
			subdev = class_find_device(drvdata->fclk_class, NULL,
					drvdata->fclk[i], match_fclk);
			fdata = dev_get_drvdata(subdev);
			if (fdata->enabled)
				clk_disable(fdata->clk);
			clk_unprepare(fdata->clk);
			kfree(fdata);
			device_unregister(subdev);
			put_device(subdev);

		}
	}

	class_destroy(drvdata->fclk_class);
	sysfs_remove_group(&dev->kobj, &fclk_exp_attr_grp);

	return;
}

/**
 * xdevcfg_drv_probe -  Probe call for the device.
 *
 * @pdev:	handle to the platform device structure.
 * Returns 0 on success, negative error otherwise.
 *
 * It does all the memory allocation and registration for the device.
 */
static int xdevcfg_drv_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct xdevcfg_drvdata *drvdata;
	dev_t devt;
	int retval;
	u32 ctrlreg;
	struct device_node *np;
	const void *prop;
	int size;
	struct device *dev;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drvdata->base_address = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drvdata->base_address))
		return PTR_ERR(drvdata->base_address);

	drvdata->irq = platform_get_irq(pdev, 0);
	retval = devm_request_irq(&pdev->dev, drvdata->irq, &xdevcfg_irq,
				0, dev_name(&pdev->dev), drvdata);
	if (retval) {
		dev_err(&pdev->dev, "No IRQ available");
		return retval;
	}

	platform_set_drvdata(pdev, drvdata);
	spin_lock_init(&drvdata->lock);
	mutex_init(&drvdata->sem);
	drvdata->is_open = 0;
	drvdata->is_partial_bitstream = 0;
	drvdata->dma_done = 0;
	drvdata->error_status = 0;
	dev_info(&pdev->dev, "ioremap %pa to %p\n",
		 &res->start, drvdata->base_address);

	drvdata->clk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(drvdata->clk)) {
		dev_err(&pdev->dev, "input clock not found\n");
		return PTR_ERR(drvdata->clk);
	}

	retval = clk_prepare_enable(drvdata->clk);
	if (retval) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		return retval;
	}

	/*
	 * Figure out from the device tree if this is running on the EP107
	 * emulation platform as it doesn't match the silicon exactly and the
	 * driver needs to work accordingly.
	 */
	np = of_get_next_parent(pdev->dev.of_node);
	np = of_get_next_parent(np);
	prop = of_get_property(np, "compatible", &size);

	if (prop != NULL) {
		if ((strcmp((const char *)prop, "xlnx,zynq-ep107")) == 0)
			drvdata->ep107 = 1;
		else
			drvdata->ep107 = 0;
	}

	/* Unlock the device */
	xdevcfg_writereg(drvdata->base_address + XDCFG_UNLOCK_OFFSET,
				0x757BDF0D);

	/*
	 * Set the configuration register with the following options
	 *  - Reset FPGA
	 *  - Enable PCAP interface for Partial reconfiguration
	 *  - Enable the PCAP interface
	 *  - Set the throughput rate for maximum speed
	 *  - Se the CPU in user mode
	 */
	ctrlreg = xdevcfg_readreg(drvdata->base_address + XDCFG_CTRL_OFFSET);
	xdevcfg_writereg(drvdata->base_address + XDCFG_CTRL_OFFSET,
				(XDCFG_CTRL_PCFG_PROG_B_MASK |
				XDCFG_CTRL_PCAP_PR_MASK |
				XDCFG_CTRL_PCAP_MODE_MASK |
				ctrlreg));

	/* Ensure internal PCAP loopback is disabled */
	ctrlreg = xdevcfg_readreg(drvdata->base_address + XDCFG_MCTRL_OFFSET);
	xdevcfg_writereg(drvdata->base_address + XDCFG_MCTRL_OFFSET,
				(~XDCFG_MCTRL_PCAP_LPBK_MASK &
				ctrlreg));


	retval = alloc_chrdev_region(&devt, 0, XDEVCFG_DEVICES, DRIVER_NAME);
	if (retval < 0)
		goto failed5;

	drvdata->devt = devt;

	cdev_init(&drvdata->cdev, &xdevcfg_fops);
	drvdata->cdev.owner = THIS_MODULE;
	retval = cdev_add(&drvdata->cdev, devt, 1);
	if (retval) {
		dev_err(&pdev->dev, "cdev_add() failed\n");
		goto failed6;
	}

	drvdata->class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(drvdata->class)) {
		dev_err(&pdev->dev, "failed to create class\n");
		goto failed6;
	}

	dev = device_create(drvdata->class, &pdev->dev, devt, drvdata,
			DRIVER_NAME);
	if (IS_ERR(dev)) {
			dev_err(&pdev->dev, "unable to create device\n");
			goto failed7;
	}

	/* create sysfs files for the device */
	retval = sysfs_create_group(&(pdev->dev.kobj), &xdevcfg_attr_group);
	if (retval) {
		dev_err(&pdev->dev, "Failed to create sysfs attr group\n");
		cdev_del(&drvdata->cdev);
		goto failed8;
	}

	xdevcfg_fclk_init(&pdev->dev);

	clk_disable(drvdata->clk);

	return 0;		/* Success */

failed8:
	device_destroy(drvdata->class, drvdata->devt);
failed7:
	class_destroy(drvdata->class);
failed6:
	/* Unregister char driver */
	unregister_chrdev_region(devt, XDEVCFG_DEVICES);
failed5:
	clk_disable_unprepare(drvdata->clk);

	return retval;
}

/**
 * xdevcfg_drv_remove -  Remove call for the device.
 *
 * @pdev:	handle to the platform device structure.
 * Returns 0 or error status.
 *
 * Unregister the device after releasing the resources.
 */
static int xdevcfg_drv_remove(struct platform_device *pdev)
{
	struct xdevcfg_drvdata *drvdata;

	drvdata = platform_get_drvdata(pdev);

	if (!drvdata)
		return -ENODEV;

	unregister_chrdev_region(drvdata->devt, XDEVCFG_DEVICES);

	sysfs_remove_group(&pdev->dev.kobj, &xdevcfg_attr_group);

	xdevcfg_fclk_remove(&pdev->dev);
	device_destroy(drvdata->class, drvdata->devt);
	class_destroy(drvdata->class);
	cdev_del(&drvdata->cdev);
	clk_unprepare(drvdata->clk);

	return 0;		/* Success */
}

static struct of_device_id xdevcfg_of_match[] = {
	{ .compatible = "xlnx,zynq-devcfg-1.0", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, xdevcfg_of_match);

/* Driver Structure */
static struct platform_driver xdevcfg_platform_driver = {
	.probe = xdevcfg_drv_probe,
	.remove = xdevcfg_drv_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = xdevcfg_of_match,
	},
};

module_platform_driver(xdevcfg_platform_driver);

MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("Xilinx Device Config Driver");
MODULE_LICENSE("GPL");

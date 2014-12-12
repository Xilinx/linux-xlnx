/*
 * xvdma.c
 *
 * Xilinx Video DMA Driver
 *
 * Author: Xilinx Inc.
 *
 * 2002-2006 (c)Xilinx Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/amba/xilinx_dma.h>
#include <linux/cdev.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>
#include "xvdma.h"


static struct xvdma_dev *xvdma_dev_info[MAX_DEVICES + 1];
static u64 dma_mask = 0xFFFFFFFFUL;
static struct chan_buf chan_buf[MAX_FRAMES];
static u32 num_devices;
static struct completion cmp;

static void xvdma_get_dev_info(u32 device_id, struct xvdma_dev *dev)
{
	int i;

	for (i = 0; i < MAX_DEVICES; i++) {
		if (xvdma_dev_info[i]->device_id == device_id)
			break;
	}
	memcpy(dev, xvdma_dev_info[i], sizeof(struct xvdma_dev));
}

/*
 * This function is called when an application opens handle to the
 * bridge driver.
 */
static int xvdma_open(struct inode *ip, struct file *filp)
{
	return 0;
}

static int xvdma_release(struct inode *ip, struct file *filp)
{
	return 0;
}


static long xvdma_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct xvdma_dev xvdma_dev;
	struct xvdma_chan_cfg chan_cfg;
	struct xvdma_buf_info buf_info;
	struct xvdma_transfer tx_info;
	u32 devices, chan;

	switch (cmd) {
	case XVDMA_GET_NUM_DEVICES:
	{
		if (copy_from_user((void *)&devices,
			(const void __user *)arg,
			sizeof(u32)))
			return -EFAULT;

		devices = num_devices;
		 if (copy_to_user((u32 __user *)arg,
			&devices, sizeof(u32)))
			return -EFAULT;
		break;
	}
	case XVDMA_GET_DEV_INFO:
	{
		if (copy_from_user((void *)&xvdma_dev,
			(const void __user *)arg,
				sizeof(struct xvdma_dev)))
			return -EFAULT;

		xvdma_get_dev_info(xvdma_dev.device_id, &xvdma_dev);

		if (copy_to_user((struct xvdma_dev __user *)arg,
			&xvdma_dev, sizeof(struct xvdma_dev)))
			return -EFAULT;
		break;
	}
	case XVDMA_DEVICE_CONTROL:
	{
		if (copy_from_user((void *)&chan_cfg,
				(const void __user *)arg,
				sizeof(struct xvdma_chan_cfg)))
			return -EFAULT;

		xvdma_device_control(&chan_cfg);
		break;
	}
	case XVDMA_PREP_BUF:
	{
		if (copy_from_user((void *)&buf_info,
				(const void __user *)arg,
				sizeof(struct xvdma_buf_info)))
			return -EFAULT;
		xvdma_prep_slave_sg(&buf_info);
		break;
	}
	case XVDMA_START_TRANSFER:
	{
		if (copy_from_user((void *)&tx_info,
				(const void __user *)arg,
				sizeof(struct xvdma_transfer)))
			return -EFAULT;

		xvdma_start_transfer(&tx_info);
		break;
	}
	case XVDMA_STOP_TRANSFER:
	{
		if (copy_from_user((void *)&chan,
				(const void __user *)arg,
				sizeof(u32)))
			return -EFAULT;

		xvdma_stop_transfer((struct dma_chan *)chan);
		break;
	}
	default:
		break;
	}
	return 0;
}

static bool xvdma_filter(struct dma_chan *chan, void *param)
{
	if (*((int *)chan->private) == *(int *)param)
		return true;


	return false;
}

static void vdma_sync_callback(void *completion)
{
	complete(completion);
}

void xvdma_stop_transfer(struct dma_chan *chan)
{
	struct dma_device *chan_dev;

	if (chan) {
		chan_dev = chan->device;
		chan_dev->device_control(chan, DMA_TERMINATE_ALL,
					(unsigned long)NULL);
	}
}

void xvdma_start_transfer(struct xvdma_transfer *tx_info)
{
	unsigned long tmo = msecs_to_jiffies(3000);

	init_completion(&cmp);
	if (tx_info->chan)
		dma_async_issue_pending((struct dma_chan *)tx_info->chan);

	if (tx_info->wait) {
		tmo = wait_for_completion_timeout(&cmp, tmo);
		if (0 == tmo)
			pr_err("Timeout has occured...\n");
	}
}

void xvdma_prep_slave_sg(struct xvdma_buf_info *buf_info)
{
	struct dma_chan *chan;
	struct dma_device *chan_dev;
	struct dma_async_tx_descriptor *chan_desc;
	struct scatterlist chansg[MAX_FRAMES];
	dma_addr_t dma_srcs[MAX_FRAMES];
	u8 **buf = NULL;
	int buf_size;
	u32 flags = 0;
	int i;
	u32 device_id;
	u32 frm_cnt = buf_info->frm_cnt;

	buf_size = buf_info->buf_size;
	chan = (struct dma_chan *) buf_info->chan;
	device_id = buf_info->device_id;

	if (chan) {
		flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

		if (buf_info->fixed_buffer) {
			chan_dev = chan->device;
			sg_init_table(chansg, frm_cnt);
			for (i = 0; i < frm_cnt; i++) {
				if (!buf_info->shared_buffer) {
					dma_srcs[i] =
					buf_info->addr_base + i * buf_size;
					chan_buf[device_id].dma_addr[i] =
					dma_srcs[i];
				}
				sg_dma_address(&chansg[i]) =
				chan_buf[device_id].dma_addr[i];
				sg_dma_len(&chansg[i]) = buf_size;
			}
		} else {
			if (!buf_info->shared_buffer) {
				buf = kcalloc(frm_cnt + 1, sizeof(u8 *),
						 GFP_KERNEL);
				if (!buf)
					pr_err("Buf failed\n");

				for (i = 0; i < frm_cnt; i++) {
					buf[i] = kmalloc(buf_size, GFP_KERNEL);
					if (!buf[i])
						pr_err("Buf[%d] failed\n", i);
				}
				buf[i] = NULL;
			}

			chan_dev = chan->device;
			sg_init_table(chansg, frm_cnt);
			for (i = 0; i < frm_cnt; i++) {
				if (!buf_info->shared_buffer) {
					dma_srcs[i] = dma_map_single(
						chan_dev->dev, buf[i], buf_size,
							buf_info->mem_type);
					chan_buf[device_id].dma_addr[i] =
								dma_srcs[i];
			}
			sg_dma_address(&chansg[i]) =
			chan_buf[device_id].dma_addr[i];
			sg_dma_len(&chansg[i]) = buf_size;
			}
		}
		chan_desc = chan_dev->device_prep_slave_sg(chan, chansg,
				frm_cnt, buf_info->direction, flags, NULL);
		if (buf_info->callback) {
			chan_desc->callback = vdma_sync_callback;
			chan_desc->callback_param = &cmp;
		}
		chan_desc->tx_submit(chan_desc);
	}
}

void xvdma_device_control(struct xvdma_chan_cfg *chan_cfg)
{
	struct dma_chan *chan;
	struct dma_device *chan_dev;

	chan = (struct dma_chan *) chan_cfg->chan;

	if (chan) {
		chan_dev = chan->device;
		chan_dev->device_control(chan, DMA_SLAVE_CONFIG,
					 (unsigned long)&chan_cfg->config);
	}
}

static void xvdma_add_dev_info(struct dma_chan *tx_chan,
				struct dma_chan *rx_chan)
{
	static u32 i;

	xvdma_dev_info[i] = kzalloc(sizeof(struct xvdma_dev), GFP_KERNEL);

	xvdma_dev_info[i]->tx_chan = (u32) tx_chan;
	xvdma_dev_info[i]->rx_chan = (u32) rx_chan;
	xvdma_dev_info[i]->device_id = i;
	num_devices++;
	i++;
}

static void xvdma_scan_channels(void)
{
	dma_cap_mask_t mask;
	u32 match_tx, match_rx;
	struct dma_chan *tx_chan, *rx_chan;
	u32 device_id = 0;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE | DMA_PRIVATE, mask);

	for (;;) {
		match_tx = (DMA_TO_DEVICE & 0xFF) | XILINX_DMA_IP_VDMA |
			(device_id << XVDMA_DEVICE_ID_SHIFT);
		tx_chan = dma_request_channel(mask, xvdma_filter,
				(void *)&match_tx);
		match_rx = (DMA_FROM_DEVICE & 0xFF) | XILINX_DMA_IP_VDMA |
			(device_id << XVDMA_DEVICE_ID_SHIFT);
		rx_chan = dma_request_channel(mask, xvdma_filter,
				(void *)&match_rx);

		if (!tx_chan && !rx_chan)
			break;
		else
			xvdma_add_dev_info(tx_chan, rx_chan);

		device_id++;
	}
}

static void xvdma_release_channels(void)
{
	int i;

	for (i = 0; i < MAX_DEVICES; i++) {
		if (xvdma_dev_info[i]->tx_chan)
			dma_release_channel((struct dma_chan *)
				xvdma_dev_info[i]->tx_chan);
		if (xvdma_dev_info[i]->rx_chan)
			dma_release_channel((struct dma_chan *)
				xvdma_dev_info[i]->rx_chan);
	}
}

static const struct file_operations xvdma_fops = {
	.owner = THIS_MODULE,
	.open = xvdma_open,
	.unlocked_ioctl = xvdma_ioctl,
	.release = xvdma_release,
};

static int xvdma_probe(struct platform_device *pdev)
{
	dev_t devt;
	struct xvdma_drvdata *drvdata = NULL;
	struct device *dev = &pdev->dev;
	int retval;

	devt = MKDEV(XVDMA_MAJOR, XVDMA_MINOR);

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct xvdma_drvdata),
				GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	dev_set_drvdata(dev, (void *)drvdata);

	drvdata->dev = dev;
	drvdata->devt = devt;

	cdev_init(&drvdata->cdev, &xvdma_fops);
	drvdata->cdev.owner = THIS_MODULE;
	retval = cdev_add(&drvdata->cdev, devt, 1);
	if (retval) {
		dev_err(dev, "cdev_add() failed\n");
		return retval;
	}

	xvdma_scan_channels();
	dev_info(dev, "Xilinx VDMA probe successful\n");
	dev_info(dev, "Devices Scanned %d\n", num_devices);
	return 0;
}

static int xvdma_remove(struct platform_device *op)
{
	struct xvdma_drvdata *drvdata;
	struct device *dev = &op->dev;

	drvdata = (struct xvdma_drvdata *)dev_get_drvdata(dev);
	if (!drvdata)
		return 0;

	xvdma_release_channels();
	cdev_del(&drvdata->cdev);
	return 0;
}

static struct platform_driver xvdma_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   },
	.probe = xvdma_probe,
	.remove = xvdma_remove,
	.suspend = XVDMA_SUSPEND,
	.resume = XVDMA_RESUME,
};

static struct platform_device xvdma_device = {
	.name = "xvdma",
	.id = 0,
	.dev = {
		.platform_data = NULL,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = NULL,
	.num_resources = 0,
};

static int __init xvdma_init(void)
{
	platform_device_register(&xvdma_device);

	return platform_driver_register(&xvdma_driver);
}

static void __exit xvdma_exit(void)
{
	platform_driver_unregister(&xvdma_driver);
}

late_initcall(xvdma_init);
module_exit(xvdma_exit);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx AXI VDMA client driver");
MODULE_LICENSE("GPL v2");

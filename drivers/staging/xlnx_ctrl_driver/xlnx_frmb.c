// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA Xilinx Framebuffer read control driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 * Author: Saurabh Sengar <saurabh.singh@xilinx.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/dmaengine.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xlnx_ctrl.h>

/* TODO: clock framework */

#define XFBWR_FB_CTRL		0x00
#define XFBWR_FB_WIDTH		0x10
#define XFBWR_FB_HEIGHT		0x18
#define XFBWR_FB_STRIDE		0x20
#define XFBWR_FB_COLOR		0x28
#define XFBWR_FB_PLANE1		0x30
#define XFBWR_FB_PLANE2		0x3C

#define XFBWR_FB_CTRL_START		BIT(0)
#define XFBWR_FB_CTRL_IDLE		BIT(2)
#define XFBWR_FB_CTRL_RESTART		BIT(7)
#define XFBWR_FB_CTRL_OFF		0

static u64 dma_mask = -1ULL;

struct frmb_dmabuf_reg {
	s32 dmabuf_fd;
	struct dma_buf *dbuf;
	struct dma_buf_attachment *dbuf_attach;
	struct sg_table *dbuf_sg_table;
};

/**
 * struct frmb_struct - Xilinx framebuffer ctrl object
 *
 * @dev: device structure
 * @db: framebuffer ctrl driver dmabuf structure
 * @frmb_miscdev: The misc device registered
 * @regs: Base address of framebuffer IP
 * @is_fbrd: True for framebuffer Read else false
 */
struct frmb_struct {
	struct device *dev;
	struct frmb_dmabuf_reg db;
	struct miscdevice frmb_miscdev;
	void __iomem *regs;
	bool is_fbrd;
};

struct frmb_data {
	u32 fd;
	u32 height;
	u32 width;
	u32 stride;
	u32 color;
	u32 n_planes;
	u32 offset;
};

struct match_struct {
	char name[8];
	bool is_read;
};

static const struct match_struct read_struct = {
	.name = "fbrd",
	.is_read = true,
};

static const struct match_struct write_struct = {
	.name = "fbwr",
	.is_read = false,
};

/* Match table for of_platform binding */
static const struct of_device_id frmb_of_match[] = {
	{ .compatible = "xlnx,ctrl-fbwr-1.0", .data = &write_struct},
	{ .compatible = "xlnx,ctrl-fbrd-1.0", .data = &read_struct},
	{},
};

MODULE_DEVICE_TABLE(of, frmb_of_match);

static inline struct frmb_struct *to_frmb_struct(struct file *file)
{
	struct miscdevice *miscdev = file->private_data;

	return container_of(miscdev, struct frmb_struct, frmb_miscdev);
}

static inline u32 frmb_ior(void __iomem *lp, off_t offset)
{
	return readl(lp + offset);
}

static inline void frmb_iow(void __iomem *lp, off_t offset, u32 value)
{
	writel(value, (lp + offset));
}

phys_addr_t frmb_add_dmabuf(u32 fd, struct frmb_struct *frmb_g)
{
	frmb_g->db.dbuf = dma_buf_get(fd);
	frmb_g->db.dbuf_attach = dma_buf_attach(frmb_g->db.dbuf, frmb_g->dev);
	if (IS_ERR(frmb_g->db.dbuf_attach)) {
		dma_buf_put(frmb_g->db.dbuf);
		dev_err(frmb_g->dev, "Failed DMA-BUF attach\n");
		return -EINVAL;
	}

	frmb_g->db.dbuf_sg_table = dma_buf_map_attachment(frmb_g->db.dbuf_attach
							  , DMA_BIDIRECTIONAL);

	if (!frmb_g->db.dbuf_sg_table) {
		dev_err(frmb_g->dev, "Failed DMA-BUF map_attachment\n");
		dma_buf_detach(frmb_g->db.dbuf, frmb_g->db.dbuf_attach);
		dma_buf_put(frmb_g->db.dbuf);
		return -EINVAL;
	}

	return (u32)sg_dma_address(frmb_g->db.dbuf_sg_table->sgl);
}

static void xlnk_clear_dmabuf(struct frmb_struct *frmb_g)
{
	dma_buf_unmap_attachment(frmb_g->db.dbuf_attach,
				 frmb_g->db.dbuf_sg_table,
				 DMA_BIDIRECTIONAL);
	dma_buf_detach(frmb_g->db.dbuf, frmb_g->db.dbuf_attach);
	dma_buf_put(frmb_g->db.dbuf);
}

static long frmb_ioctl(struct file *file, unsigned int cmd,
		       unsigned long arg)
{
	long retval = 0;
	struct frmb_data data;
	phys_addr_t phys_y = 0, phys_uv = 0;
	struct frmb_struct *frmb_g = to_frmb_struct(file);

	switch (cmd) {
	case XSET_FB_POLL:
		retval = frmb_ior(frmb_g->regs, XFBWR_FB_CTRL);
		if (retval == XFBWR_FB_CTRL_IDLE)
			retval = 0;
		else
			retval = 1;
		break;
	case XSET_FB_ENABLE_SNGL:
		frmb_iow(frmb_g->regs, XFBWR_FB_CTRL, XFBWR_FB_CTRL_START);
		break;
	case XSET_FB_ENABLE:
		frmb_iow(frmb_g->regs, XFBWR_FB_CTRL, XFBWR_FB_CTRL_START);
		frmb_iow(frmb_g->regs, XFBWR_FB_CTRL,
			 XFBWR_FB_CTRL_RESTART | XFBWR_FB_CTRL_START);
		break;
	case XSET_FB_DISABLE:
		frmb_iow(frmb_g->regs, XFBWR_FB_CTRL, XFBWR_FB_CTRL_OFF);
		break;
	case XSET_FB_CONFIGURE:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			break;
		}
		frmb_iow(frmb_g->regs, XFBWR_FB_WIDTH, data.width);
		frmb_iow(frmb_g->regs, XFBWR_FB_HEIGHT, data.height);
		frmb_iow(frmb_g->regs, XFBWR_FB_STRIDE, data.stride);
		frmb_iow(frmb_g->regs, XFBWR_FB_COLOR, data.color);
		break;
	case XSET_FB_CAPTURE:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			break;
		}
		phys_y = frmb_add_dmabuf(data.fd, frmb_g);
		frmb_iow(frmb_g->regs, XFBWR_FB_PLANE1, phys_y);
		if (data.n_planes == 2) {
			phys_uv = phys_y + data.offset;
			frmb_iow(frmb_g->regs, XFBWR_FB_PLANE2, phys_uv);
		}
		break;
	case XSET_FB_RELEASE:
		xlnk_clear_dmabuf(frmb_g);
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}

static const struct file_operations frmb_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = frmb_ioctl,
};

static int frmb_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int ret;
	struct resource *res_frmb;
	const struct of_device_id *match;
	struct frmb_struct *frmb_g;
	struct gpio_desc *reset_gpio;
	const struct match_struct *config;

	pdev->dev.dma_mask = &dma_mask;
	pdev->dev.coherent_dma_mask = dma_mask;

	frmb_g = devm_kzalloc(&pdev->dev, sizeof(*frmb_g), GFP_KERNEL);
	if (!frmb_g)
		return -ENOMEM;

	reset_gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpio)) {
		ret = PTR_ERR(reset_gpio);
		if (ret == -EPROBE_DEFER)
			dev_dbg(&pdev->dev, "No gpio probed, Deferring...\n");
		else
			dev_err(&pdev->dev, "No reset gpio info from dts\n");
		return ret;
	}
	gpiod_set_value_cansleep(reset_gpio, 0);

	platform_set_drvdata(pdev, frmb_g);
	frmb_g->dev = &pdev->dev;
	res_frmb = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	frmb_g->regs = devm_ioremap_resource(&pdev->dev, res_frmb);
	if (IS_ERR(frmb_g->regs))
		return PTR_ERR(frmb_g->regs);

	match = of_match_node(frmb_of_match, node);
	if (!match)
		return -ENODEV;

	config = match->data;
	frmb_g->frmb_miscdev.name = config->name;
	frmb_g->is_fbrd = config->is_read;

	frmb_g->frmb_miscdev.minor = MISC_DYNAMIC_MINOR;
	frmb_g->frmb_miscdev.fops = &frmb_fops;
	frmb_g->frmb_miscdev.parent = NULL;
	ret = misc_register(&frmb_g->frmb_miscdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "FrameBuffer control driver registration failed!\n");
		return ret;
	}

	dev_info(&pdev->dev, "FrameBuffer control driver success!\n");

	return ret;
}

static int frmb_remove(struct platform_device *pdev)
{
	struct frmb_struct *frmb_g = platform_get_drvdata(pdev);

	misc_deregister(&frmb_g->frmb_miscdev);
	return 0;
}

static struct platform_driver frmb_driver = {
	.probe = frmb_probe,
	.remove = frmb_remove,
	.driver = {
		 .name = "xlnx_ctrl-frmb",
		 .of_match_table = frmb_of_match,
	},
};

module_platform_driver(frmb_driver);

MODULE_DESCRIPTION("Xilinx Framebuffer control driver");
MODULE_AUTHOR("Saurabh Sengar");
MODULE_LICENSE("GPL v2");

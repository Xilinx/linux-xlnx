/*
 * xlnk.c
 *
 * Xilinx Accelerator driver support.
 *
 * Copyright (C) 2010 Xilinx Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*  ----------------------------------- Host OS */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <linux/io.h>
#include <linux/dma-buf.h>

#include <linux/string.h>

#include <linux/uaccess.h>

#include <linux/dmaengine.h>
#include <linux/completion.h>
#include <linux/wait.h>

#include <linux/device.h>
#include <linux/init.h>
#include <linux/cdev.h>

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/errno.h>	/* error codes */
#include <linux/dma-mapping.h>  /* dma */
#include <linux/of.h>
#include <linux/list.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/uio_driver.h>
#include <asm/cacheflush.h>

#include "xlnk-ioctl.h"
#include "xlnk-config.h"
#include "xlnk-sysdef.h"
#include "xlnk.h"

#ifdef CONFIG_XILINX_DMA_APF
#include "xilinx-dma-apf.h"
#endif

#ifdef CONFIG_XILINX_MCDMA
#include "xdma-if.h"
#include "xdma.h"

static void xdma_if_device_release(struct device *op)
{
}

#endif

#define DRIVER_NAME  "xlnk"
#define DRIVER_VERSION  "0.2"

static struct platform_device *xlnk_pdev;
static struct device *xlnk_dev;

static struct cdev xlnk_cdev;

static struct class *xlnk_class;

static s32 driver_major;

static char *driver_name = DRIVER_NAME;

static void *xlnk_dev_buf;
static ssize_t xlnk_dev_size;
static int xlnk_dev_vmas;

#define XLNK_BUF_POOL_SIZE	256
static unsigned int xlnk_bufpool_size = XLNK_BUF_POOL_SIZE;
static void *xlnk_bufpool[XLNK_BUF_POOL_SIZE];
static void *xlnk_bufpool_alloc_point[XLNK_BUF_POOL_SIZE];
static xlnk_intptr_type xlnk_userbuf[XLNK_BUF_POOL_SIZE];
static dma_addr_t xlnk_phyaddr[XLNK_BUF_POOL_SIZE];
static size_t xlnk_buflen[XLNK_BUF_POOL_SIZE];
static unsigned int xlnk_bufcacheable[XLNK_BUF_POOL_SIZE];

static struct page **xlnk_page_store;
static int xlnk_page_store_size;

static int xlnk_open(struct inode *ip, struct file *filp);  /* Open */
static int xlnk_release(struct inode *ip, struct file *filp);   /* Release */
static long xlnk_ioctl(struct file *filp, unsigned int code,
				unsigned long args);
static ssize_t xlnk_read(struct file *filp, char __user *buf,
			  size_t count, loff_t *offp);
static ssize_t xlnk_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *offp);
static int xlnk_mmap(struct file *filp, struct vm_area_struct *vma);
static void xlnk_vma_open(struct vm_area_struct *vma);
static void xlnk_vma_close(struct vm_area_struct *vma);

static int xlnk_init_bufpool(void);

LIST_HEAD(xlnk_dmabuf_list);

static int xlnk_shutdown(unsigned long buf);
static int xlnk_recover_resource(unsigned long buf);

static const struct file_operations xlnk_fops = {
	.open = xlnk_open,
	.release = xlnk_release,
	.read = xlnk_read,
	.write = xlnk_write,
	.unlocked_ioctl = xlnk_ioctl,
	.mmap = xlnk_mmap,
};

#define MAX_XLNK_DMAS 16

struct xlnk_device_pack {
	char name[64];
	struct platform_device pdev;
	struct resource res[8];
	struct uio_info *io_ptr;

#ifdef CONFIG_XILINX_DMA_APF
	struct xdma_channel_config dma_chan_cfg[4];  /* for xidane dma only */
	struct xdma_device_config dma_dev_cfg;	   /* for xidane dma only */
#endif

#ifdef CONFIG_XILINX_MCDMA
	struct xdma_device_info mcdma_dev_cfg;	 /* for mcdma only */
#endif

};

static struct xlnk_device_pack *xlnk_devpacks[MAX_XLNK_DMAS];
static void xlnk_devpacks_init(void)
{
	unsigned int i;

	for (i = 0; i < MAX_XLNK_DMAS; i++)
		xlnk_devpacks[i] = NULL;

}

static void xlnk_devpacks_delete(struct xlnk_device_pack *devpack)
{
	unsigned int i;

	for (i = 0; i < MAX_XLNK_DMAS; i++) {
		if (xlnk_devpacks[i] == devpack)
			xlnk_devpacks[i] = NULL;
	}
}

static void xlnk_devpacks_add(struct xlnk_device_pack *devpack)
{
	unsigned int i;

	for (i = 0; i < MAX_XLNK_DMAS; i++) {
		if (xlnk_devpacks[i] == NULL) {
			xlnk_devpacks[i] = devpack;
			break;
		}
	}
}

static struct xlnk_device_pack *xlnk_devpacks_find(xlnk_intptr_type base)
{
	unsigned int i;

	for (i = 0; i < MAX_XLNK_DMAS; i++) {
		if (xlnk_devpacks[i]
			&& xlnk_devpacks[i]->res[0].start == base)
			return xlnk_devpacks[i];
	}
	return NULL;
}

static void xlnk_devpacks_free(xlnk_intptr_type base)
{
	struct xlnk_device_pack *devpack;

	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		if (xlnk_config_dma_type(xlnk_config_dma_standard)) {
			if (devpack->io_ptr)
				uio_unregister_device(devpack->io_ptr);
			if (strcmp(devpack->pdev.name, "xilinx-axidma") != 0)
				platform_device_unregister(&devpack->pdev);
		} else {
			platform_device_unregister(&devpack->pdev);
		}
		xlnk_devpacks_delete(devpack);
		kfree(devpack);
	}
}

static void xlnk_devpacks_free_all(void)
{
	struct xlnk_device_pack *devpack;
	unsigned int i;

	for (i = 0; i < MAX_XLNK_DMAS; i++) {
		devpack = xlnk_devpacks[i];
		if (devpack) {
			if (devpack->io_ptr) {
				uio_unregister_device(devpack->io_ptr);
				kfree(devpack->io_ptr);
			} else {
				platform_device_unregister(&devpack->pdev);
			}
			xlnk_devpacks_delete(devpack);
			kfree(devpack);
		}
	}
}

static void xlnk_load_config_from_dt(struct platform_device *pdev)
{
	const char *dma_name = NULL;
	struct xlnk_config_block block;

	xlnk_init_config();
	xlnk_get_config(&block);

	if (of_property_read_string(xlnk_dev->of_node,
				    "config-dma-type",
				    &dma_name) == 0) {
		if (strcmp(dma_name, "manual") == 0) {
			block.valid_mask[xlnk_config_valid_dma_type] = 1;
			block.dma_type = xlnk_config_dma_manual;
		} else if (strcmp(dma_name, "standard") == 0) {
			block.valid_mask[xlnk_config_valid_dma_type] = 1;
			block.dma_type = xlnk_config_dma_standard;
		} else
			pr_err("%s: Unrecognized DMA type %s\n",
			       __func__, dma_name);
	}
	xlnk_set_config(&block);
}

static int xlnk_probe(struct platform_device *pdev)
{
	int err, i;
	dev_t dev = 0;

	xlnk_dev_buf = NULL;
	xlnk_dev_size = 0;
	xlnk_dev_vmas = 0;

	/* use 2.6 device model */
	xlnk_page_store_size = 1024;
	xlnk_page_store = vmalloc(sizeof(struct page *) * xlnk_page_store_size);
	if (!xlnk_page_store) {
		pr_err("failed to allocate memory for page store\n");
		err = -ENOMEM;
		goto err1;
	}
	err = alloc_chrdev_region(&dev, 0, 1, driver_name);
	if (err) {
		dev_err(&pdev->dev, "%s: Can't get major %d\n",
			 __func__, driver_major);
		goto err1;
	}

	cdev_init(&xlnk_cdev, &xlnk_fops);

	xlnk_cdev.owner = THIS_MODULE;

	err = cdev_add(&xlnk_cdev, dev, 1);

	if (err) {
		dev_err(&pdev->dev, "%s: Failed to add XLNK device\n",
			 __func__);
		goto err3;
	}

	/* udev support */
	xlnk_class = class_create(THIS_MODULE, "xlnk");
	if (IS_ERR(xlnk_class)) {
		dev_err(xlnk_dev, "%s: Error creating xlnk class\n", __func__);
		goto err3;
	}

	driver_major = MAJOR(dev);

	dev_info(&pdev->dev, "Major %d\n", driver_major);

	device_create(xlnk_class, NULL, MKDEV(driver_major, 0),
			  NULL, "xlnk");

	xlnk_init_bufpool();

	dev_info(&pdev->dev, "%s driver loaded\n", DRIVER_NAME);

	xlnk_pdev = pdev;
	xlnk_dev = &pdev->dev;

	xlnk_load_config_from_dt(pdev);

	if (xlnk_pdev)
		dev_info(&pdev->dev, "xlnk_pdev is not null\n");
	else
		dev_info(&pdev->dev, "xlnk_pdev is null\n");

	xlnk_devpacks_init();


	return 0;

err3:
	cdev_del(&xlnk_cdev);
	unregister_chrdev_region(dev, 1);
err1:
	return err;
}

static int xlnk_buf_findnull(void)
{
	int i;

	for (i = 1; i < xlnk_bufpool_size; i++) {
		if (!xlnk_bufpool[i])
			return i;
	}

	return 0;
}

static int xlnk_buf_find_by_phys_addr(xlnk_intptr_type addr)
{
	int i;

	for (i = 1; i < xlnk_bufpool_size; i++) {
		if (xlnk_phyaddr[i] <= addr &&
		    xlnk_phyaddr[i] + xlnk_buflen[i] > addr)
			return i;
	}

	return 0;
}

/**
 * allocate and return an id
 * id must be a positve number
 */
static int xlnk_allocbuf(unsigned int len, unsigned int cacheable)
{
	int id;
	void *kaddr;
	dma_addr_t phys_addr_anchor;
	unsigned int page_dst;

	id = xlnk_buf_findnull();

	if (id <= 0 || id >= XLNK_BUF_POOL_SIZE) {
		pr_err("No id could be found in range\n");
		return -ENOMEM;
	}
	kaddr = kmalloc(len + PAGE_SIZE, GFP_KERNEL | GFP_DMA);
	if (!kaddr)
		return -ENOMEM;
	phys_addr_anchor = virt_to_phys(kaddr);
	xlnk_bufpool_alloc_point[id] = kaddr;
	page_dst = (((phys_addr_anchor + (PAGE_SIZE - 1))
		/ PAGE_SIZE) * PAGE_SIZE) - phys_addr_anchor;
	xlnk_bufpool[id] = (void *)((uint8_t *)kaddr + page_dst);
	xlnk_buflen[id] = len;
	xlnk_bufcacheable[id] = cacheable;
	xlnk_phyaddr[id] = phys_addr_anchor + page_dst;

	return id;
}

static int xlnk_init_bufpool(void)
{
	unsigned int i;

	xlnk_dev_buf = kmalloc(8192, GFP_KERNEL | __GFP_DMA);
	*((char *)xlnk_dev_buf) = '\0';

	if (!xlnk_dev_buf) {
		dev_err(xlnk_dev, "%s: malloc failed\n", __func__);
		return -ENOMEM;
	}

	xlnk_bufpool[0] = xlnk_dev_buf;
	for (i = 1; i < xlnk_bufpool_size; i++)
		xlnk_bufpool[i] = NULL;

	return 0;
}

#define XLNK_SUSPEND NULL
#define XLNK_RESUME NULL

static int xlnk_remove(struct platform_device *pdev)
{
	dev_t devno;

	kfree(xlnk_dev_buf);
	xlnk_dev_buf = NULL;

	devno = MKDEV(driver_major, 0);
	cdev_del(&xlnk_cdev);
	unregister_chrdev_region(devno, 1);
	if (xlnk_class) {
		/* remove the device from sysfs */
		device_destroy(xlnk_class, MKDEV(driver_major, 0));
		class_destroy(xlnk_class);
	}

	xlnk_devpacks_free_all();

	return 0;
}

static const struct of_device_id xlnk_match[] = {
	{ .compatible = "xlnx,xlnk-1.0", },
	{}
};
MODULE_DEVICE_TABLE(of, xlnk_match);

static struct platform_driver xlnk_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = xlnk_match,
	},
	.probe = xlnk_probe,
	.remove = xlnk_remove,
	.suspend = XLNK_SUSPEND,
	.resume = XLNK_RESUME,
};

static u64 dma_mask = 0xFFFFFFFFUL;

/*
 * This function is called when an application opens handle to the
 * bridge driver.
 */
static int xlnk_open(struct inode *ip, struct file *filp)
{
	int status = 0;

	if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
		xlnk_dev_size = 0;

	return status;
}

static ssize_t xlnk_read(struct file *filp, char __user *buf,
			  size_t count, loff_t *offp)
{
	ssize_t retval = 0;

	/* todo: need semi for critical section */

	if (*offp >= xlnk_dev_size)
		goto out;

	if (*offp + count > xlnk_dev_size)
		count = xlnk_dev_size - *offp;

	if (copy_to_user(buf, xlnk_dev_buf + *offp, count)) {
		retval = -EFAULT;
		goto out;
	}
	*offp += count;
	retval = count;

 out:
	return retval;
}

static ssize_t xlnk_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *offp)
{
	ssize_t retval = 0;

	/* todo: need to setup semi for critical section */

	if (copy_from_user(xlnk_dev_buf + *offp, buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	*offp += count;
	retval = count;

	if (xlnk_dev_size < *offp)
		xlnk_dev_size = *offp;

 out:
	return retval;
}

/*
 * This function is called when an application closes handle to the bridge
 * driver.
 */
static int xlnk_release(struct inode *ip, struct file *filp)
{
	return 0;
}


static int xlnk_devregister(char *name, unsigned int id,
				xlnk_intptr_type base, unsigned int size,
				unsigned int *irqs,
				xlnk_intptr_type *handle)
{
	unsigned int nres;
	unsigned int nirq;
	unsigned int *irqptr;
	struct xlnk_device_pack *devpack;
	unsigned int i;
	int status;

	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (xlnk_intptr_type)devpack;
		return 0;
	}
	nirq = 0;
	irqptr = irqs;

	while (*irqptr) {
		nirq++;
		irqptr++;
	}

	if (nirq > 7)
		return -ENOMEM;

	nres = nirq + 1;

	devpack = kzalloc(sizeof(struct xlnk_device_pack),
			  GFP_KERNEL);
	devpack->io_ptr = NULL;
	strcpy(devpack->name, name);
	devpack->pdev.name = devpack->name;

	devpack->pdev.id = id;

	devpack->pdev.dev.dma_mask = &dma_mask;
	devpack->pdev.dev.coherent_dma_mask = dma_mask;

	devpack->res[0].start = base;
	devpack->res[0].end = base + size - 1;
	devpack->res[0].flags = IORESOURCE_MEM;

	for (i = 0; i < nirq; i++) {
		devpack->res[i+1].start = irqs[i];
		devpack->res[i+1].end = irqs[i];
		devpack->res[i+1].flags = IORESOURCE_IRQ;
	}

	devpack->pdev.resource = devpack->res;
	devpack->pdev.num_resources = nres;

	status = platform_device_register(&devpack->pdev);
	if (status) {
		kfree(devpack);
		*handle = 0;
	} else {
		xlnk_devpacks_add(devpack);
		*handle = (xlnk_intptr_type)devpack;
	}
	return status;
}

static int xlnk_dmaregister(char *name, unsigned int id,
				xlnk_intptr_type base, unsigned int size,
				unsigned int chan_num,
				unsigned int chan0_dir,
				unsigned int chan0_irq,
				unsigned int chan0_poll_mode,
				unsigned int chan0_include_dre,
				unsigned int chan0_data_width,
				unsigned int chan1_dir,
				unsigned int chan1_irq,
				unsigned int chan1_poll_mode,
				unsigned int chan1_include_dre,
				unsigned int chan1_data_width,
				xlnk_intptr_type *handle)
{
	int status = 0;

#ifdef CONFIG_XILINX_DMA_APF

	struct xlnk_device_pack *devpack;

	if (chan_num < 1 || chan_num > 2) {
		pr_err("%s: Expected either 1 or 2 channels, got %d\n",
		       __func__, chan_num);
		return -EINVAL;
	}

	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (xlnk_intptr_type)devpack;
		return 0;
	}

	devpack = kzalloc(sizeof(struct xlnk_device_pack),
			  GFP_KERNEL);
	if (!devpack)
		return -ENOMEM;
	strcpy(devpack->name, name);
	devpack->pdev.name = "xilinx-axidma";
	if (xlnk_config_dma_type(xlnk_config_dma_standard) &&
	    chan0_data_width == 0 && chan1_data_width == 0) {
		devpack->io_ptr = kzalloc(sizeof(*devpack->io_ptr),
					  GFP_KERNEL);
		if (!devpack->io_ptr)
			return -EFAULT;
		devpack->io_ptr->name = devpack->name;
		devpack->io_ptr->version = "0.0.1";
		devpack->io_ptr->irq = -1;
		if (uio_register_device(xlnk_dev, devpack->io_ptr)) {
			pr_err("UIO dummy failed to install\n");
			return -EFAULT;
		}
	} else {
		devpack->io_ptr = NULL;
	}

	devpack->pdev.id = id;

	devpack->dma_chan_cfg[0].include_dre = chan0_include_dre;
	devpack->dma_chan_cfg[0].datawidth   = chan0_data_width;
	devpack->dma_chan_cfg[0].irq = chan0_irq;
	devpack->dma_chan_cfg[0].poll_mode   = chan0_poll_mode;
	devpack->dma_chan_cfg[0].type =
		(chan0_dir == XLNK_DMA_FROM_DEVICE) ?
					"axi-dma-s2mm-channel" :
					"axi-dma-mm2s-channel";

	if (chan_num > 1) {
		devpack->dma_chan_cfg[1].include_dre = chan1_include_dre;
		devpack->dma_chan_cfg[1].datawidth   = chan1_data_width;
		devpack->dma_chan_cfg[1].irq = chan1_irq;
		devpack->dma_chan_cfg[1].poll_mode   = chan1_poll_mode;
		devpack->dma_chan_cfg[1].type =
			(chan1_dir == XLNK_DMA_FROM_DEVICE) ?
						"axi-dma-s2mm-channel" :
						"axi-dma-mm2s-channel";
	}

	devpack->dma_dev_cfg.name = devpack->name;
	devpack->dma_dev_cfg.type = "axi-dma";
	devpack->dma_dev_cfg.include_sg = 1;
	devpack->dma_dev_cfg.sg_include_stscntrl_strm = 1;
	devpack->dma_dev_cfg.channel_count = chan_num;
	devpack->dma_dev_cfg.channel_config = &devpack->dma_chan_cfg[0];

	devpack->pdev.dev.platform_data = &devpack->dma_dev_cfg;

	devpack->pdev.dev.dma_mask = &dma_mask;
	devpack->pdev.dev.coherent_dma_mask = dma_mask;

	devpack->res[0].start = base;
	devpack->res[0].end = base + size - 1;
	devpack->res[0].flags = IORESOURCE_MEM;

	devpack->pdev.resource = devpack->res;
	devpack->pdev.num_resources = 1;
	if (xlnk_config_dma_type(xlnk_config_dma_manual))
		status = platform_device_register(&devpack->pdev);
	if (status) {
		kfree(devpack);
		*handle = 0;
	} else {
		xlnk_devpacks_add(devpack);
		*handle = (xlnk_intptr_type)devpack;
	}

#endif
	return status;
}

static int xlnk_mcdmaregister(char *name, unsigned int id,
			      xlnk_intptr_type base, unsigned int size,
			      unsigned int mm2s_chan_num,
			      unsigned int mm2s_chan_irq,
			      unsigned int s2mm_chan_num,
			      unsigned int s2mm_chan_irq,
			      xlnk_intptr_type *handle)
{
	int status = -1;

#ifdef CONFIG_XILINX_MCDMA
	struct xlnk_device_pack *devpack;
	if (xlnk_config_dma_type(xlnk_config_dma_standard)) {
		pr_err("Standard driver not yet supporting multichannel\n");
		return -EFAULT;
	}

	if (strcmp(name, "xdma"))
		return -EINVAL;


	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (xlnk_intptr_type)devpack;
		return 0;
	}

	devpack = kzalloc(sizeof(struct xlnk_device_pack),
			  GFP_KERNEL);
	if (!devpack)
		return -ENOMEM;

	strcpy(devpack->name, name);
	devpack->pdev.name = devpack->name;
	devpack->pdev.id = id;

	devpack->mcdma_dev_cfg.tx_chans	= mm2s_chan_num;
	devpack->mcdma_dev_cfg.rx_chans	= s2mm_chan_num;
	devpack->mcdma_dev_cfg.legacy_mode = XDMA_MCHAN_MODE;
	devpack->mcdma_dev_cfg.device_id   = id;

	devpack->pdev.dev.platform_data	 = &devpack->mcdma_dev_cfg;
	devpack->pdev.dev.dma_mask = &dma_mask;
	devpack->pdev.dev.coherent_dma_mask = dma_mask;
	devpack->pdev.dev.release = xdma_if_device_release,

	devpack->res[0].start = base;
	devpack->res[0].end   = base + size - 1;
	devpack->res[0].flags = IORESOURCE_MEM;

	devpack->res[1].start = mm2s_chan_irq;
	devpack->res[1].end   = s2mm_chan_irq;
	devpack->res[1].flags = IORESOURCE_IRQ;

	devpack->pdev.resource	  = devpack->res;
	devpack->pdev.num_resources = 2;

	status = platform_device_register(&devpack->pdev);
	if (status) {
		kfree(devpack);
		*handle = 0;
	} else {
		xlnk_devpacks_add(devpack);
		*handle = (xlnk_intptr_type)devpack;
	}

#endif

	return status;
}

static int xlnk_allocbuf_ioctl(struct file *filp, unsigned int code,
			unsigned long args)
{

	union xlnk_args temp_args;
	int status;
	xlnk_intptr_type id;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	id = xlnk_allocbuf(temp_args.allocbuf.len,
			   temp_args.allocbuf.cacheable);

	if (id <= 0)
		return -ENOMEM;

	temp_args.allocbuf.id = id;
	temp_args.allocbuf.phyaddr = (xlnk_intptr_type)(xlnk_phyaddr[id]);
	status = copy_to_user(args, &temp_args, sizeof(union xlnk_args));

	return status;
}

static int xlnk_freebuf(int id)
{

	if (id <= 0 || id >= xlnk_bufpool_size)
		return -ENOMEM;

	if (!xlnk_bufpool[id])
		return -ENOMEM;

	kfree(xlnk_bufpool_alloc_point[id]);
	xlnk_bufpool[id] = NULL;
	xlnk_phyaddr[id] = (dma_addr_t)NULL;
	xlnk_buflen[id] = 0;

	return 0;
}

static void xlnk_free_all_buf(void)
{
	int i;

	for (i = 1; i < xlnk_bufpool_size; i++)
		xlnk_freebuf(i);
}

static int xlnk_freebuf_ioctl(struct file *filp, unsigned int code,
			unsigned long args)
{

	union xlnk_args temp_args;
	int status;
	int id;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	id = temp_args.freebuf.id;
	return xlnk_freebuf(id);
}

static int xlnk_adddmabuf_ioctl(struct file *filp, unsigned int code,
			unsigned long args)
{
	union xlnk_args temp_args;
	struct xlnk_dmabuf_reg *db;
	int status;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	dev_dbg(xlnk_dev, "Registering dmabuf fd %d for virtual address %p\n",
		temp_args.dmabuf.dmabuf_fd, temp_args.dmabuf.user_addr);

	db = kzalloc(sizeof(struct xlnk_dmabuf_reg), GFP_KERNEL);
	if (!db)
		return -ENOMEM;

	db->dmabuf_fd = temp_args.dmabuf.dmabuf_fd;
	db->user_vaddr = temp_args.dmabuf.user_addr;

	db->dbuf = dma_buf_get(db->dmabuf_fd);
	if (IS_ERR_OR_NULL(db->dbuf)) {
		dev_err(xlnk_dev, "%s Invalid dmabuf fd %d\n",
			 __func__, db->dmabuf_fd);
		return -EINVAL;
	}
	db->is_mapped = 0;

	INIT_LIST_HEAD(&db->list);
	list_add_tail(&db->list, &xlnk_dmabuf_list);

	return 0;
}

static int xlnk_cleardmabuf_ioctl(struct file *filp, unsigned int code,
				unsigned long args)
{
	union xlnk_args temp_args;
	struct xlnk_dmabuf_reg *dp, *dp_temp;
	int status;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	list_for_each_entry_safe(dp, dp_temp, &xlnk_dmabuf_list, list) {
		if (dp->user_vaddr == temp_args.dmabuf.user_addr) {
			if (dp->is_mapped) {
				dma_buf_unmap_attachment(dp->dbuf_attach,
					dp->dbuf_sg_table, dp->dma_direction);
				dma_buf_detach(dp->dbuf, dp->dbuf_attach);
				kfree(dp->sg_list);
			}
			dma_buf_put(dp->dbuf);
			list_del(&dp->list);
			kfree(dp);
			return 0;
		}
	}
	return 1;
}

static int xlnk_dmarequest_ioctl(struct file *filp, unsigned int code,
				 unsigned long args)
{

#ifdef CONFIG_XILINX_DMA_APF

	union xlnk_args temp_args;
	int status;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	if (!temp_args.dmarequest.name[0])
		return 0;

	if (xlnk_config_dma_type(xlnk_config_dma_standard)) {
		struct dma_chan *chan;

		if (!xlnk_dev->of_node) {
			pr_err("xlnk %s: No device tree info.", __func__);
			return -EFAULT;
		}
		chan = dma_request_slave_channel(xlnk_dev,
						 temp_args.dmarequest.name);
		if (!chan) {
			pr_err("Unable to get channel named %s\n",
			       temp_args.dmarequest.name);
			return -EFAULT;
		}
		temp_args.dmarequest.dmachan = (xlnk_intptr_type)chan;
	} else {
		struct xdma_chan *chan =
			xdma_request_channel(temp_args.dmarequest.name);

		if (!chan)
			return -ENOMEM;
		temp_args.dmarequest.dmachan = (xlnk_intptr_type)chan;
		temp_args.dmarequest.bd_space_phys_addr = chan->bd_phys_addr;
		temp_args.dmarequest.bd_space_size = chan->bd_chain_size;
	}

	if (copy_to_user((void __user *)args, &temp_args,
			sizeof(union xlnk_args)))
		return -EFAULT;

	return 0;

#else

	return -1;

#endif

}

static void xlnk_complete_dma_callback(void *args)
{
	complete(args);
}

static int xlnk_dmasubmit_ioctl(struct file *filp, unsigned int code,
				unsigned long args)
{
#ifdef CONFIG_XILINX_DMA_APF
	union xlnk_args temp_args;
	struct xdma_head *dmahead;
	struct xlnk_dmabuf_reg *dp, *cp;
	int status = -1;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	if (!temp_args.dmasubmit.dmachan)
		return -ENODEV;

	cp = NULL;

	list_for_each_entry(dp, &xlnk_dmabuf_list, list) {
		if (dp->user_vaddr == temp_args.dmasubmit.buf) {
			cp = dp;
			break;
		}
	}

	if (xlnk_config_dma_type(xlnk_config_dma_standard)) {
		struct xlnk_dma_transfer_handle *t =
			vmalloc(sizeof(struct xlnk_dma_transfer_handle));

		if (!t) {
			pr_err("Could not allocate dma transfer handle\n");
			return -ENOMEM;
		}
		t->transfer_direction = temp_args.dmasubmit.dmadir;
		t->user_addr = (xlnk_intptr_type)temp_args.dmasubmit.buf;
		t->transfer_length = temp_args.dmasubmit.len;
		t->flags = temp_args.dmasubmit.flag;
		t->channel = (struct dma_chan *)(temp_args.dmasubmit.dmachan);
		if (t->flags & CF_FLAG_PHYSICALLY_CONTIGUOUS) {
			int id = xlnk_buf_find_by_phys_addr(t->user_addr);

			if (id <= 0) {
				pr_err("invalid ID, failing\n");
				return -EFAULT;
			}
			t->kern_addr = xlnk_bufpool[id];
			t->sg_effective_length = 1;
			t->sg_list_size = 1;
			t->sg_list = kmalloc(sizeof(*t->sg_list)
					     * (t->sg_list_size),
					     GFP_KERNEL | GFP_DMA);
			sg_init_table(t->sg_list, t->sg_list_size);
			t->dma_addr = dma_map_single(t->channel->device->dev,
						     t->kern_addr,
						     t->transfer_length,
						     t->transfer_direction);
			if (dma_mapping_error(t->channel->device->dev,
					      t->dma_addr)) {
				pr_err("DMA mapping error\n");
				vfree(t);
				return -EFAULT;
			}
			sg_dma_address(t->sg_list) = t->dma_addr;
			sg_dma_len(t->sg_list) = t->transfer_length;
		} else {
			unsigned long it;
			int locked_page_count;
			int p_it;
			unsigned long first_page = t->user_addr / PAGE_SIZE;
			unsigned long last_page =
				(t->user_addr + (t->transfer_length - 1))
				/ PAGE_SIZE;

			t->kern_addr = NULL;
			t->dma_addr = 0;
			t->sg_list_size = last_page - first_page;
			t->sg_list = kmalloc(sizeof(*t->sg_list)
					     * (t->sg_list_size),
					     GFP_KERNEL | GFP_DMA);
			if (!t->sg_list) {
				vfree(t);
				return -ENOMEM;
			}
			if (xlnk_page_store_size <= t->sg_list_size) {
				struct page **tmp =
					vmalloc(sizeof(struct page *)
						* 2 * t->sg_list_size);

				if (!tmp) {
					kfree(t->sg_list);
					vfree(t);
					return -ENOMEM;
				}
				xlnk_page_store = tmp;
				xlnk_page_store_size = 2 * t->sg_list_size;
			}
			down_read(&current->mm->mmap_sem);
			locked_page_count =
				get_user_pages(current,
					       current->mm,
					       first_page * PAGE_SIZE,
					       t->sg_list_size, 1, 1,
					       xlnk_page_store, NULL);
			up_read(&current->mm->mmap_sem);
			if (locked_page_count != t->sg_list_size) {
				int i;

				pr_err("could not get user pages");
				for (i = 0; i < locked_page_count; i++)
					page_cache_release(xlnk_page_store[i]);
				kfree(t->sg_list);
				vfree(t);
				return -EFAULT;
			}
			it = t->user_addr;
			p_it = 0;
			sg_init_table(t->sg_list, t->sg_list_size);
			while (it < t->user_addr + t->transfer_length) {
				unsigned long page_addr =
					(it / PAGE_SIZE) * PAGE_SIZE;
				unsigned long offset = it - page_addr;
				unsigned long page_barrier =
					page_addr + PAGE_SIZE;
				unsigned long segment_end =
					(page_barrier < t->user_addr +
					t->transfer_length) ?
					page_barrier :
					(t->user_addr + t->transfer_length);
				unsigned long segment_size = segment_end - it;

				it = segment_end;
				sg_set_page(t->sg_list + p_it,
					    xlnk_page_store[p_it],
					    (unsigned int)segment_size,
					    (unsigned int)offset);
				p_it++;
			}
			t->sg_effective_length =
				dma_map_sg(t->channel->device->dev,
					   t->sg_list,
					   t->sg_list_size,
					   t->transfer_direction);
			if (t->sg_effective_length == 0) {
				int i;

				pr_err("could not map user pages");
				for (i = 0; i < locked_page_count; i++)
					page_cache_release(xlnk_page_store[i]);
				kfree(t->sg_list);
				vfree(t);
				return -EFAULT;
			}
		}
		t->async_desc =
			t->channel->device->device_prep_slave_sg(
				t->channel, t->sg_list,
				t->sg_effective_length,
				t->transfer_direction,
				DMA_CTRL_ACK | DMA_PREP_INTERRUPT,
				temp_args.dmasubmit.appwords_i);
		if (!t->async_desc) {
			pr_err("Async desc is null, aborting\n");
			return -EFAULT;
		}
		init_completion(&t->completion_handle);
		t->async_desc->callback = &xlnk_complete_dma_callback;
		t->async_desc->callback_param = &t->completion_handle;
		t->dma_cookie = t->async_desc->tx_submit(t->async_desc);
		dma_async_issue_pending(t->channel);
		if (dma_submit_error(t->dma_cookie)) {
			pr_err("Huge problem submitting DMA action\n");
			return -EFAULT;
		}
		temp_args.dmasubmit.dmahandle = (xlnk_intptr_type)t;
	} else {
		status = xdma_submit((struct xdma_chan *)
				     (temp_args.dmasubmit.dmachan),
				     temp_args.dmasubmit.buf,
				     temp_args.dmasubmit.len,
				     temp_args.dmasubmit.nappwords_i,
				     temp_args.dmasubmit.appwords_i,
				     temp_args.dmasubmit.nappwords_o,
				     temp_args.dmasubmit.flag,
				     &dmahead,
				     cp);

		temp_args.dmasubmit.dmahandle = (xlnk_intptr_type)dmahead;
		temp_args.dmasubmit.last_bd_index =
			(xlnk_intptr_type)dmahead->last_bd_index;
	}
	if (!status) {
		if (copy_to_user((void __user *)args, &temp_args,
				sizeof(union xlnk_args)))
			return -EFAULT;
	}
	return status;
#endif
	return -ENOMEM;
}


static int xlnk_dmawait_ioctl(struct file *filp, unsigned int code,
				  unsigned long args)
{
	int status = -1;

#ifdef CONFIG_XILINX_DMA_APF
	union xlnk_args temp_args;
	struct xdma_head *dmahead;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;
	if (xlnk_config_dma_type(xlnk_config_dma_standard)) {
		int dma_result;
		struct xlnk_dma_transfer_handle *t =
			(struct xlnk_dma_transfer_handle *)
			temp_args.dmawait.dmahandle;

		wait_for_completion(&t->completion_handle);
		dma_result = dma_async_is_tx_complete(t->channel,
						      t->dma_cookie,
						      NULL, NULL);
		if (dma_result != DMA_COMPLETE) {
			pr_err("Dma transfer failed for unknown reason\n");
			return -1;
		}
		if (t->dma_addr) {
			dma_unmap_single(t->channel->device->dev,
					 t->dma_addr,
					 t->transfer_length,
					 t->transfer_direction);
		} else {
			int i;

			dma_unmap_sg(t->channel->device->dev,
				     t->sg_list,
				     t->sg_list_size,
				     t->transfer_direction);
			for (i = 0; i < t->sg_list_size; i++)
				page_cache_release(sg_page(t->sg_list + i));
		}
		kfree(t->sg_list);
		vfree(t);
	} else {
		struct xdma_head *dmahead =
			(struct xdma_head *)temp_args.dmawait.dmahandle;

		status = xdma_wait(dmahead,
				   dmahead->userflag,
				   &temp_args.dmawait.flags);
		if (temp_args.dmawait.flags & XDMA_FLAGS_WAIT_COMPLETE) {
			if (temp_args.dmawait.nappwords) {
				memcpy(temp_args.dmawait.appwords,
				       dmahead->appwords_o,
				       dmahead->nappwords_o * sizeof(u32));
			}
			kfree(dmahead);
		}
		if (copy_to_user((void __user *)args,
				 &temp_args,
				 sizeof(union xlnk_args)))
			return -EFAULT;
	}
#endif

	return status;
}

static int xlnk_dmarelease_ioctl(struct file *filp, unsigned int code,
				 unsigned long args)
{
	int status = -1;

#ifdef CONFIG_XILINX_DMA_APF

	union xlnk_args temp_args;
	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	if (xlnk_config_dma_type(xlnk_config_dma_standard))
		dma_release_channel((struct dma_chan *)
				   (temp_args.dmarelease.dmachan));
	else
		xdma_release_channel((struct xdma_chan *)
				    (temp_args.dmarelease.dmachan));
#endif

	return status;
}


static int xlnk_devregister_ioctl(struct file *filp, unsigned int code,
				  unsigned long args)
{
	union xlnk_args temp_args;
	int status;
	xlnk_intptr_type handle;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	status = xlnk_devregister(temp_args.devregister.name,
				  temp_args.devregister.id,
				  temp_args.devregister.base,
				  temp_args.devregister.size,
				  temp_args.devregister.irqs,
				  &handle);

	return status;
}

static int xlnk_dmaregister_ioctl(struct file *filp, unsigned int code,
				  unsigned long args)
{
	union xlnk_args temp_args;
	int status;
	xlnk_intptr_type handle;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	status = xlnk_dmaregister(temp_args.dmaregister.name,
				  temp_args.dmaregister.id,
				  temp_args.dmaregister.base,
				  temp_args.dmaregister.size,
				  temp_args.dmaregister.chan_num,
				  temp_args.dmaregister.chan0_dir,
				  temp_args.dmaregister.chan0_irq,
				  temp_args.dmaregister.chan0_poll_mode,
				  temp_args.dmaregister.chan0_include_dre,
				  temp_args.dmaregister.chan0_data_width,
				  temp_args.dmaregister.chan1_dir,
				  temp_args.dmaregister.chan1_irq,
				  temp_args.dmaregister.chan1_poll_mode,
				  temp_args.dmaregister.chan1_include_dre,
				  temp_args.dmaregister.chan1_data_width,
				  &handle);

	return status;
}

static int xlnk_mcdmaregister_ioctl(struct file *filp, unsigned int code,
				  unsigned long args)
{
	union xlnk_args temp_args;
	int status;
	xlnk_intptr_type handle;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	status = xlnk_mcdmaregister(temp_args.mcdmaregister.name,
				  temp_args.mcdmaregister.id,
				  temp_args.mcdmaregister.base,
				  temp_args.mcdmaregister.size,
				  temp_args.mcdmaregister.mm2s_chan_num,
				  temp_args.mcdmaregister.mm2s_chan_irq,
				  temp_args.mcdmaregister.s2mm_chan_num,
				  temp_args.mcdmaregister.s2mm_chan_irq,
				  &handle);

	return status;
}

static int xlnk_devunregister_ioctl(struct file *filp, unsigned int code,
					unsigned long args)
{
	union xlnk_args temp_args;
	int status;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	xlnk_devpacks_free(temp_args.devunregister.base);

	return 0;
}

static int xlnk_cachecontrol_ioctl(struct file *filp, unsigned int code,
				   unsigned long args)
{
	union xlnk_args temp_args;
	int status, size;
	void *paddr, *kaddr;
	int buf_id;

	if (xlnk_config_dma_type(xlnk_config_dma_standard)) {
		pr_err("Manual cache management is forbidden in standard dma types");
		return -1;
	}

	status = copy_from_user(&temp_args, (void __user *)args,
						sizeof(union xlnk_args));

	if (status) {
		dev_err(xlnk_dev, "Error in copy_from_user. status = %d\n",
			status);
		return -ENOMEM;
	}

	if (!(temp_args.cachecontrol.action == 0 ||
		  temp_args.cachecontrol.action == 1)) {
		dev_err(xlnk_dev, "Illegal action specified to cachecontrol_ioctl: %d\n",
		       temp_args.cachecontrol.action);
		return -EINVAL;
	}

	size = temp_args.cachecontrol.size;
	paddr = temp_args.cachecontrol.phys_addr;
	buf_id = xlnk_buf_find_by_phys_addr(paddr);
	if (buf_id == 0) {
		pr_err("Illegal cachecontrol on non-sds_alloc memory");
		return -EINVAL;
	}
	kaddr = xlnk_bufpool[buf_id];
#if XLNK_SYS_BIT_WIDTH == 32
	__cpuc_flush_dcache_area(kaddr, size);
	outer_flush_range(paddr, paddr + size);
	if (temp_args.cachecontrol.action == 1)
		outer_inv_range(paddr, paddr + size);
#else
	if (temp_args.cachecontrol.action == 1)
		__dma_map_area(kaddr, size, DMA_FROM_DEVICE);
	else
		__dma_map_area(kaddr, size, DMA_TO_DEVICE);
#endif
	return 0;
}

static int xlnk_config_ioctl(struct file *filp, unsigned long args)
{
	struct xlnk_config_block block;
	int status, setting = 0, i;

	xlnk_config_clear_block(&block);
	status = copy_from_user(&block, (void __user *)args,
				sizeof(struct xlnk_config_block));
	if (status) {
		pr_err("Error in copy_from_user. status= %d\n", status);
		return -ENOMEM;
	}
	for (i = 0; i < xlnk_config_valid_size; i++)
		if (block.valid_mask[i])
			setting = 1;
	if (setting) {
		status = xlnk_set_config(&block);
	} else {
		xlnk_get_config(&block);
		status = copy_to_user(args, &block,
				      sizeof(struct xlnk_config_block));
	}
	return status;
}

/* This function provides IO interface to the bridge driver. */
static long xlnk_ioctl(struct file *filp, unsigned int code,
			 unsigned long args)
{
	int status = 0;


	if (_IOC_TYPE(code) != XLNK_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(code) > XLNK_IOC_MAXNR)
		return -ENOTTY;

	/* some sanity check */
	switch (code) {
	case XLNK_IOCALLOCBUF:
		status = xlnk_allocbuf_ioctl(filp, code, args);
		break;
	case XLNK_IOCFREEBUF:
		status = xlnk_freebuf_ioctl(filp, code, args);
		break;
	case XLNK_IOCADDDMABUF:
		status = xlnk_adddmabuf_ioctl(filp, code, args);
		break;
	case XLNK_IOCCLEARDMABUF:
		status = xlnk_cleardmabuf_ioctl(filp, code, args);
		break;
	case XLNK_IOCDMAREQUEST:
		status = xlnk_dmarequest_ioctl(filp, code, args);
		break;
	case XLNK_IOCDMASUBMIT:
		status = xlnk_dmasubmit_ioctl(filp, code, args);
		break;
	case XLNK_IOCDMAWAIT:
		status = xlnk_dmawait_ioctl(filp, code, args);
		break;
	case XLNK_IOCDMARELEASE:
		status = xlnk_dmarelease_ioctl(filp, code, args);
		break;
	case XLNK_IOCDEVREGISTER:
		status = xlnk_devregister_ioctl(filp, code, args);
		break;
	case XLNK_IOCDMAREGISTER:
		status = xlnk_dmaregister_ioctl(filp, code, args);
		break;
	case XLNK_IOCMCDMAREGISTER:
		status = xlnk_mcdmaregister_ioctl(filp, code, args);
		break;
	case XLNK_IOCDEVUNREGISTER:
		status = xlnk_devunregister_ioctl(filp, code, args);
		break;
	case XLNK_IOCCACHECTRL:
		status = xlnk_cachecontrol_ioctl(filp, code, args);
		break;
	case XLNK_IOCSHUTDOWN:
		status = xlnk_shutdown(args);
		break;
	case XLNK_IOCRECRES: /* recover resource */
		status = xlnk_recover_resource(args);
		break;
	case XLNK_IOCCONFIG:
		status = xlnk_config_ioctl(filp, args);
		break;
	default:
		status = -EINVAL;
	}

	return status;
}

static struct vm_operations_struct xlnk_vm_ops = {
	.open = xlnk_vma_open,
	.close = xlnk_vma_close,
};

/* This function maps kernel space memory to user space memory. */
static int xlnk_mmap(struct file *filp, struct vm_area_struct *vma)
{

	int bufid;
	int status;

	bufid = vma->vm_pgoff >> (24 - PAGE_SHIFT);

	if (bufid == 0)
		status = remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(xlnk_dev_buf) >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot);
	else {
		if (xlnk_config_dma_type(xlnk_config_dma_standard)) {
			unsigned long pfn;

			if (vma->vm_start != PAGE_ALIGN(vma->vm_start)) {
				pr_err("Cannot map on non-aligned addresses\n");
				return -1;
			}
			if (xlnk_bufcacheable[bufid] == 0)
				vma->vm_page_prot =
				pgprot_noncached(vma->vm_page_prot);
			pfn = virt_to_pfn(xlnk_bufpool[bufid]);
			status = remap_pfn_range(vma,
						 vma->vm_start,
						 pfn,
						 vma->vm_end - vma->vm_start,
						 vma->vm_page_prot);
			xlnk_userbuf[bufid] = vma->vm_start;
		} else {
			if (xlnk_bufcacheable[bufid] == 0)
				vma->vm_page_prot =
					pgprot_noncached(vma->vm_page_prot);
			status = remap_pfn_range(vma, vma->vm_start,
						 xlnk_phyaddr[bufid]
						 >> PAGE_SHIFT,
						 vma->vm_end - vma->vm_start,
						 vma->vm_page_prot);
		}

	}
	if (status) {
		pr_err("xlnk_mmap failed with code %d\n", EAGAIN);
		return -EAGAIN;
	}

	xlnk_vma_open(vma);
	vma->vm_ops = &xlnk_vm_ops;
	vma->vm_private_data = xlnk_bufpool[bufid];

	return 0;
}

static void xlnk_vma_open(struct vm_area_struct *vma)
{
	xlnk_dev_vmas++;
}

static void xlnk_vma_close(struct vm_area_struct *vma)
{
	xlnk_dev_vmas--;
}





static int xlnk_shutdown(unsigned long buf)
{
	return 0;
}

static int xlnk_recover_resource(unsigned long buf)
{
	xlnk_free_all_buf();
#ifdef CONFIG_XILINX_DMA_APF
	xdma_release_all_channels();
#endif
	return 0;
}

module_platform_driver(xlnk_driver);

MODULE_DESCRIPTION("Xilinx APF driver");
MODULE_LICENSE("GPL");

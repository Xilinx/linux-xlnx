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
#include <linux/semaphore.h>

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

#define XLNK_BUF_POOL_SIZE	4096
static unsigned int xlnk_bufpool_size = XLNK_BUF_POOL_SIZE;
static void *xlnk_bufpool[XLNK_BUF_POOL_SIZE];
static void *xlnk_bufpool_alloc_point[XLNK_BUF_POOL_SIZE];
static xlnk_intptr_type xlnk_userbuf[XLNK_BUF_POOL_SIZE];
static int xlnk_buf_process[XLNK_BUF_POOL_SIZE];
static dma_addr_t xlnk_phyaddr[XLNK_BUF_POOL_SIZE];
static size_t xlnk_buflen[XLNK_BUF_POOL_SIZE];
static unsigned int xlnk_bufcacheable[XLNK_BUF_POOL_SIZE];
static spinlock_t xlnk_buf_lock;

static int xlnk_open(struct inode *ip, struct file *filp);
static int xlnk_release(struct inode *ip, struct file *filp);
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

#define MAX_XLNK_DMAS 128

struct xlnk_device_pack {
	char name[64];
	struct platform_device pdev;
	struct resource res[8];
	struct uio_info *io_ptr;
	int refs;

#ifdef CONFIG_XILINX_DMA_APF
	struct xdma_channel_config dma_chan_cfg[4];  /* for xidane dma only */
	struct xdma_device_config dma_dev_cfg;	   /* for xidane dma only */
#endif

#ifdef CONFIG_XILINX_MCDMA
	struct xdma_device_info mcdma_dev_cfg;	 /* for mcdma only */
#endif

};

static struct semaphore xlnk_devpack_sem;
static struct xlnk_device_pack *xlnk_devpacks[MAX_XLNK_DMAS];
static void xlnk_devpacks_init(void)
{
	unsigned int i;

	sema_init(&xlnk_devpack_sem, 1);
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

	devpack->refs = 1;
	for (i = 0; i < MAX_XLNK_DMAS; i++) {
		if (!xlnk_devpacks[i]) {
			xlnk_devpacks[i] = devpack;
			break;
		}
	}
}

static struct xlnk_device_pack *xlnk_devpacks_find(xlnk_intptr_type base)
{
	unsigned int i;

	for (i = 0; i < MAX_XLNK_DMAS; i++) {
		if (xlnk_devpacks[i] &&
		    xlnk_devpacks[i]->res[0].start == base)
			return xlnk_devpacks[i];
	}
	return NULL;
}

static void xlnk_devpacks_free(xlnk_intptr_type base)
{
	struct xlnk_device_pack *devpack;

	down(&xlnk_devpack_sem);
	devpack = xlnk_devpacks_find(base);
	if (!devpack) {
		up(&xlnk_devpack_sem);
		return;
	}
	devpack->refs--;
	if (devpack->refs) {
		up(&xlnk_devpack_sem);
		return;
	}
	platform_device_unregister(&devpack->pdev);
	xlnk_devpacks_delete(devpack);
	kfree(devpack);
	up(&xlnk_devpack_sem);
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
		} else
			pr_err("%s: Unrecognized DMA type %s\n",
			       __func__, dma_name);
	}
	xlnk_set_config(&block);
}

static int xlnk_probe(struct platform_device *pdev)
{
	int err;
	dev_t dev = 0;

	xlnk_dev_buf = NULL;
	xlnk_dev_size = 0;
	xlnk_dev_vmas = 0;

	/* use 2.6 device model */
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
		if (xlnk_bufpool[i] &&
		    xlnk_phyaddr[i] <= addr &&
		    xlnk_phyaddr[i] + xlnk_buflen[i] > addr)
			return i;
	}

	return 0;
}

static int xlnk_buf_find_by_user_addr(xlnk_intptr_type addr, int pid)
{
	int i;

	for (i = 1; i < xlnk_bufpool_size; i++) {
		if (xlnk_bufpool[i] &&
		    xlnk_buf_process[i] == pid &&
		    xlnk_userbuf[i] <= addr &&
		    xlnk_userbuf[i] + xlnk_buflen[i] > addr)
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
	unsigned long attrs;

	attrs = cacheable ? DMA_ATTR_NON_CONSISTENT : 0;

	kaddr = dma_alloc_attrs(xlnk_dev,
				len,
				&phys_addr_anchor,
				GFP_KERNEL | GFP_DMA,
				attrs);
	if (!kaddr)
		return -ENOMEM;

	spin_lock(&xlnk_buf_lock);
	id = xlnk_buf_findnull();
	if (id > 0 && id < XLNK_BUF_POOL_SIZE) {
		xlnk_bufpool_alloc_point[id] = kaddr;
		xlnk_bufpool[id] = kaddr;
		xlnk_buflen[id] = len;
		xlnk_bufcacheable[id] = cacheable;
		xlnk_phyaddr[id] = phys_addr_anchor;
	}
	spin_unlock(&xlnk_buf_lock);

	if (id <= 0 || id >= XLNK_BUF_POOL_SIZE)
		return -ENOMEM;

	return id;
}

static int xlnk_init_bufpool(void)
{
	unsigned int i;

	spin_lock_init(&xlnk_buf_lock);
	xlnk_dev_buf = kmalloc(8192, GFP_KERNEL | GFP_DMA);
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

static u64 dma_mask = 0xFFFFFFFFFFFFFFFFull;

/*
 * This function is called when an application opens handle to the
 * bridge driver.
 */
static int xlnk_open(struct inode *ip, struct file *filp)
{
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
		xlnk_dev_size = 0;

	return 0;
}

static ssize_t xlnk_read(struct file *filp,
			 char __user *buf,
			 size_t count,
			 loff_t *offp)
{
	ssize_t retval = 0;

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

static int xlnk_devregister(char *name,
			    unsigned int id,
			    xlnk_intptr_type base,
			    unsigned int size,
			    unsigned int *irqs,
			    xlnk_intptr_type *handle)
{
	unsigned int nres;
	unsigned int nirq;
	unsigned int *irqptr;
	struct xlnk_device_pack *devpack;
	unsigned int i;
	int status;

	down(&xlnk_devpack_sem);
	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (xlnk_intptr_type)devpack;
		devpack->refs++;
		status = 0;
	} else {
		nirq = 0;
		irqptr = irqs;

		while (*irqptr) {
			nirq++;
			irqptr++;
		}

		if (nirq > 7) {
			up(&xlnk_devpack_sem);
			return -ENOMEM;
		}

		nres = nirq + 1;

		devpack = kzalloc(sizeof(*devpack),
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
			devpack->res[i + 1].start = irqs[i];
			devpack->res[i + 1].end = irqs[i];
			devpack->res[i + 1].flags = IORESOURCE_IRQ;
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
	}
	up(&xlnk_devpack_sem);

	return status;
}

static int xlnk_dmaregister(char *name,
			    unsigned int id,
			    xlnk_intptr_type base,
			    unsigned int size,
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

	down(&xlnk_devpack_sem);
	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (xlnk_intptr_type)devpack;
		devpack->refs++;
		status = 0;
	} else {
		devpack = kzalloc(sizeof(*devpack),
				  GFP_KERNEL);
		if (!devpack) {
			up(&xlnk_devpack_sem);
			return -ENOMEM;
		}
		strcpy(devpack->name, name);
		devpack->pdev.name = "xilinx-axidma";

		devpack->io_ptr = NULL;

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
			devpack->dma_chan_cfg[1].include_dre =
				chan1_include_dre;
			devpack->dma_chan_cfg[1].datawidth = chan1_data_width;
			devpack->dma_chan_cfg[1].irq = chan1_irq;
			devpack->dma_chan_cfg[1].poll_mode = chan1_poll_mode;
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
	}
	up(&xlnk_devpack_sem);

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

	if (strcmp(name, "xdma"))
		return -EINVAL;

	devpack = xlnk_devpacks_find(base);
	if (devpack)
		devpack->refs++;
	if (devpack) {
		*handle = (xlnk_intptr_type)devpack;
		return 0;
	}

	devpack = kzalloc(sizeof(*devpack),
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

static int xlnk_allocbuf_ioctl(struct file *filp,
			       unsigned int code,
			       unsigned long args)
{
	union xlnk_args temp_args;
	int status;
	xlnk_int_type id;

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
	status = copy_to_user((void __user *)args,
			      &temp_args,
			      sizeof(union xlnk_args));

	return status;
}

static int xlnk_freebuf(int id)
{
	void *alloc_point;
	dma_addr_t p_addr;
	size_t buf_len;
	int cacheable;
	unsigned long attrs;

	if (id <= 0 || id >= xlnk_bufpool_size)
		return -ENOMEM;

	if (!xlnk_bufpool[id])
		return -ENOMEM;

	spin_lock(&xlnk_buf_lock);
	alloc_point = xlnk_bufpool_alloc_point[id];
	p_addr = xlnk_phyaddr[id];
	buf_len = xlnk_buflen[id];
	xlnk_bufpool[id] = NULL;
	xlnk_phyaddr[id] = (dma_addr_t)NULL;
	xlnk_buflen[id] = 0;
	cacheable = xlnk_bufcacheable[id];
	xlnk_bufcacheable[id] = 0;
	spin_unlock(&xlnk_buf_lock);

	attrs = cacheable ? DMA_ATTR_NON_CONSISTENT : 0;

	dma_free_attrs(xlnk_dev,
		       buf_len,
		       alloc_point,
		       p_addr,
		       attrs);

	return 0;
}

static void xlnk_free_all_buf(void)
{
	int i;

	for (i = 1; i < xlnk_bufpool_size; i++)
		xlnk_freebuf(i);
}

static int xlnk_freebuf_ioctl(struct file *filp,
			      unsigned int code,
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

static int xlnk_adddmabuf_ioctl(struct file *filp,
				unsigned int code,
				unsigned long args)
{
	union xlnk_args temp_args;
	struct xlnk_dmabuf_reg *db;
	int status;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	spin_lock(&xlnk_buf_lock);
	list_for_each_entry(db, &xlnk_dmabuf_list, list) {
		if (db->user_vaddr == temp_args.dmasubmit.buf) {
			pr_err("Attempting to register DMA-BUF for addr %llx that is already registered\n",
			       (unsigned long long)temp_args.dmabuf.user_addr);
			spin_unlock(&xlnk_buf_lock);
			return -EINVAL;
		}
	}
	spin_unlock(&xlnk_buf_lock);

	db = kzalloc(sizeof(*db), GFP_KERNEL);
	if (!db)
		return -ENOMEM;

	db->dmabuf_fd = temp_args.dmabuf.dmabuf_fd;
	db->user_vaddr = temp_args.dmabuf.user_addr;
	db->dbuf = dma_buf_get(db->dmabuf_fd);
	db->dbuf_attach = dma_buf_attach(db->dbuf, xlnk_dev);
	if (IS_ERR(db->dbuf_attach)) {
		dma_buf_put(db->dbuf);
		pr_err("Failed DMA-BUF attach\n");
		return -EINVAL;
	}

	db->dbuf_sg_table = dma_buf_map_attachment(db->dbuf_attach,
						   DMA_BIDIRECTIONAL);

	if (!db->dbuf_sg_table) {
		pr_err("Failed DMA-BUF map_attachment\n");
		dma_buf_detach(db->dbuf, db->dbuf_attach);
		dma_buf_put(db->dbuf);
		return -EINVAL;
	}

	spin_lock(&xlnk_buf_lock);
	INIT_LIST_HEAD(&db->list);
	list_add_tail(&db->list, &xlnk_dmabuf_list);
	spin_unlock(&xlnk_buf_lock);

	return 0;
}

static int xlnk_cleardmabuf_ioctl(struct file *filp,
				  unsigned int code,
				  unsigned long args)
{
	union xlnk_args temp_args;
	struct xlnk_dmabuf_reg *dp, *dp_temp;
	int status;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	spin_lock(&xlnk_buf_lock);
	list_for_each_entry_safe(dp, dp_temp, &xlnk_dmabuf_list, list) {
		if (dp->user_vaddr == temp_args.dmabuf.user_addr) {
			dma_buf_unmap_attachment(dp->dbuf_attach,
						 dp->dbuf_sg_table,
						 DMA_BIDIRECTIONAL);
			dma_buf_detach(dp->dbuf, dp->dbuf_attach);
			dma_buf_put(dp->dbuf);
			list_del(&dp->list);
			spin_unlock(&xlnk_buf_lock);
			kfree(dp);
			return 0;
		}
	}
	spin_unlock(&xlnk_buf_lock);
	pr_err("Attempting to unregister a DMA-BUF that was not registered at addr %llx\n",
	       (unsigned long long)temp_args.dmabuf.user_addr);

	return 1;
}

static int xlnk_dmarequest_ioctl(struct file *filp, unsigned int code,
				 unsigned long args)
{
#ifdef CONFIG_XILINX_DMA_APF
	union xlnk_args temp_args;
	int status;
	struct xdma_chan *chan;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	if (!temp_args.dmarequest.name[0])
		return 0;

	down(&xlnk_devpack_sem);
	chan = xdma_request_channel(temp_args.dmarequest.name);
	up(&xlnk_devpack_sem);
	if (!chan)
		return -ENOMEM;
	temp_args.dmarequest.dmachan = (xlnk_intptr_type)chan;
	temp_args.dmarequest.bd_space_phys_addr = chan->bd_phys_addr;
	temp_args.dmarequest.bd_space_size = chan->bd_chain_size;

	if (copy_to_user((void __user *)args,
			 &temp_args,
			 sizeof(union xlnk_args)))
		return -EFAULT;

	return 0;
#else
	return -1;
#endif
}

static int xlnk_dmasubmit_ioctl(struct file *filp, unsigned int code,
				unsigned long args)
{
#ifdef CONFIG_XILINX_DMA_APF
	union xlnk_args temp_args;
	struct xdma_head *dmahead;
	struct xlnk_dmabuf_reg *dp, *cp = NULL;
	int buf_id;
	void *kaddr = NULL;
	int status = -1;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	if (!temp_args.dmasubmit.dmachan)
		return -ENODEV;

	spin_lock(&xlnk_buf_lock);
	buf_id = xlnk_buf_find_by_phys_addr(temp_args.dmasubmit.buf);
	if (buf_id) {
		xlnk_intptr_type addr_delta =
			temp_args.dmasubmit.buf -
			xlnk_phyaddr[buf_id];
		kaddr = (u8 *)(xlnk_bufpool[buf_id]) + addr_delta;
	} else {
		list_for_each_entry(dp, &xlnk_dmabuf_list, list) {
			if (dp->user_vaddr == temp_args.dmasubmit.buf) {
				cp = dp;
				break;
			}
		}
	}
	spin_unlock(&xlnk_buf_lock);

	status = xdma_submit((struct xdma_chan *)
					(temp_args.dmasubmit.dmachan),
					temp_args.dmasubmit.buf,
					kaddr,
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

	if (!status) {
		if (copy_to_user((void __user *)args,
				 &temp_args,
				 sizeof(union xlnk_args)))
			return -EFAULT;
	}
	return status;
#endif
	return -ENOMEM;
}

static int xlnk_dmawait_ioctl(struct file *filp,
			      unsigned int code,
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

	dmahead = (struct xdma_head *)temp_args.dmawait.dmahandle;
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
	down(&xlnk_devpack_sem);
	xdma_release_channel((struct xdma_chan *)
			     (temp_args.dmarelease.dmachan));
	up(&xlnk_devpack_sem);
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

static int xlnk_mcdmaregister_ioctl(struct file *filp,
				    unsigned int code,
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

static int xlnk_devunregister_ioctl(struct file *filp,
				    unsigned int code,
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
	void *kaddr;
	xlnk_intptr_type paddr;
	int buf_id;

	status = copy_from_user(&temp_args,
				(void __user *)args,
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

	spin_lock(&xlnk_buf_lock);
	buf_id = xlnk_buf_find_by_phys_addr(paddr);
	kaddr = xlnk_bufpool[buf_id];
	spin_unlock(&xlnk_buf_lock);

	if (buf_id == 0) {
		pr_err("Illegal cachecontrol on non-sds_alloc memory");
		return -EINVAL;
	}

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
		status = copy_to_user((void __user *)args, &block,
				      sizeof(struct xlnk_config_block));
	}
	return status;
}

static int xlnk_memop_ioctl(struct file *filp, unsigned long arg_addr)
{
	union xlnk_args args;
	xlnk_intptr_type p_addr = 0;
	int status = 0;
	int buf_id;
	struct xlnk_dmabuf_reg *cp = NULL;
	int cacheable = 1;
	enum dma_data_direction dmadir;
	xlnk_intptr_type page_id;
	unsigned int page_offset;
	struct scatterlist sg;
	unsigned long attrs = 0;

	status = copy_from_user(&args,
				(void __user *)arg_addr,
				sizeof(union xlnk_args));

	if (status) {
		pr_err("Error in copy_from_user.  status = %d\n", status);
		return status;
	}

	if (!(args.memop.flags & XLNK_FLAG_MEM_ACQUIRE) &&
	    !(args.memop.flags & XLNK_FLAG_MEM_RELEASE)) {
		pr_err("memop lacks acquire or release flag\n");
		return -EINVAL;
	}

	if (args.memop.flags & XLNK_FLAG_MEM_ACQUIRE &&
	    args.memop.flags & XLNK_FLAG_MEM_RELEASE) {
		pr_err("memop has both acquire and release defined\n");
		return -EINVAL;
	}

	spin_lock(&xlnk_buf_lock);
	buf_id = xlnk_buf_find_by_user_addr(args.memop.virt_addr,
					    current->pid);
	if (buf_id > 0) {
		cacheable = xlnk_bufcacheable[buf_id];
		p_addr = xlnk_phyaddr[buf_id] +
			(args.memop.virt_addr - xlnk_userbuf[buf_id]);
	} else {
		struct xlnk_dmabuf_reg *dp;

		list_for_each_entry(dp, &xlnk_dmabuf_list, list) {
			if (dp->user_vaddr == args.memop.virt_addr) {
				cp = dp;
				break;
			}
		}
	}
	spin_unlock(&xlnk_buf_lock);

	if (buf_id <= 0 && !cp) {
		pr_err("Error, buffer not found\n");
		return -EINVAL;
	}

	dmadir = (enum dma_data_direction)args.memop.dir;

	if (args.memop.flags & XLNK_FLAG_COHERENT || !cacheable)
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	if (buf_id > 0) {
		page_id = p_addr >> PAGE_SHIFT;
		page_offset = p_addr - (page_id << PAGE_SHIFT);
		sg_init_table(&sg, 1);
		sg_set_page(&sg,
			    pfn_to_page(page_id),
			    args.memop.size,
			    page_offset);
		sg_dma_len(&sg) = args.memop.size;
	}

	if (args.memop.flags & XLNK_FLAG_MEM_ACQUIRE) {
		if (buf_id > 0) {
			status = get_dma_ops(xlnk_dev)->map_sg(xlnk_dev,
							       &sg,
							       1,
							       dmadir,
							       attrs);
			if (!status) {
				pr_err("Failed to map address\n");
				return -EINVAL;
			}
			args.memop.phys_addr = (xlnk_intptr_type)
				sg_dma_address(&sg);
			args.memop.token = (xlnk_intptr_type)
				sg_dma_address(&sg);
			status = copy_to_user((void __user *)arg_addr,
					      &args,
					      sizeof(union xlnk_args));
			if (status)
				pr_err("Error in copy_to_user.  status = %d\n",
				       status);
		} else {
			if (cp->dbuf_sg_table->nents != 1) {
				pr_err("Non-SG-DMA datamovers require physically contiguous DMABUFs.  DMABUF is not physically contiguous\n");
				return -EINVAL;
			}
			args.memop.phys_addr = (xlnk_intptr_type)
				sg_dma_address(cp->dbuf_sg_table->sgl);
			args.memop.token = 0;
			status = copy_to_user((void __user *)arg_addr,
					      &args,
					      sizeof(union xlnk_args));
			if (status)
				pr_err("Error in copy_to_user.  status = %d\n",
				       status);
		}
	} else {
		if (buf_id > 0) {
			sg_dma_address(&sg) = (dma_addr_t)args.memop.token;
			get_dma_ops(xlnk_dev)->unmap_sg(xlnk_dev,
							&sg,
							1,
							dmadir,
							attrs);
		}
	}

	return status;
}

/* This function provides IO interface to the bridge driver. */
static long xlnk_ioctl(struct file *filp,
		       unsigned int code,
		       unsigned long args)
{
	if (_IOC_TYPE(code) != XLNK_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(code) > XLNK_IOC_MAXNR)
		return -ENOTTY;

	/* some sanity check */
	switch (code) {
	case XLNK_IOCALLOCBUF:
		return xlnk_allocbuf_ioctl(filp, code, args);
	case XLNK_IOCFREEBUF:
		return xlnk_freebuf_ioctl(filp, code, args);
	case XLNK_IOCADDDMABUF:
		return xlnk_adddmabuf_ioctl(filp, code, args);
	case XLNK_IOCCLEARDMABUF:
		return xlnk_cleardmabuf_ioctl(filp, code, args);
	case XLNK_IOCDMAREQUEST:
		return xlnk_dmarequest_ioctl(filp, code, args);
	case XLNK_IOCDMASUBMIT:
		return xlnk_dmasubmit_ioctl(filp, code, args);
	case XLNK_IOCDMAWAIT:
		return xlnk_dmawait_ioctl(filp, code, args);
	case XLNK_IOCDMARELEASE:
		return xlnk_dmarelease_ioctl(filp, code, args);
	case XLNK_IOCDEVREGISTER:
		return xlnk_devregister_ioctl(filp, code, args);
	case XLNK_IOCDMAREGISTER:
		return xlnk_dmaregister_ioctl(filp, code, args);
	case XLNK_IOCMCDMAREGISTER:
		return xlnk_mcdmaregister_ioctl(filp, code, args);
	case XLNK_IOCDEVUNREGISTER:
		return xlnk_devunregister_ioctl(filp, code, args);
	case XLNK_IOCCACHECTRL:
		return xlnk_cachecontrol_ioctl(filp, code, args);
	case XLNK_IOCSHUTDOWN:
		return xlnk_shutdown(args);
	case XLNK_IOCRECRES:
		return xlnk_recover_resource(args);
	case XLNK_IOCCONFIG:
		return xlnk_config_ioctl(filp, args);
	case XLNK_IOCMEMOP:
		return xlnk_memop_ioctl(filp, args);
	default:
		return -EINVAL;
	}
}

static const struct vm_operations_struct xlnk_vm_ops = {
	.open = xlnk_vma_open,
	.close = xlnk_vma_close,
};

/* This function maps kernel space memory to user space memory. */
static int xlnk_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int bufid;
	int status;

	bufid = vma->vm_pgoff >> (16 - PAGE_SHIFT);

	if (bufid == 0) {
		unsigned long paddr = virt_to_phys(xlnk_dev_buf);

		status = remap_pfn_range(vma,
					 vma->vm_start,
					 paddr >> PAGE_SHIFT,
					 vma->vm_end - vma->vm_start,
					 vma->vm_page_prot);
	} else {
		if (xlnk_bufcacheable[bufid] == 0)
			vma->vm_page_prot =
				pgprot_noncached(vma->vm_page_prot);
		status = remap_pfn_range(vma, vma->vm_start,
					 xlnk_phyaddr[bufid]
					 >> PAGE_SHIFT,
					 vma->vm_end - vma->vm_start,
					 vma->vm_page_prot);
		xlnk_userbuf[bufid] = vma->vm_start;
		xlnk_buf_process[bufid] = current->pid;
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

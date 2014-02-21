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


#include "xlnk-ioctl.h"
#include "xlnk-event-tracer-type.h"
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
static void **xlnk_bufpool;
static unsigned int xlnk_bufpool_size = XLNK_BUF_POOL_SIZE;
static dma_addr_t xlnk_phyaddr[XLNK_BUF_POOL_SIZE];
static size_t xlnk_buflen[XLNK_BUF_POOL_SIZE];
static unsigned int  xlnk_bufcacheable[XLNK_BUF_POOL_SIZE];


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

static void xlnk_start_benchmark_counter(void);
static int xlnk_dump_events(unsigned long buf);
static int xlnk_get_event_size(unsigned long buf);

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

#ifdef CONFIG_XILINX_DMA_APF
	struct dma_channel_config dma_chan_cfg[4];  /* for xidane dma only */
	struct dma_device_config dma_dev_cfg;	   /* for xidane dma only */
#endif

#ifdef CONFIG_XILINX_MCDMA
	struct xdma_device_info mcdma_dev_cfg;	 /* for mcdma only */
#endif

};

static struct xlnk_device_pack *xlnk_devpacks[16];
static void xlnk_devpacks_init(void)
{
	unsigned int i;

	for (i = 0; i < 16; i++)
		xlnk_devpacks[0] = NULL;

}

static void xlnk_devpacks_delete(struct xlnk_device_pack *devpack)
{
	unsigned int i;

	for (i = 0; i < 16; i++) {
		if (xlnk_devpacks[i] == devpack)
			xlnk_devpacks[i] = NULL;
	}
}

static void xlnk_devpacks_add(struct xlnk_device_pack *devpack)
{
	unsigned int i;

	for (i = 0; i < 16; i++) {
		if (xlnk_devpacks[i] == NULL) {
			xlnk_devpacks[i] = devpack;
			break;
		}
	}
}

static struct xlnk_device_pack *xlnk_devpacks_find(unsigned long base)
{
	unsigned int i;

	for (i = 0; i < 16; i++) {
		if (xlnk_devpacks[i]
			&& xlnk_devpacks[i]->res[0].start == base)
			return xlnk_devpacks[i];
	}
	return NULL;
}

static void xlnk_devpacks_free(unsigned long base)
{
	struct xlnk_device_pack *devpack;

	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		platform_device_unregister(&devpack->pdev);
		kfree(devpack);
		xlnk_devpacks_delete(devpack);
	}
}

static void xlnk_devpacks_free_all(void)
{
	struct xlnk_device_pack *devpack;
	unsigned int i;

	for (i = 0; i < 16; i++) {
		devpack = xlnk_devpacks[i];
		if (devpack) {
			platform_device_unregister(&devpack->pdev);
			kfree(devpack);
			xlnk_devpacks_delete(devpack);
		}
	}
}

static int xlnk_probe(struct platform_device *pdev)
{
	int err;
	dev_t dev = 0;

	/* use 2.6 device model */
	err = alloc_chrdev_region(&dev, 0, 1, driver_name);
	if (err) {
		pr_err("%s: Can't get major %d\n", __func__, driver_major);
		goto err1;
	}

	cdev_init(&xlnk_cdev, &xlnk_fops);

	xlnk_cdev.owner = THIS_MODULE;

	err = cdev_add(&xlnk_cdev, dev, 1);

	if (err) {
		pr_err("%s: Failed to add XLNK device\n", __func__);
		goto err3;
	}

	/* udev support */
	xlnk_class = class_create(THIS_MODULE, "xlnk");
	if (IS_ERR(xlnk_class)) {
		pr_err("%s: Error creating xlnk class\n", __func__);
		goto err3;
	}

	driver_major = MAJOR(dev);

	pr_info("xlnk major %d\n", driver_major);

	device_create(xlnk_class, NULL, MKDEV(driver_major, 0),
			  NULL, "xlnk");

	xlnk_init_bufpool();

	pr_info("%s driver loaded\n", DRIVER_NAME);

	xlnk_pdev = pdev;
	xlnk_dev = &pdev->dev;

	if (xlnk_pdev)
		pr_info("xlnk_pdev is not null\n");
	else
		pr_info("xlnk_pdev is null\n");

	xlnk_devpacks_init();

#ifdef CONFIG_ARCH_ZYNQ
	xlnk_start_benchmark_counter();
#endif

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

/**
 * allocate and return an id
 * id must be a positve number
 */
static int xlnk_allocbuf(unsigned int len, unsigned int cacheable)
{
	int id;

	id = xlnk_buf_findnull();

	if (id <= 0)
		return -ENOMEM;

	xlnk_bufpool[id] = dma_alloc_coherent(xlnk_dev, len,
					      &xlnk_phyaddr[id],
					      GFP_KERNEL | GFP_DMA);
	xlnk_buflen[id] = len;
	xlnk_bufcacheable[id] = cacheable;

	if (!xlnk_bufpool[id]) {
		pr_err("%s: dma_alloc_coherent of %d byte buffer failed\n",
		       __func__, len);
		return -ENOMEM;
	}

	return id;
}

static int xlnk_init_bufpool(void)
{
	unsigned int i;

	xlnk_dev_buf = kmalloc(8192, GFP_KERNEL | __GFP_DMA);
	*((char *)xlnk_dev_buf) = '\0';

	if (!xlnk_dev_buf) {
		pr_err("%s: malloc failed\n", __func__);
		return -ENOMEM;
	}

	xlnk_bufpool = kmalloc(sizeof(void *) * xlnk_bufpool_size,
				   GFP_KERNEL);

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

	kfree(xlnk_bufpool);
	xlnk_bufpool = NULL;

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


static struct platform_driver xlnk_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   },
	.probe = xlnk_probe,
	.remove = xlnk_remove,
	.suspend = XLNK_SUSPEND,
	.resume = XLNK_RESUME,
};

static u64 dma_mask = 0xFFFFFFFFUL;

static struct platform_device xlnk_device = {
	.name = "xlnk",
	.id = 0,
	.dev = {
		.platform_data = NULL,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = NULL,
	.num_resources = 0,
};

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
	int status = 0;
	return status;
}


static int xlnk_devregister(char *name, unsigned int id,
				unsigned long base, unsigned int size,
				unsigned int *irqs,
				u32 *handle)
{
	unsigned int nres;
	unsigned int nirq;
	unsigned int *irqptr;
	struct xlnk_device_pack *devpack;
	unsigned int i;
	int status;

	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (u32)devpack;
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
	strcpy(devpack->name, name);
	devpack->pdev.name = devpack->name;

	devpack->pdev.id = id;

	devpack->pdev.dev.dma_mask = &dma_mask;
	devpack->pdev.dev.coherent_dma_mask = 0xFFFFFFFF;

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
		*handle = (u32)devpack;
	}
	return status;
}

static int xlnk_dmaregister(char *name, unsigned int id,
				unsigned long base, unsigned int size,
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
				u32 *handle)
{
	int status = -1;

#ifdef CONFIG_XILINX_DMA_APF

	struct xlnk_device_pack *devpack;

	if (strcmp(name, "xilinx-axidma"))
		return -EINVAL;

	if (chan_num < 1 || chan_num > 2)
		return -EINVAL;

	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (u32)devpack;
		return 0;
	}

	devpack = kzalloc(sizeof(struct xlnk_device_pack),
			  GFP_KERNEL);
	if (!devpack)
		return -ENOMEM;

	strcpy(devpack->name, name);
	devpack->pdev.name = devpack->name;

	devpack->pdev.id = id;

	devpack->dma_chan_cfg[0].include_dre = chan0_include_dre;
	devpack->dma_chan_cfg[0].datawidth   = chan0_data_width;
	devpack->dma_chan_cfg[0].irq = chan0_irq;
	devpack->dma_chan_cfg[0].poll_mode   = chan0_poll_mode;
	devpack->dma_chan_cfg[0].type = chan0_dir ?
					"axi-dma-s2mm-channel" :
					"axi-dma-mm2s-channel";

	if (chan_num > 1) {
		devpack->dma_chan_cfg[1].include_dre = chan1_include_dre;
		devpack->dma_chan_cfg[1].datawidth   = chan1_data_width;
		devpack->dma_chan_cfg[1].irq = chan1_irq;
		devpack->dma_chan_cfg[1].poll_mode   = chan1_poll_mode;
		devpack->dma_chan_cfg[1].type = chan1_dir ?
						"axi-dma-s2mm-channel" :
						"axi-dma-mm2s-channel";
	}

	devpack->dma_dev_cfg.type = "axi-dma";
	devpack->dma_dev_cfg.include_sg = 1;
	devpack->dma_dev_cfg.sg_include_stscntrl_strm = 1;
	devpack->dma_dev_cfg.channel_count = chan_num;
	devpack->dma_dev_cfg.channel_config = &devpack->dma_chan_cfg[0];

	devpack->pdev.dev.platform_data = &devpack->dma_dev_cfg;

	devpack->pdev.dev.dma_mask = &dma_mask;
	devpack->pdev.dev.coherent_dma_mask = 0xFFFFFFFF;

	devpack->res[0].start = base;
	devpack->res[0].end = base + size - 1;
	devpack->res[0].flags = IORESOURCE_MEM;

	devpack->pdev.resource = devpack->res;
	devpack->pdev.num_resources = 1;

	status = platform_device_register(&devpack->pdev);
	if (status) {
		kfree(devpack);
		*handle = 0;
	} else {
		xlnk_devpacks_add(devpack);
		*handle = (u32)devpack;
	}

#endif
	return status;
}

static int xlnk_mcdmaregister(char *name, unsigned int id,
			      unsigned long base, unsigned int size,
			      unsigned int mm2s_chan_num,
			      unsigned int mm2s_chan_irq,
			      unsigned int s2mm_chan_num,
			      unsigned int s2mm_chan_irq,
			      u32 *handle)
{
	int status = -1;

#ifdef CONFIG_XILINX_MCDMA
	struct xlnk_device_pack *devpack;

	if (strcmp(name, "xdma"))
		return -EINVAL;


	devpack = xlnk_devpacks_find(base);
	if (devpack) {
		*handle = (u32)devpack;
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
	devpack->pdev.dev.coherent_dma_mask = 0xFFFFFFFF;
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
		*handle = (u32)devpack;
	}

#endif

	return status;
}

static int xlnk_allocbuf_ioctl(struct file *filp, unsigned int code,
			unsigned long args)
{

	union xlnk_args temp_args;
	int status;
	int id;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	id = xlnk_allocbuf(temp_args.allocbuf.len,
			   temp_args.allocbuf.cacheable);

	if (id <= 0)
		return -ENOMEM;

	put_user(id, temp_args.allocbuf.idptr);
	put_user((u32)(xlnk_phyaddr[id]), temp_args.allocbuf.phyaddrptr);

	return 0;
}

static int xlnk_freebuf(int id)
{

	if (id <= 0 || id >= xlnk_bufpool_size)
		return -ENOMEM;

	if (!xlnk_bufpool[id])
		return -ENOMEM;

	dma_free_coherent(xlnk_dev, xlnk_buflen[id], xlnk_bufpool[id],
			  xlnk_phyaddr[id]);

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

	chan = xdma_request_channel(temp_args.dmarequest.name);

	if (!chan) {
		return -ENOMEM;
	}

	temp_args.dmarequest.dmachan = (u32)chan;
	temp_args.dmarequest.bd_space_phys_addr = chan->bd_phys_addr;
	temp_args.dmarequest.bd_space_size = chan->bd_chain_size;

	if (copy_to_user((void __user *)args, &temp_args,
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
	int status = -1;

	status = copy_from_user(&temp_args, (void __user *)args,
				sizeof(union xlnk_args));

	if (status)
		return -ENOMEM;

	if (!temp_args.dmasubmit.dmachan)
		return -ENODEV;

	status = xdma_submit((struct xdma_chan *)temp_args.dmasubmit.dmachan,
						temp_args.dmasubmit.buf,
						temp_args.dmasubmit.len,
						temp_args.dmasubmit.nappwords_i,
						temp_args.dmasubmit.appwords_i,
						temp_args.dmasubmit.nappwords_o,
						temp_args.dmasubmit.flag,
						&dmahead);

	if (!status) {
		temp_args.dmasubmit.dmahandle = (u32)dmahead;
		temp_args.dmasubmit.last_bd_index =
					(u32)dmahead->last_bd_index;
		if (copy_to_user((void __user *)args, &temp_args,
				sizeof(union xlnk_args)))
			return -EFAULT;
		return 0;
	}
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

	dmahead = (struct xdma_head *)temp_args.dmawait.dmahandle;
	status = xdma_wait(dmahead, dmahead->userflag);

	if (temp_args.dmawait.nappwords) {
		memcpy(temp_args.dmawait.appwords, dmahead->appwords_o,
			   dmahead->nappwords_o * sizeof(u32));

		if (copy_to_user((void __user *)args, &temp_args,
				sizeof(union xlnk_args)))
			return -EFAULT;
	}
	kfree(dmahead);

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

	xdma_release_channel((struct xdma_chan *)temp_args.dmarelease.dmachan);
#endif

	return status;
}


static int xlnk_devregister_ioctl(struct file *filp, unsigned int code,
				  unsigned long args)
{
	union xlnk_args temp_args;
	int status;
	u32 handle;

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
	u32 handle;

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
	u32 handle;

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

	status = copy_from_user(&temp_args, (void __user *)args,
						sizeof(union xlnk_args));

	if (status) {
		pr_err("Error in copy_from_user. status = %d\n", status);
		return -ENOMEM;
	}

	if (!(temp_args.cachecontrol.action == 0 ||
		  temp_args.cachecontrol.action == 1)) {
		pr_err("Illegal action specified to cachecontrol_ioctl: %d\n",
		       temp_args.cachecontrol.action);
		return -EINVAL;
	}

	size = temp_args.cachecontrol.size;
	paddr = temp_args.cachecontrol.phys_addr;
	kaddr = phys_to_virt((unsigned int)paddr);

	if (temp_args.cachecontrol.action == 0) {
		/* flush cache */
		dmac_map_area(kaddr, size, DMA_TO_DEVICE);
		outer_clean_range((unsigned int)paddr,
				  (unsigned int)(paddr + size));
	} else {
		/* invalidate cache */
		outer_inv_range((unsigned int)paddr,
				(unsigned int)(paddr + size));
		dmac_unmap_area(kaddr, size, DMA_FROM_DEVICE);
	}

	return 0;
}

/* This function provides IO interface to the bridge driver. */
static long xlnk_ioctl(struct file *filp, unsigned int code,
			 unsigned long args)
{
	int status = 0;

	xlnk_record_event(XLNK_ET_KERNEL_ENTER_IOCTL);

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
	case XLNK_IOCGETEVENTSIZE:
		status = xlnk_get_event_size(args);
		break;
	case XLNK_IOCCACHECTRL:
		status = xlnk_cachecontrol_ioctl(filp, code, args);
		break;
	case XLNK_IOCDUMPEVENTS:
		status = xlnk_dump_events(args);
		break;
	case XLNK_IOCSHUTDOWN:
		status = xlnk_shutdown(args);
		break;
	case XLNK_IOCRECRES: /* recover resource */
		status = xlnk_recover_resource(args);
		break;
	}

	xlnk_record_event(XLNK_ET_KERNEL_LEAVE_IOCTL);
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
		if (xlnk_bufcacheable[bufid] == 0)
			vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

		status = remap_pfn_range(vma, vma->vm_start,
					 xlnk_phyaddr[bufid] >> PAGE_SHIFT,
					 vma->vm_end - vma->vm_start,
					 vma->vm_page_prot);
	}
	if (status)
		return -EAGAIN;

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


#ifdef CONFIG_ARCH_ZYNQ

/*
 * Xidane XLNK benchmark counter support
 */
static u32 __iomem *bc_virt;


/* Zynq global counter */
static const unsigned long bc_phyaddr = 0xF8F00200;
static const unsigned long bc_to_cpu_shift = 1;
static const unsigned long bc_csr_size = 16;
static const unsigned long bc_ctr_offset = 2;
static const unsigned long bc_ctr_start = 1;
static const unsigned long bc_data_offset;


static void xlnk_start_benchmark_counter(void)
{
	bc_virt = ioremap(bc_phyaddr, bc_csr_size);
	if (bc_virt) {
		iowrite32(bc_ctr_start, bc_virt + bc_ctr_offset);
		pr_info("xlnk: benchmark counter started\n");
		/* iounmap(bc_virt); */
	}
}

#define XLNK_EVENT_TRACER_ENTRY_NUM 60000
static struct event_tracer {
	u32 event_id;
	u32 event_time;
} xlnk_et[XLNK_EVENT_TRACER_ENTRY_NUM];

static unsigned long xlnk_et_index;
static unsigned long xlnk_et_numbers_to_dump;

void xlnk_record_event(u32 event_id)
{
	if (xlnk_et_index >= XLNK_EVENT_TRACER_ENTRY_NUM)
		return;

	xlnk_et[xlnk_et_index].event_id = event_id;
	xlnk_et[xlnk_et_index].event_time = ioread32(bc_virt +
						bc_data_offset) <<
						bc_to_cpu_shift;
	xlnk_et_index++;
}
EXPORT_SYMBOL(xlnk_record_event);

static int xlnk_get_event_size(unsigned long args)
{
	unsigned long __user *datap = (unsigned long __user *)args;

	/* take a snapshot of current index and only copy this
	 * size to user space thru xlnk_dump_events(), as the snapshot
	 * value determine the dynamically created user space event
	 * trace buffer size  but the xlnk_et_index could keep going up
	 * with any xlnk_record_event() calls after this function
	 */
	xlnk_et_numbers_to_dump = xlnk_et_index;
	put_user(xlnk_et_numbers_to_dump, datap);
	return 0;
}

static int xlnk_dump_events(unsigned long buf)
{
	/* only dump the number of event traces reported thru
	 * xlnk_get_event_size() and ignore the rest to avoid
	 * buffer overflow issue
	 */
	if (copy_to_user((void __user *)buf, xlnk_et,
		xlnk_et_numbers_to_dump * sizeof(struct event_tracer)))
		return -EFAULT;

	/* clear up event pool so it's ready to use again */
	xlnk_et_index = 0;
	xlnk_et_numbers_to_dump = 0;

	return 0;
}
#endif


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

static int __init xlnk_init(void)
{
	pr_info("%s driver initializing\n", DRIVER_NAME);

	xlnk_dev_buf = NULL;
	xlnk_dev_size = 0;
	xlnk_dev_vmas = 0;
	xlnk_bufpool = NULL;

	platform_device_register(&xlnk_device);

	return platform_driver_register(&xlnk_driver);
}

static void __exit xlnk_exit(void)
{
	platform_driver_unregister(&xlnk_driver);
}

/* APF driver initialization and de-initialization functions */
module_init(xlnk_init);
module_exit(xlnk_exit);

MODULE_DESCRIPTION("Xilinx APF driver");
MODULE_LICENSE("GPL");

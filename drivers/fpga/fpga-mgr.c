// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Manager Core
 *
 *  Copyright (C) 2013-2015 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
 *
 * With code from the mailing list:
 * Copyright (C) 2013 Xilinx, Inc.
 */
#include <linux/dma-buf.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>

static DEFINE_IDA(fpga_mgr_ida);
static struct class *fpga_mgr_class;

/**
 * fpga_image_info_alloc - Allocate a FPGA image info struct
 * @dev: owning device
 *
 * Return: struct fpga_image_info or NULL
 */
struct fpga_image_info *fpga_image_info_alloc(struct device *dev)
{
	struct fpga_image_info *info;

	get_device(dev);

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		put_device(dev);
		return NULL;
	}

	info->dev = dev;

	return info;
}
EXPORT_SYMBOL_GPL(fpga_image_info_alloc);

/**
 * fpga_image_info_free - Free a FPGA image info struct
 * @info: FPGA image info struct to free
 */
void fpga_image_info_free(struct fpga_image_info *info)
{
	struct device *dev;

	if (!info)
		return;

	dev = info->dev;
	if (info->firmware_name)
		devm_kfree(dev, info->firmware_name);

	devm_kfree(dev, info);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(fpga_image_info_free);

/*
 * Call the low level driver's write_init function.  This will do the
 * device-specific things to get the FPGA into the state where it is ready to
 * receive an FPGA image. The low level driver only gets to see the first
 * initial_header_size bytes in the buffer.
 */
static int fpga_mgr_write_init_buf(struct fpga_manager *mgr,
				   struct fpga_image_info *info,
				   const char *buf, size_t count)
{
	int ret;

	mgr->state = FPGA_MGR_STATE_WRITE_INIT;
	if (!mgr->mops->initial_header_size)
		ret = mgr->mops->write_init(mgr, info, NULL, 0);
	else
		ret = mgr->mops->write_init(
		    mgr, info, buf, min(mgr->mops->initial_header_size, count));

	if (ret) {
		dev_err(&mgr->dev, "Error preparing FPGA for writing\n");
		mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
		return ret;
	}

	return 0;
}

static int fpga_mgr_write_init_sg(struct fpga_manager *mgr,
				  struct fpga_image_info *info,
				  struct sg_table *sgt)
{
	struct sg_mapping_iter miter;
	size_t len;
	char *buf;
	int ret;

	if (!mgr->mops->initial_header_size)
		return fpga_mgr_write_init_buf(mgr, info, NULL, 0);

	/*
	 * First try to use miter to map the first fragment to access the
	 * header, this is the typical path.
	 */
	sg_miter_start(&miter, sgt->sgl, sgt->nents, SG_MITER_FROM_SG);
	if (sg_miter_next(&miter) &&
	    miter.length >= mgr->mops->initial_header_size) {
		ret = fpga_mgr_write_init_buf(mgr, info, miter.addr,
					      miter.length);
		sg_miter_stop(&miter);
		return ret;
	}
	sg_miter_stop(&miter);

	/* Otherwise copy the fragments into temporary memory. */
	buf = kmalloc(mgr->mops->initial_header_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = sg_copy_to_buffer(sgt->sgl, sgt->nents, buf,
				mgr->mops->initial_header_size);
	ret = fpga_mgr_write_init_buf(mgr, info, buf, len);

	kfree(buf);

	return ret;
}

/*
 * After all the FPGA image has been written, do the device specific steps to
 * finish and set the FPGA into operating mode.
 */
static int fpga_mgr_write_complete(struct fpga_manager *mgr,
				   struct fpga_image_info *info)
{
	int ret;

	mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE;
	ret = mgr->mops->write_complete(mgr, info);
	if (ret) {
		dev_err(&mgr->dev, "Error after writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE_ERR;
		return ret;
	}
	mgr->state = FPGA_MGR_STATE_OPERATING;

	return 0;
}

/**
 * fpga_mgr_buf_load_sg - load fpga from image in buffer from a scatter list
 * @mgr:	fpga manager
 * @info:	fpga image specific information
 * @sgt:	scatterlist table
 *
 * Step the low level fpga manager through the device-specific steps of getting
 * an FPGA ready to be configured, writing the image to it, then doing whatever
 * post-configuration steps necessary.  This code assumes the caller got the
 * mgr pointer from of_fpga_mgr_get() or fpga_mgr_get() and checked that it is
 * not an error code.
 *
 * This is the preferred entry point for FPGA programming, it does not require
 * any contiguous kernel memory.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int fpga_mgr_buf_load_sg(struct fpga_manager *mgr,
				struct fpga_image_info *info,
				struct sg_table *sgt)
{
	int ret;

	if (info->flags & FPGA_MGR_USERKEY_ENCRYPTED_BITSTREAM)
		memcpy(info->key, mgr->key, ENCRYPTED_KEY_LEN);

	ret = fpga_mgr_write_init_sg(mgr, info, sgt);
	if (ret)
		return ret;

	/* Write the FPGA image to the FPGA. */
	mgr->state = FPGA_MGR_STATE_WRITE;
	if (mgr->mops->write_sg) {
		ret = mgr->mops->write_sg(mgr, sgt);
	} else {
		struct sg_mapping_iter miter;

		sg_miter_start(&miter, sgt->sgl, sgt->nents, SG_MITER_FROM_SG);
		while (sg_miter_next(&miter)) {
			ret = mgr->mops->write(mgr, miter.addr, miter.length);
			if (ret)
				break;
		}
		sg_miter_stop(&miter);
	}

	if (ret) {
		dev_err(&mgr->dev, "Error while writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_ERR;
		return ret;
	}

	return fpga_mgr_write_complete(mgr, info);
}

static int fpga_mgr_buf_load_mapped(struct fpga_manager *mgr,
				    struct fpga_image_info *info,
				    const char *buf, size_t count)
{
	int ret;

	ret = fpga_mgr_write_init_buf(mgr, info, buf, count);
	if (ret)
		return ret;

	/*
	 * Write the FPGA image to the FPGA.
	 */
	mgr->state = FPGA_MGR_STATE_WRITE;
	ret = mgr->mops->write(mgr, buf, count);
	if (ret) {
		dev_err(&mgr->dev, "Error while writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_ERR;
		return ret;
	}

	return fpga_mgr_write_complete(mgr, info);
}

/**
 * fpga_mgr_buf_load - load fpga from image in buffer
 * @mgr:	fpga manager
 * @info:	fpga image info
 * @buf:	buffer contain fpga image
 * @count:	byte count of buf
 *
 * Step the low level fpga manager through the device-specific steps of getting
 * an FPGA ready to be configured, writing the image to it, then doing whatever
 * post-configuration steps necessary.  This code assumes the caller got the
 * mgr pointer from of_fpga_mgr_get() and checked that it is not an error code.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int fpga_mgr_buf_load(struct fpga_manager *mgr,
			     struct fpga_image_info *info,
			     const char *buf, size_t count)
{
	struct page **pages;
	struct sg_table sgt;
	const void *p;
	int nr_pages;
	int index;
	int rc;

	/*
	 * This is just a fast path if the caller has already created a
	 * contiguous kernel buffer and the driver doesn't require SG, non-SG
	 * drivers will still work on the slow path.
	 */
	if (mgr->mops->write)
		return fpga_mgr_buf_load_mapped(mgr, info, buf, count);

	/*
	 * Convert the linear kernel pointer into a sg_table of pages for use
	 * by the driver.
	 */
	nr_pages = DIV_ROUND_UP((unsigned long)buf + count, PAGE_SIZE) -
		   (unsigned long)buf / PAGE_SIZE;
	pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	p = buf - offset_in_page(buf);
	for (index = 0; index < nr_pages; index++) {
		if (is_vmalloc_addr(p))
			pages[index] = vmalloc_to_page(p);
		else
			pages[index] = kmap_to_page((void *)p);
		if (!pages[index]) {
			kfree(pages);
			return -EFAULT;
		}
		p += PAGE_SIZE;
	}

	/*
	 * The temporary pages list is used to code share the merging algorithm
	 * in sg_alloc_table_from_pages
	 */
	rc = sg_alloc_table_from_pages(&sgt, pages, index, offset_in_page(buf),
				       count, GFP_KERNEL);
	kfree(pages);
	if (rc)
		return rc;

	rc = fpga_mgr_buf_load_sg(mgr, info, &sgt);
	sg_free_table(&sgt);

	return rc;
}

static int fpga_dmabuf_load(struct fpga_manager *mgr,
			    struct fpga_image_info *info)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	int ret;

	/* create attachment for dmabuf with the user device */
	attach = dma_buf_attach(mgr->dmabuf, &mgr->dev);
	if (IS_ERR(attach)) {
		pr_err("failed to attach dmabuf\n");
		ret = PTR_ERR(attach);
		goto fail_put;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	info->sgt = sgt;
	ret = fpga_mgr_buf_load_sg(mgr, info, info->sgt);
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);

fail_detach:
	dma_buf_detach(mgr->dmabuf, attach);
fail_put:
	dma_buf_put(mgr->dmabuf);

	return ret;
}

/**
 * fpga_mgr_firmware_load - request firmware and load to fpga
 * @mgr:	fpga manager
 * @info:	fpga image specific information
 * @image_name:	name of image file on the firmware search path
 *
 * Request an FPGA image using the firmware class, then write out to the FPGA.
 * Update the state before each step to provide info on what step failed if
 * there is a failure.  This code assumes the caller got the mgr pointer
 * from of_fpga_mgr_get() or fpga_mgr_get() and checked that it is not an error
 * code.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int fpga_mgr_firmware_load(struct fpga_manager *mgr,
				  struct fpga_image_info *info,
				  const char *image_name)
{
	struct device *dev = &mgr->dev;
	const struct firmware *fw;
	int ret;

	dev_info(dev, "writing %s to %s\n", image_name, mgr->name);

	mgr->state = FPGA_MGR_STATE_FIRMWARE_REQ;

	/* flags indicates whether to do full or partial reconfiguration */
	info->flags = mgr->flags;
	memcpy(info->key, mgr->key, ENCRYPTED_KEY_LEN);

	ret = request_firmware(&fw, image_name, dev);
	if (ret) {
		mgr->state = FPGA_MGR_STATE_FIRMWARE_REQ_ERR;
		dev_err(dev, "Error requesting firmware %s\n", image_name);
		return ret;
	}

	ret = fpga_mgr_buf_load(mgr, info, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

/**
 * fpga_mgr_load - load FPGA from scatter/gather table, buffer, or firmware
 * @mgr:	fpga manager
 * @info:	fpga image information.
 *
 * Load the FPGA from an image which is indicated in @info.  If successful, the
 * FPGA ends up in operating mode.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int fpga_mgr_load(struct fpga_manager *mgr, struct fpga_image_info *info)
{
	if (info->flags & FPGA_MGR_CONFIG_DMA_BUF)
		return fpga_dmabuf_load(mgr, info);
	if (info->sgt)
		return fpga_mgr_buf_load_sg(mgr, info, info->sgt);
	if (info->buf && info->count)
		return fpga_mgr_buf_load(mgr, info, info->buf, info->count);
	if (info->firmware_name)
		return fpga_mgr_firmware_load(mgr, info, info->firmware_name);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(fpga_mgr_load);

static const char * const state_str[] = {
	[FPGA_MGR_STATE_UNKNOWN] =		"unknown",
	[FPGA_MGR_STATE_POWER_OFF] =		"power off",
	[FPGA_MGR_STATE_POWER_UP] =		"power up",
	[FPGA_MGR_STATE_RESET] =		"reset",

	/* requesting FPGA image from firmware */
	[FPGA_MGR_STATE_FIRMWARE_REQ] =		"firmware request",
	[FPGA_MGR_STATE_FIRMWARE_REQ_ERR] =	"firmware request error",

	/* Preparing FPGA to receive image */
	[FPGA_MGR_STATE_WRITE_INIT] =		"write init",
	[FPGA_MGR_STATE_WRITE_INIT_ERR] =	"write init error",

	/* Writing image to FPGA */
	[FPGA_MGR_STATE_WRITE] =		"write",
	[FPGA_MGR_STATE_WRITE_ERR] =		"write error",

	/* Finishing configuration after image has been written */
	[FPGA_MGR_STATE_WRITE_COMPLETE] =	"write complete",
	[FPGA_MGR_STATE_WRITE_COMPLETE_ERR] =	"write complete error",

	/* FPGA reports to be in normal operating mode */
	[FPGA_MGR_STATE_OPERATING] =		"operating",
};

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return sprintf(buf, "%s\n", mgr->name);
}

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return sprintf(buf, "%s\n", state_str[mgr->state]);
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	u64 status;
	int len = 0;

	if (!mgr->mops->status)
		return -ENOENT;

	status = mgr->mops->status(mgr);

	if (status & FPGA_MGR_STATUS_OPERATION_ERR)
		len += sprintf(buf + len, "reconfig operation error\n");
	if (status & FPGA_MGR_STATUS_CRC_ERR)
		len += sprintf(buf + len, "reconfig CRC error\n");
	if (status & FPGA_MGR_STATUS_INCOMPATIBLE_IMAGE_ERR)
		len += sprintf(buf + len, "reconfig incompatible image\n");
	if (status & FPGA_MGR_STATUS_IP_PROTOCOL_ERR)
		len += sprintf(buf + len, "reconfig IP protocol error\n");
	if (status & FPGA_MGR_STATUS_FIFO_OVERFLOW_ERR)
		len += sprintf(buf + len, "reconfig fifo overflow error\n");
	if (status & FPGA_MGR_STATUS_SECURITY_ERR)
		len += sprintf(buf + len, "reconfig security error\n");
	if (status & FPGA_MGR_STATUS_DEVICE_INIT_ERR)
		len += sprintf(buf + len,
			       "initialization has not finished\n");
	if (status & FPGA_MGR_STATUS_SIGNAL_ERR)
		len += sprintf(buf + len, "device internal signal error\n");
	if (status & FPGA_MGR_STATUS_HIGH_Z_STATE_ERR)
		len += sprintf(buf + len,
			       "all I/Os are placed in High-Z state\n");
	if (status & FPGA_MGR_STATUS_EOS_ERR)
		len += sprintf(buf + len,
			       "start-up sequence has not finished\n");
	if (status & FPGA_MGR_STATUS_FIRMWARE_REQ_ERR)
		len += sprintf(buf + len, "firmware request error\n");

	return len;
}

static ssize_t firmware_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	unsigned int len;
	char image_name[NAME_MAX];
	int ret;

	/* struct with information about the FPGA image to program. */
	struct fpga_image_info info = {0};

	/* lose terminating \n */
	strcpy(image_name, buf);
	len = strlen(image_name);
	if (image_name[len - 1] == '\n')
		image_name[len - 1] = 0;

	ret = fpga_mgr_firmware_load(mgr, &info, image_name);
	if (ret)
		return ret;

	return count;
}

static ssize_t key_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return snprintf(buf, ENCRYPTED_KEY_LEN + 1, "%s\n", mgr->key);
}

static ssize_t key_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	memcpy(mgr->key, buf, count);

	return count;
}

static ssize_t flags_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return sprintf(buf, "%lx\n", mgr->flags);
}

static ssize_t flags_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	int ret;

	ret = kstrtol(buf, 16, &mgr->flags);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RO(name);
static DEVICE_ATTR_RO(state);
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_WO(firmware);
static DEVICE_ATTR_RW(flags);
static DEVICE_ATTR_RW(key);

static struct attribute *fpga_mgr_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_status.attr,
	&dev_attr_firmware.attr,
	&dev_attr_flags.attr,
	&dev_attr_key.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpga_mgr);

static struct fpga_manager *__fpga_mgr_get(struct device *dev)
{
	struct fpga_manager *mgr;

	mgr = to_fpga_manager(dev);

	if (!try_module_get(dev->parent->driver->owner))
		goto err_dev;

	return mgr;

err_dev:
	put_device(dev);
	return ERR_PTR(-ENODEV);
}

static int fpga_mgr_dev_match(struct device *dev, const void *data)
{
	return dev->parent == data;
}

/**
 * fpga_mgr_get - Given a device, get a reference to a fpga mgr.
 * @dev:	parent device that fpga mgr was registered with
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
struct fpga_manager *fpga_mgr_get(struct device *dev)
{
	struct device *mgr_dev = class_find_device(fpga_mgr_class, NULL, dev,
						   fpga_mgr_dev_match);
	if (!mgr_dev)
		return ERR_PTR(-ENODEV);

	return __fpga_mgr_get(mgr_dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_get);

/**
 * of_fpga_mgr_get - Given a device node, get a reference to a fpga mgr.
 *
 * @node:	device node
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
struct fpga_manager *of_fpga_mgr_get(struct device_node *node)
{
	struct device *dev;

	dev = class_find_device_by_of_node(fpga_mgr_class, node);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return __fpga_mgr_get(dev);
}
EXPORT_SYMBOL_GPL(of_fpga_mgr_get);

/**
 * fpga_mgr_put - release a reference to a fpga manager
 * @mgr:	fpga manager structure
 */
void fpga_mgr_put(struct fpga_manager *mgr)
{
	module_put(mgr->dev.parent->driver->owner);
	put_device(&mgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_put);

#ifdef CONFIG_FPGA_MGR_DEBUG_FS
#include <linux/debugfs.h>

static int fpga_mgr_read(struct seq_file *s, void *data)
{
	struct fpga_manager *mgr = (struct fpga_manager *)s->private;
	int ret = 0;

	if (!mgr->mops->read)
		return -ENOENT;

	if (!mutex_trylock(&mgr->ref_mutex))
		return -EBUSY;

	if (mgr->state != FPGA_MGR_STATE_OPERATING) {
		ret = -EPERM;
		goto err_unlock;
	}

	/* Read the FPGA configuration data from the fabric */
	ret = mgr->mops->read(mgr, s);
	if (ret)
		dev_err(&mgr->dev, "Error while reading configuration data from FPGA\n");

err_unlock:
	mutex_unlock(&mgr->ref_mutex);

	return ret;
}

static int fpga_mgr_read_open(struct inode *inode, struct file *file)
{
	return single_open(file, fpga_mgr_read, inode->i_private);
}

static const struct file_operations fpga_mgr_ops_image = {
	.owner = THIS_MODULE,
	.open = fpga_mgr_read_open,
	.read = seq_read,
};
#endif

static int fpga_dmabuf_fd_get(struct file *file, char __user *argp)
{
	struct fpga_manager *mgr =  (struct fpga_manager *)(file->private_data);
	int buffd;

	if (copy_from_user(&buffd, argp, sizeof(buffd)))
		return -EFAULT;

	mgr->dmabuf = dma_buf_get(buffd);
	if (IS_ERR_OR_NULL(mgr->dmabuf))
		return -EINVAL;

	return 0;
}

static int fpga_device_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct fpga_manager *mgr = container_of(miscdev,
						struct fpga_manager, miscdev);

	file->private_data = mgr;

	return 0;
}

static int fpga_device_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long fpga_device_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	char __user *argp = (char __user *)arg;
	int err;

	switch (cmd) {
	case FPGA_IOCTL_LOAD_DMA_BUFF:
		err = fpga_dmabuf_fd_get(file, argp);
		break;
	default:
		err = -ENOTTY;
	}

	return err;
}

static const struct file_operations fpga_fops = {
	.owner		= THIS_MODULE,
	.open		= fpga_device_open,
	.release	= fpga_device_release,
	.unlocked_ioctl	= fpga_device_ioctl,
	.compat_ioctl	= fpga_device_ioctl,
};

/**
 * fpga_mgr_lock - Lock FPGA manager for exclusive use
 * @mgr:	fpga manager
 *
 * Given a pointer to FPGA Manager (from fpga_mgr_get() or
 * of_fpga_mgr_put()) attempt to get the mutex. The user should call
 * fpga_mgr_lock() and verify that it returns 0 before attempting to
 * program the FPGA.  Likewise, the user should call fpga_mgr_unlock
 * when done programming the FPGA.
 *
 * Return: 0 for success or -EBUSY
 */
int fpga_mgr_lock(struct fpga_manager *mgr)
{
	if (!mutex_trylock(&mgr->ref_mutex)) {
		dev_err(&mgr->dev, "FPGA manager is in use.\n");
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_mgr_lock);

/**
 * fpga_mgr_unlock - Unlock FPGA manager after done programming
 * @mgr:	fpga manager
 */
void fpga_mgr_unlock(struct fpga_manager *mgr)
{
	mutex_unlock(&mgr->ref_mutex);
}
EXPORT_SYMBOL_GPL(fpga_mgr_unlock);

/**
 * fpga_mgr_create - create and initialize a FPGA manager struct
 * @dev:	fpga manager device from pdev
 * @name:	fpga manager name
 * @mops:	pointer to structure of fpga manager ops
 * @priv:	fpga manager private data
 *
 * The caller of this function is responsible for freeing the struct with
 * fpga_mgr_free().  Using devm_fpga_mgr_create() instead is recommended.
 *
 * Return: pointer to struct fpga_manager or NULL
 */
struct fpga_manager *fpga_mgr_create(struct device *dev, const char *name,
				     const struct fpga_manager_ops *mops,
				     void *priv)
{
	struct fpga_manager *mgr;
	int id, ret;

	if (!mops || !mops->write_complete || !mops->state ||
	    !mops->write_init || (!mops->write && !mops->write_sg)) {
		dev_err(dev, "Attempt to register without fpga_manager_ops\n");
		return NULL;
	}

	if (!name || !strlen(name)) {
		dev_err(dev, "Attempt to register with no name!\n");
		return NULL;
	}

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return NULL;

	id = ida_simple_get(&fpga_mgr_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto error_kfree;
	}

	mutex_init(&mgr->ref_mutex);

	mgr->name = name;
	mgr->mops = mops;
	mgr->priv = priv;

	device_initialize(&mgr->dev);
	mgr->dev.class = fpga_mgr_class;
	mgr->dev.groups = mops->groups;
	mgr->dev.parent = dev;
	mgr->dev.of_node = dev->of_node;
	mgr->dev.id = id;

	/* Make device dma capable by inheriting from parent's */
	set_dma_ops(&mgr->dev, get_dma_ops(dev));
	ret = dma_coerce_mask_and_coherent(&mgr->dev, dma_get_mask(dev));
	if (ret) {
		dev_warn(dev,
		"Failed to set DMA mask %llx. Trying to continue... %x\n",
		dma_get_mask(dev), ret);
	}

	ret = dev_set_name(&mgr->dev, "fpga%d", id);
	if (ret)
		goto error_device;

	mgr->miscdev.minor = MISC_DYNAMIC_MINOR;
	mgr->miscdev.name = kobject_name(&mgr->dev.kobj);
	mgr->miscdev.fops = &fpga_fops;
	ret = misc_register(&mgr->miscdev);
	if (ret) {
		pr_err("fpga: failed to register misc device.\n");
		goto error_device;
	}

	return mgr;

error_device:
	ida_simple_remove(&fpga_mgr_ida, id);
error_kfree:
	kfree(mgr);

	return NULL;
}
EXPORT_SYMBOL_GPL(fpga_mgr_create);

/**
 * fpga_mgr_free - free a FPGA manager created with fpga_mgr_create()
 * @mgr:	fpga manager struct
 */
void fpga_mgr_free(struct fpga_manager *mgr)
{
	ida_simple_remove(&fpga_mgr_ida, mgr->dev.id);
	kfree(mgr);
}
EXPORT_SYMBOL_GPL(fpga_mgr_free);

static void devm_fpga_mgr_release(struct device *dev, void *res)
{
	struct fpga_manager *mgr = *(struct fpga_manager **)res;

	fpga_mgr_free(mgr);
}

/**
 * devm_fpga_mgr_create - create and initialize a managed FPGA manager struct
 * @dev:	fpga manager device from pdev
 * @name:	fpga manager name
 * @mops:	pointer to structure of fpga manager ops
 * @priv:	fpga manager private data
 *
 * This function is intended for use in a FPGA manager driver's probe function.
 * After the manager driver creates the manager struct with
 * devm_fpga_mgr_create(), it should register it with fpga_mgr_register().  The
 * manager driver's remove function should call fpga_mgr_unregister().  The
 * manager struct allocated with this function will be freed automatically on
 * driver detach.  This includes the case of a probe function returning error
 * before calling fpga_mgr_register(), the struct will still get cleaned up.
 *
 * Return: pointer to struct fpga_manager or NULL
 */
struct fpga_manager *devm_fpga_mgr_create(struct device *dev, const char *name,
					  const struct fpga_manager_ops *mops,
					  void *priv)
{
	struct fpga_manager **ptr, *mgr;

	ptr = devres_alloc(devm_fpga_mgr_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	mgr = fpga_mgr_create(dev, name, mops, priv);
	if (!mgr) {
		devres_free(ptr);
	} else {
		*ptr = mgr;
		devres_add(dev, ptr);
	}

	return mgr;
}
EXPORT_SYMBOL_GPL(devm_fpga_mgr_create);

/**
 * fpga_mgr_register - register a FPGA manager
 * @mgr: fpga manager struct
 *
 * Return: 0 on success, negative error code otherwise.
 */
int fpga_mgr_register(struct fpga_manager *mgr)
{
	int ret;
#ifdef CONFIG_FPGA_MGR_DEBUG_FS
	struct dentry *d, *parent;
#endif

	/*
	 * Initialize framework state by requesting low level driver read state
	 * from device.  FPGA may be in reset mode or may have been programmed
	 * by bootloader or EEPROM.
	 */
	mgr->state = mgr->mops->state(mgr);

	ret = device_add(&mgr->dev);
	if (ret)
		goto error_device;

#ifdef CONFIG_FPGA_MGR_DEBUG_FS
	mgr->dir = debugfs_create_dir("fpga", NULL);
	if (!mgr->dir)
		goto error_device;

	parent = mgr->dir;
	d = debugfs_create_dir(mgr->dev.kobj.name, parent);
	if (!d) {
		debugfs_remove_recursive(parent);
		goto error_device;
	}

	parent = d;
	d = debugfs_create_file("image", 0644, parent, mgr,
				&fpga_mgr_ops_image);
	if (!d) {
		debugfs_remove_recursive(mgr->dir);
		goto error_device;
	}
#endif
	dev_info(&mgr->dev, "%s registered\n", mgr->name);

	return 0;

error_device:
	ida_simple_remove(&fpga_mgr_ida, mgr->dev.id);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_mgr_register);

/**
 * fpga_mgr_unregister - unregister a FPGA manager
 * @mgr: fpga manager struct
 *
 * This function is intended for use in a FPGA manager driver's remove function.
 */
void fpga_mgr_unregister(struct fpga_manager *mgr)
{
	dev_info(&mgr->dev, "%s %s\n", __func__, mgr->name);

#ifdef CONFIG_FPGA_MGR_DEBUG_FS
	debugfs_remove_recursive(mgr->dir);
#endif

	/*
	 * If the low level driver provides a method for putting fpga into
	 * a desired state upon unregister, do it.
	 */
	if (mgr->mops->fpga_remove)
		mgr->mops->fpga_remove(mgr);

	device_unregister(&mgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_unregister);

static void fpga_mgr_dev_release(struct device *dev)
{
}

static int __init fpga_mgr_class_init(void)
{
	pr_info("FPGA manager framework\n");

	fpga_mgr_class = class_create(THIS_MODULE, "fpga_manager");
	if (IS_ERR(fpga_mgr_class))
		return PTR_ERR(fpga_mgr_class);

	fpga_mgr_class->dev_groups = fpga_mgr_groups;
	fpga_mgr_class->dev_release = fpga_mgr_dev_release;

	return 0;
}

static void __exit fpga_mgr_class_exit(void)
{
	class_destroy(fpga_mgr_class);
	ida_destroy(&fpga_mgr_ida);
}

MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_DESCRIPTION("FPGA manager framework");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_mgr_class_init);
module_exit(fpga_mgr_class_exit);

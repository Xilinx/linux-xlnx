// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

#define AIE_DEV_MAX	(MINORMASK + 1)

static dev_t aie_major;
struct class *aie_class;

static DEFINE_IDA(aie_device_ida);
static DEFINE_IDA(aie_minor_ida);

/**
 * aie_get_partition_fd() - Get AI engine partition file descriptor
 * @apart: AI engine partition
 * @return: file descriptor for AI engine partition for success, or negative
 *	    value for failure.
 *
 * This function gets a file descriptor for the AI engine partition.
 */
static int aie_get_partition_fd(struct aie_partition *apart)
{
	struct file *filep;
	int ret;

	/*
	 * We can't use anon_inode_getfd() because we need to modify
	 * the f_mode flags directly to allow more than just ioctls
	 */
	ret = get_unused_fd_flags(O_CLOEXEC);
	if (ret < 0)
		return ret;

	filep = anon_inode_getfile(dev_name(&apart->dev), &aie_part_fops,
				   apart, O_RDWR);
	if (IS_ERR(filep)) {
		put_unused_fd(ret);
		ret = PTR_ERR(filep);
		return ret;
	}
	filep->f_mode |= (FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	fd_install(ret, filep);

	return ret;
}

/**
 * aie_enquire_partitions() - get AI engine partitions information
 * @adev: AI engine device
 * @query: data struct to store the partition information
 * @return: 0 for success, and negative value for failure.
 */
static int aie_enquire_partitions(struct aie_device *adev,
				  struct aie_partition_query *query)
{
	struct aie_partition *apart;
	u32 partition_cnt, i = 0;

	if (!query->partitions) {
		/*
		 * If partitions information buffer is NULL.
		 * It is to get the number of partitions.
		 */
		query->partition_cnt = 0;
		list_for_each_entry(apart, &adev->partitions, node)
			query->partition_cnt++;
		return 0;
	}

	partition_cnt = query->partition_cnt;
	if (!partition_cnt)
		return 0;

	mutex_lock_interruptible(&adev->mlock);
	list_for_each_entry(apart, &adev->partitions, node) {
		struct aie_range_args part;

		if (i >= partition_cnt)
			break;
		part.partition_id = apart->partition_id;
		/*
		 * TBD: check with PLM that if the partition is programmed
		 * and get the UID of the image which is loaded on the AI
		 * engine partition.
		 */
		part.uid = 0;
		part.range.start.col = apart->range.start.col;
		part.range.start.row = apart->range.start.row;
		part.range.size.col = apart->range.size.col;
		part.range.size.row = apart->range.size.row;
		/* Check if partition is in use */
		part.status = apart->status;
		if (copy_to_user((void __user *)&query->partitions[i], &part,
				 sizeof(part))) {
			mutex_unlock(&adev->mlock);
			return -EFAULT;
		}
		i++;
	}
	mutex_unlock(&adev->mlock);
	query->partition_cnt = i;

	return 0;
}

/**
 * aie_get_partition_from_id() - get AI engine partition from id
 * @adev: AI engine device
 * @partition_id: partition id to check
 * @return: partition pointer if partition exists, otherwise, NULL.
 *
 * This function checks defined partitions with partition id.
 * This function expect the caller to lock mlock of @adev.
 */
struct aie_partition *aie_get_partition_from_id(struct aie_device *adev,
						u32 partition_id)
{
	struct aie_partition *apart;

	list_for_each_entry(apart, &adev->partitions, node) {
		if (apart->partition_id == partition_id)
			return apart;
	}

	return NULL;
}

/**
 * aie_request_partition() - request AI engine partition
 * @adev: AI engine device
 * @req: partition request, includes the requested AI engine information
 *	 such as partition node ID and the UID of the image which is
 *	 loaded on the partition.
 * @return: partition pointer if partition exists, otherwise, NULL.
 *
 * This function finds a defined partition which matches the specified
 * partition id, request it by increasing the refcount, and returns it.
 */
struct aie_partition *aie_request_partition(struct aie_device *adev,
					    struct aie_partition_req *req)
{
	struct aie_partition *apart;

	mutex_lock_interruptible(&adev->mlock);
	apart = aie_get_partition_from_id(adev, req->partition_id);
	if (!apart) {
		dev_err(&adev->dev,
			"request partition %u failed, not exist.\n",
			req->partition_id);
		mutex_unlock(&adev->mlock);
		return ERR_PTR(-EINVAL);
	}
	/*
	 * TODO: It will check image UID too to see if the user matches
	 * what's loaded in the AI engine partition. And check the meta
	 * data to see which resources used by application.
	 */

	mutex_lock_interruptible(&apart->mlock);
	if (apart->status & XAIE_PART_STATUS_INUSE) {
		mutex_unlock(&apart->mlock);
		dev_err(&adev->dev,
			"request partition %u failed, partition in use.\n",
			req->partition_id);
		apart = ERR_PTR(-EBUSY);
	} else {
		/*
		 * TBD:
		 * 1. setup NOC AXI MM config to only generate error events
		 *    for slave error and decode error.
		 * 2. scan to see which tiles have been clock gated.
		 *
		 * This needs to be done before the AI engine partition is
		 * exported for user to access.
		 */
		apart->status = XAIE_PART_STATUS_INUSE;
		mutex_unlock(&apart->mlock);
	}
	mutex_unlock(&adev->mlock);

	return apart;
}

static long xilinx_ai_engine_ioctl(struct file *filp, unsigned int cmd,
				   unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct aie_device *adev = cdev_to_aiedev(inode->i_cdev);
	void __user *argp = (void __user *)arg;
	int ret;

	switch (cmd) {
	case AIE_ENQUIRE_PART_IOCTL:
	{
		struct aie_partition_query query;
		struct aie_partition_query  __user *uquery_ptr = argp;

		if (copy_from_user(&query, uquery_ptr, sizeof(query)))
			return -EFAULT;
		ret = aie_enquire_partitions(adev, &query);
		if (ret < 0)
			return ret;
		if (copy_to_user((void __user *)&uquery_ptr->partition_cnt,
				 &query.partition_cnt,
				 sizeof(query.partition_cnt)))
			return -EFAULT;
		break;
	}
	case AIE_REQUEST_PART_IOCTL:
	{
		struct aie_partition_req req;
		struct aie_partition *apart;

		if (copy_from_user(&req, argp, sizeof(req)))
			return -EFAULT;
		apart = aie_request_partition(adev, &req);
		if (IS_ERR(apart))
			return PTR_ERR(apart);
		ret = aie_get_partition_fd(apart);
		if (ret < 0) {
			dev_err(&apart->dev, "failed to get fd.\n");
			break;
		}
		break;
	}
	default:
		dev_err(&adev->dev, "Invalid ioctl command %u.\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct file_operations aie_device_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= xilinx_ai_engine_ioctl,
};

static void xilinx_ai_engine_release_device(struct device *dev)
{
	struct aie_device *adev = dev_to_aiedev(dev);

	ida_simple_remove(&aie_device_ida, dev->id);
	ida_simple_remove(&aie_minor_ida, MINOR(dev->devt));
	cdev_del(&adev->cdev);
	aie_resource_uninitialize(&adev->cols_res);
}

/**
 * of_xilinx_ai_engine_part_probe() - probes for AI engine partition nodes
 * @adev: AI engine device
 *
 * This function will probe for children AI engine partition nodes and create
 * an AI engine partition instance for each node.
 */
static void of_xilinx_ai_engine_part_probe(struct aie_device *adev)
{
	struct device_node *nc;

	for_each_available_child_of_node(adev->dev.of_node, nc) {
		struct aie_partition *apart;

		if (of_node_test_and_set_flag(nc, OF_POPULATED))
			continue;
		apart = of_aie_part_probe(adev, nc);
		if (IS_ERR(apart)) {
			dev_err(&adev->dev,
				"Failed to probe AI engine part for %pOF\n",
				nc);
			of_node_clear_flag(nc, OF_POPULATED);
		}
	}
}

static int xilinx_ai_engine_probe(struct platform_device *pdev)
{
	struct aie_device *adev;
	struct device *dev;
	int ret;

	adev = devm_kzalloc(&pdev->dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	platform_set_drvdata(pdev, adev);
	INIT_LIST_HEAD(&adev->partitions);
	mutex_init(&adev->mlock);

	adev->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!adev->res) {
		dev_err(&pdev->dev, "No memory resource.\n");
		return -EINVAL;
	}
	adev->base = devm_ioremap_resource(&pdev->dev, adev->res);
	if (IS_ERR(adev->base)) {
		dev_err(&pdev->dev, "no io memory resource.\n");
		return PTR_ERR(adev->base);
	}

	/* For now only AI engine v1 device is supported */
	ret = aiev1_device_init(adev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize device instance.\n");
		return ret;
	}

	dev = &adev->dev;
	device_initialize(dev);
	dev->class = aie_class;
	dev->parent = &pdev->dev;
	dev->of_node = pdev->dev.of_node;

	ret = ida_simple_get(&aie_minor_ida, 0, AIE_DEV_MAX, GFP_KERNEL);
	if (ret < 0)
		goto free_dev;
	dev->devt = MKDEV(MAJOR(aie_major), ret);
	ret = ida_simple_get(&aie_device_ida, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto free_minor_ida;
	dev->id = ret;
	dev_set_name(&adev->dev, "aie%d", dev->id);

	cdev_init(&adev->cdev, &aie_device_fops);
	adev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&adev->cdev, dev->devt, 1);
	if (ret)
		goto free_ida;
	/* We can now rely on the release function for cleanup */
	dev->release = xilinx_ai_engine_release_device;

	ret = device_add(dev);
	if (ret) {
		dev_err(&pdev->dev, "device_add failed: %d\n", ret);
		put_device(dev);
		return ret;
	}

	of_xilinx_ai_engine_part_probe(adev);
	dev_info(&pdev->dev, "Xilinx AI Engine device(cols=%u) probed\n",
		 adev->cols_res.total);
	return 0;

free_ida:
	ida_simple_remove(&aie_device_ida, dev->id);
free_minor_ida:
	ida_simple_remove(&aie_minor_ida, MINOR(dev->devt));
free_dev:
	put_device(dev);

	return ret;
}

static int xilinx_ai_engine_remove(struct platform_device *pdev)
{
	struct aie_device *adev = platform_get_drvdata(pdev);
	struct aie_partition *apart;

	list_for_each_entry(apart, &adev->partitions, node)
		aie_part_remove(apart);

	device_del(&adev->dev);
	put_device(&adev->dev);

	return 0;
}

static const struct of_device_id xilinx_ai_engine_of_match[] = {
	{ .compatible = "xlnx,ai-engine-v1.0", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_ai_engine_of_match);

static struct platform_driver xilinx_ai_engine_driver = {
	.probe			= xilinx_ai_engine_probe,
	.remove			= xilinx_ai_engine_remove,
	.driver			= {
		.name		= "xilinx-ai-engine",
		.of_match_table	= xilinx_ai_engine_of_match,
	},
};

static int __init xilinx_ai_engine_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&aie_major, 0, AIE_DEV_MAX, "aie");
	if (ret < 0) {
		pr_err("aie: failed to allocate aie region\n");
		return ret;
	}

	aie_class = class_create(THIS_MODULE, "aie");
	if (IS_ERR(aie_class)) {
		pr_err("failed to create aie class\n");
		unregister_chrdev_region(aie_major, AIE_DEV_MAX);
		return PTR_ERR(aie_class);
	}

	return 0;
}
postcore_initcall(xilinx_ai_engine_init);

static void __exit xilinx_ai_engine_exit(void)
{
	class_destroy(aie_class);
	unregister_chrdev_region(aie_major, AIE_DEV_MAX);
}
module_exit(xilinx_ai_engine_exit);

module_platform_driver(xilinx_ai_engine_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_LICENSE("GPL v2");

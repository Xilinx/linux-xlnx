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
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xlnx-ai-engine.h>
#include <uapi/linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

#define AIE_DEV_MAX			(MINORMASK + 1)
#define VERSAL_SILICON_REV_MASK		GENMASK(31, 28)

static dev_t aie_major;
struct class *aie_class;

static DEFINE_IDA(aie_device_ida);
static DEFINE_IDA(aie_minor_ida);

/**
 * aie_partition_fd() - returns a file descriptor for the given AI engine
 *			partition device
 * @apart: AI engine partition
 * @return: file descriptor of the AI engine partition for success,
 *	    negative value for failure.
 *
 * This function allocate a file descriptor for the AI engine partition
 * export file.
 */
static int aie_partition_fd(struct aie_partition *apart)
{
	int ret;

	ret = get_unused_fd_flags(O_CLOEXEC);
	if (ret < 0) {
		dev_err(&apart->dev,
			"Failed to get fd for partition %u.\n",
			apart->partition_id);
		return ret;
	}
	fd_install(ret, apart->filep);

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
	int ret;

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

	ret = mutex_lock_interruptible(&adev->mlock);
	if (ret)
		return ret;

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
 * aie_partition_get() - Request the specified AI engine partition
 *
 * @apart: AI engine partition
 * @req: AI engine partition request information which includes image UID.
 *	 flag to indicate if the partition will cleanup or not when releasing
 *	 the partition.
 * @return: 0 for success, and negative value for failure.
 *
 * This function will check if the specified partition can be requested, it
 * will check if the partition has been loaded with an image, if no, as long
 * as it is not in use, the partition request will be granted. If there is
 * image loaded, it will check if the given UID from @req matches the image UID
 * loaded on the partition, if they match, the partition request will be
 * granted. A file will be created for the requested partition.
 */
static int aie_partition_get(struct aie_partition *apart,
			     struct aie_partition_req *req)
{
	struct file *filep;
	int ret;

	(void)req;

	if (apart->status & XAIE_PART_STATUS_INUSE) {
		dev_err(&apart->dev,
			"request partition %u failed, partition in use.\n",
			apart->partition_id);
		return -EBUSY;
	}
	/*
	 * TODO:
	 * 1. It will check image UID too to see if the user matches what's
	 *    loaded in the AI engine partition. And check the meta data to see
	 *    which resources used by application.
	 */

	/* scan to setup the initial clock state for tiles */
	ret = aie_part_scan_clk_state(apart);
	if (ret)
		return ret;

	/* Get a file for the partition */
	filep = anon_inode_getfile(dev_name(&apart->dev), &aie_part_fops,
				   apart, O_RDWR);
	if (IS_ERR(filep)) {
		dev_err(&apart->dev,
			"Failed to request partition %u, failed to get file.\n",
			apart->partition_id);
		return PTR_ERR(filep);
	}

	filep->f_mode |= (FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	apart->filep = filep;

	apart->status = XAIE_PART_STATUS_INUSE;
	apart->cntrflag = req->flag;
	return 0;
}

/**
 * aie_partition_request_from_adev() - request AI engine partition from AI
 *				       engine device
 * @adev: AI engine device
 * @req: partition request, includes the requested AI engine information
 *	 such as partition node ID and the UID of the image which is used
 *	 to describe the partition to request.
 * @return: pointer to the AI engine partition for success, and negative
 *	    value for failure.
 *
 * This function finds a defined partition which matches the specified
 * partition id, and request it.
 */
static struct aie_partition *
aie_partition_request_from_adev(struct aie_device *adev,
				struct aie_partition_req *req)
{
	struct aie_partition *apart;
	int ret;

	ret = mutex_lock_interruptible(&adev->mlock);
	if (ret)
		return ERR_PTR(ret);

	apart = aie_get_partition_from_id(adev, req->partition_id);
	if (!apart) {
		dev_err(&adev->dev,
			"request partition %u failed, not exist.\n",
			req->partition_id);
		mutex_unlock(&adev->mlock);
		return ERR_PTR(-EINVAL);
	}
	mutex_unlock(&adev->mlock);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ERR_PTR(ret);

	ret = aie_partition_get(apart, req);

	mutex_unlock(&apart->mlock);

	if (ret)
		apart = ERR_PTR(ret);
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

		apart = aie_partition_request_from_adev(adev, &req);
		if (IS_ERR(apart))
			return PTR_ERR(apart);

		/* Allocate fd */
		ret = aie_partition_fd(apart);
		if (ret < 0) {
			fput(apart->filep);
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
	u32 idcode, version, pm_reg[2];
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

	/* Initialize AIE device specific instance. */
	ret = aie_device_init(adev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize device instance.\n");
		return ret;
	}

	/*
	 * AI Engine platform management node ID is required for requesting
	 * services from firmware driver.
	 */
	ret = of_property_read_u32_array(pdev->dev.of_node, "power-domains",
					 pm_reg, ARRAY_SIZE(pm_reg));
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read power manangement information\n");
		return ret;
	}
	adev->pm_node_id = pm_reg[1];

	adev->eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(adev->eemi_ops)) {
		dev_err(&adev->dev, "failed to get eemi ops.\n");
		return PTR_ERR(adev->eemi_ops);
	}
	if (!adev->eemi_ops->reset_assert || !adev->eemi_ops->get_chipid ||
	    !adev->eemi_ops->ioctl) {
		dev_err(&adev->dev, "required eemi ops not found.\n");
		return -EINVAL;
	}

	ret = adev->eemi_ops->get_chipid(&idcode, &version);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get chip ID\n");
		return ret;
	}
	adev->version = FIELD_GET(VERSAL_SILICON_REV_MASK, idcode);

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

	INIT_WORK(&adev->backtrack, aie_array_backtrack);

	adev->irq = platform_get_irq_byname(pdev, "interrupt1");
	if (adev->irq < 0)
		goto free_ida;

	ret = devm_request_threaded_irq(dev, adev->irq, NULL, aie_interrupt,
					IRQF_ONESHOT, dev_name(dev), adev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request AIE IRQ.\n");
		goto free_ida;
	}
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

/**
 * aie_partition_dev_match() - check if AI engine partition matches partition ID
 *
 * @dev: pointer to the AI engine partition device
 * @data: partition_id
 * @return: 1 if partition matches, otherwise 0.
 */
static int aie_partition_dev_match(struct device *dev, const void *data)
{
	struct aie_partition *apart;
	u32 partition_id = (u32)(uintptr_t)data;

	if (strncmp(dev_name(dev), "aiepart", strlen("aiepart")))
		return 0;

	apart = dev_to_aiepart(dev);
	if (apart->partition_id == partition_id)
		return 1;
	return 0;
}

/**
 * aie_class_find_partition_from_id() - Find the AI engine partition whose ID
 *					matches.
 * @partition_id: AI engine partition ID
 * @return: pointer to the AI engine partition if partition is found, otherwise
 *	    NULL.
 *
 * This function looks up all the devices of the AI engine class to check if
 * the device is AI engine partition device if if the partition ID matches.
 */
static struct aie_partition *aie_class_find_partition_from_id(u32 partition_id)
{
	struct device *dev;

	dev = class_find_device(aie_class, NULL,
				(void *)(uintptr_t)partition_id,
				aie_partition_dev_match);
	if (!dev)
		return NULL;
	return dev_to_aiepart(dev);
}

/**
 * aie_partition_is_available() - Check if an AI engine partition is available
 * @req: AI engine partition requesting arguments
 * @return: true if the AI engine partition is not in use, otherwise, false
 *
 * This function looks up the AI engine class devices to find the AI engine
 * partition whose partition ID matches the given partition ID in @req. If
 * the partition can be found, if will check if the partition is in use.
 *
 * In case the AI engine release function is called from kernel context, the
 * release() will be scheduled when the AI engine partition reference count is
 * reduced to 0 instead of get called synchronously, and thus, this is a helper
 * function for another kernel module to check if the partitions is released
 * after calling release function from kernel context
 *
 * However, if closing the partition is from user context, it will not return
 * until the release is complete when there is no reference to the AI engine
 * partition file. In this case, user doesn't need to call this function to
 * check if the partition is released.
 */
bool aie_partition_is_available(struct aie_partition_req *req)
{
	struct aie_partition *apart;
	int ret;

	if (!req)
		return false;

	apart = aie_class_find_partition_from_id(req->partition_id);
	if (!apart)
		return false;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return false;

	if (apart->status & XAIE_PART_STATUS_INUSE) {
		mutex_unlock(&apart->mlock);
		return false;
	}

	mutex_unlock(&apart->mlock);
	return true;
}
EXPORT_SYMBOL_GPL(aie_partition_is_available);

/**
 * aie_partition_request() - Request an AI engine partition
 * @req: AI engine partition requesting arguments
 * @return: pointer to the AI engine partition device, error value for failure.
 *
 * This function looks up the AI engine class devices to find the AI engine
 * partition whose partition ID matches the given partition ID in @req. If
 * the partition can be found, it will try to request it. It will get a file
 * for the requested AI engine partition. User can only use the AI engine
 * partition after it is successfully requested.
 */
struct device *aie_partition_request(struct aie_partition_req *req)
{
	struct aie_partition *apart;
	int ret;

	if (!req)
		return ERR_PTR(-EINVAL);

	apart = aie_class_find_partition_from_id(req->partition_id);
	if (!apart)
		return ERR_PTR(-ENODEV);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ERR_PTR(ret);

	ret = aie_partition_get(apart, req);
	mutex_unlock(&apart->mlock);
	if (ret)
		return ERR_PTR(ret);

	if (apart->error_to_report)
		schedule_work(&apart->adev->backtrack);

	return &apart->dev;
}
EXPORT_SYMBOL_GPL(aie_partition_request);

/**
 * aie_partition_get_fd() - get AI engine partition file descriptor
 * @dev: AI engine partition device pointer
 * @return: file descriptor for the AI engine partition for success, and
 *	    negative value for failure.
 *
 * This function allocate a file descriptor for the AI engine requested
 * partition, and increase the reference count to the AI engine partition file.
 */
int aie_partition_get_fd(struct device *dev)
{
	struct aie_partition *apart;
	int ret;

	if (!dev)
		return -EINVAL;

	apart = dev_to_aiepart(dev);

	ret = aie_partition_fd(apart);
	if (ret < 0)
		return ret;

	get_file(apart->filep);

	return ret;
}
EXPORT_SYMBOL_GPL(aie_partition_get_fd);

/**
 * aie_partition_release() - Recrease refcount of the AI engine partition
 * @dev: AI engine partition device
 */
void aie_partition_release(struct device *dev)
{
	struct aie_partition *apart;

	if (WARN_ON(!dev))
		return;

	apart = dev_to_aiepart(dev);
	fput(apart->filep);
}
EXPORT_SYMBOL_GPL(aie_partition_release);

/**
 * aie_partition_reset() - Reset AI engine partition
 * @dev: AI engine partition device
 * @return: 0 for success, negative value for failure
 */
int aie_partition_reset(struct device *dev)
{
	struct aie_partition *apart;

	if (WARN_ON(!dev))
		return -EINVAL;

	apart = dev_to_aiepart(dev);
	return aie_part_reset(apart);
}
EXPORT_SYMBOL_GPL(aie_partition_reset);

/**
 * aie_partition_post_reinit() - Indicate AI engine partition driver the
 *				 partition has been re-initialized.
 * @dev: AI engine partition device
 * @return: 0 for success, negative value for failure
 *
 * This function is called after the AI engine partition is reconfigured with
 * PDI outside the AI engine driver.
 */
int aie_partition_post_reinit(struct device *dev)
{
	struct aie_partition *apart;

	if (WARN_ON(!dev))
		return -EINVAL;

	apart = dev_to_aiepart(dev);
	return aie_part_post_reinit(apart);
}
EXPORT_SYMBOL_GPL(aie_partition_post_reinit);

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

	platform_driver_register(&xilinx_ai_engine_driver);

	return 0;
}
postcore_initcall(xilinx_ai_engine_init);

static void __exit xilinx_ai_engine_exit(void)
{
	platform_driver_unregister(&xilinx_ai_engine_driver);
	class_destroy(aie_class);
	unregister_chrdev_region(aie_major, AIE_DEV_MAX);
}
module_exit(xilinx_ai_engine_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_LICENSE("GPL v2");

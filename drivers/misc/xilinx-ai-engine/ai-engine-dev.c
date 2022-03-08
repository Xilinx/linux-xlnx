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
	struct aie_aperture *aperture;
	struct aie_range_args *query_parts;
	u32 part_cnt, parts_filled;
	int ret;

	if (!query->partitions) {
		part_cnt = 0;

		/*
		 * If partitions information buffer is NULL.
		 * It is to get the number of partitions.
		 */
		ret = mutex_lock_interruptible(&adev->mlock);
		if (ret)
			return ret;

		list_for_each_entry(aperture, &adev->apertures, node) {
			part_cnt += aie_aperture_get_num_parts(aperture);
		}
		mutex_unlock(&adev->mlock);

		query->partition_cnt = part_cnt;

		return 0;
	}

	part_cnt = query->partition_cnt;
	if (!part_cnt)
		return 0;
	parts_filled = 0;
	query_parts = query->partitions;

	ret = mutex_lock_interruptible(&adev->mlock);
	if (ret)
		return ret;

	list_for_each_entry(aperture, &adev->apertures, node) {
		int lparts_filled, num_parts_left;

		lparts_filled = aie_aperture_enquire_parts(aperture,
							   part_cnt,
							   query_parts,
							   &num_parts_left,
							   true);
		if (lparts_filled < 0) {
			dev_err(&adev->dev,
				"failed to enquire partitions.\n");
			mutex_unlock(&adev->mlock);
			return lparts_filled;
		}
		parts_filled += lparts_filled;
		query_parts += lparts_filled;
		/*
		 * input partitions enquires buffers are less than the number
		 * of partitions.
		 * TODO: ioctl arguments can be updated to include how many
		 * number of partitions information not yet filled.
		 */
		if (num_parts_left)
			break;
	}
	mutex_unlock(&adev->mlock);
	query->partition_cnt = parts_filled;

	return 0;
}

/**
 * aie_request_part_from_id() - request AI engine partition from id
 * @adev: AI engine device
 * @partition_id: partition id to check
 * @return: partition pointer for success, and NULL for failure
 *
 * The partition ID contains the start column and number of columns
 * information for the partition.
 * This function expect the caller to lock mlock of @adev.
 */
static struct aie_partition *aie_request_part_from_id(struct aie_device *adev,
						      u32 partition_id)
{
	struct aie_aperture *aperture;

	list_for_each_entry(aperture, &adev->apertures, node) {
		struct aie_partition *apart;

		apart = aie_aperture_request_part_from_id(aperture,
							  partition_id);
		if (apart)
			return apart;
	}

	return ERR_PTR(-EINVAL);
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

	if (apart->status == XAIE_PART_STATUS_INUSE) {
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

	apart->cntrflag = req->flag;

	/* open AI engine partition instance to get it ready for use */
	ret = aie_part_open(apart, (void *)req->meta_data);
	if (ret) {
		dev_err(&apart->dev, "Failed to open partition %u instance.\n",
			apart->partition_id);
		fput(filep);
		return ret;
	}

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

	apart = aie_request_part_from_id(adev, req->partition_id);
	if (IS_ERR(apart)) {
		dev_err(&adev->dev,
			"request partition %u failed, not exist.\n",
			req->partition_id);
		mutex_unlock(&adev->mlock);
		return ERR_PTR(-EINVAL);
	}
	mutex_unlock(&adev->mlock);

	ret = aie_partition_get(apart, req);

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
}

/**
 * of_xilinx_ai_engine_aperture_probe() - probes AI engine aperture nodes
 * @adev: AI engine device
 *
 * This function will probe children AI engine apertures nodes and create
 * an AI engine aperture instance for each node.
 */
void of_xilinx_ai_engine_aperture_probe(struct aie_device *adev)
{
	struct device_node *nc;

	for_each_available_child_of_node(adev->dev.of_node, nc) {
		struct aie_aperture *aperture;
		int ret;

		if (of_node_test_and_set_flag(nc, OF_POPULATED))
			continue;

		ret = mutex_lock_interruptible(&adev->mlock);
		if (ret)
			return;

		aperture = of_aie_aperture_probe(adev, nc);
		if (IS_ERR(aperture)) {
			dev_err(&adev->dev,
				"Failed to probe AI engine aperture for %pOF\n",
				nc);
			/* try to probe the next node */
			continue;
		}
		list_add_tail(&aperture->node, &adev->apertures);

		mutex_unlock(&adev->mlock);
	}
}

/**
 * xilinx_ai_engine_add_dev() - initialize and add AI engine device
 * @adev: AI engine device
 * @pdev: AI engine platform device
 * @return: 0 for success, negative value for failure
 *
 * This function will initialize and add AI engine device to Linux kernel
 * device framework.
 * TODO: This function should be moved back to xilinx_ai_engine_probe()
 * implementation once v1.0 device node support is removed.
 */
int xilinx_ai_engine_add_dev(struct aie_device *adev,
			     struct platform_device *pdev)
{
	struct device *dev;
	int ret;

	dev = &adev->dev;
	device_initialize(dev);
	dev->class = aie_class;
	dev->parent = &pdev->dev;
	dev->of_node = pdev->dev.of_node;

	ret = ida_simple_get(&aie_minor_ida, 0, AIE_DEV_MAX, GFP_KERNEL);
	if (ret < 0)
		return ret;
	dev->devt = MKDEV(MAJOR(aie_major), ret);
	ret = ida_simple_get(&aie_device_ida, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		ida_simple_remove(&aie_minor_ida, MINOR(dev->devt));
		return ret;
	}
	dev->id = ret;
	dev_set_name(&adev->dev, "aie%d", dev->id);

	cdev_init(&adev->cdev, &aie_device_fops);
	adev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&adev->cdev, dev->devt, 1);
	if (ret) {
		ida_simple_remove(&aie_device_ida, dev->id);
		ida_simple_remove(&aie_minor_ida, MINOR(dev->devt));
		return ret;
	}
	/* We can now rely on the release function for cleanup */
	dev->release = xilinx_ai_engine_release_device;

	ret = device_add(dev);
	if (ret) {
		dev_err(&pdev->dev, "device_add failed: %d\n", ret);
		put_device(dev);
		return ret;
	}

	return 0;
}

static int xilinx_ai_engine_probe(struct platform_device *pdev)
{
	struct aie_device *adev;
	u32 idcode, version, pm_reg[2];
	int ret;
	u8 regs_u8[2];
	u8 aie_gen;

	adev = devm_kzalloc(&pdev->dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	platform_set_drvdata(pdev, adev);
	INIT_LIST_HEAD(&adev->apertures);
	mutex_init(&adev->mlock);

	/* check if device node is v1.0 or not */
	ret = of_device_is_compatible(pdev->dev.of_node, "xlnx,ai-engine-v1.0");
	if (ret)
		return xilinx_ai_engine_probe_v1(pdev);

	ret = of_property_read_u8(pdev->dev.of_node, "xlnx,aie-gen", &aie_gen);
	if (ret < 0) {
		dev_warn(&pdev->dev,
			 "no aie dev generation information in device tree\n");
		return ret;
	}

	ret = of_property_read_u8_array(pdev->dev.of_node, "xlnx,shim-rows",
					regs_u8, ARRAY_SIZE(regs_u8));
	if (ret < 0) {
		dev_warn(&pdev->dev,
			 "no SHIM rows information in device tree\n");
		return ret;
	}
	adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].start_row = regs_u8[0];
	adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].num_rows = regs_u8[1];
	adev->ttype_attr[AIE_TILE_TYPE_SHIMNOC].start_row = regs_u8[0];
	adev->ttype_attr[AIE_TILE_TYPE_SHIMNOC].num_rows = regs_u8[1];

	ret = of_property_read_u8_array(pdev->dev.of_node, "xlnx,core-rows",
					regs_u8, ARRAY_SIZE(regs_u8));
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read core rows information\n");
		return ret;
	}
	adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row = regs_u8[0];
	adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows = regs_u8[1];

	ret = of_property_read_u8_array(pdev->dev.of_node, "xlnx,mem-rows",
					regs_u8, ARRAY_SIZE(regs_u8));
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read mem rows information\n");
		return ret;
	}

	adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row = regs_u8[0];
	adev->ttype_attr[AIE_TILE_TYPE_MEMORY].num_rows = regs_u8[1];

	adev->dev_gen = aie_gen;
	if (aie_gen == AIE_DEVICE_GEN_AIE) {
		ret = aie_device_init(adev);
	} else if (aie_gen == AIE_DEVICE_GEN_AIEML) {
		ret = aieml_device_init(adev);
	} else {
		dev_err(&pdev->dev, "Invalid device generation\n");
		return -EINVAL;
	}
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

	ret = zynqmp_pm_get_chipid(&idcode, &version);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get chip ID\n");
		return ret;
	}
	adev->version = FIELD_GET(VERSAL_SILICON_REV_MASK, idcode);

	adev->clk = devm_clk_get(&pdev->dev, NULL);
	if (!adev->clk) {
		dev_err(&pdev->dev, "Failed to get device clock.\n");
		return -EINVAL;
	}

	ret = xilinx_ai_engine_add_dev(adev, pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add ai engine device.\n");
		return ret;
	}

	of_xilinx_ai_engine_aperture_probe(adev);
	dev_info(&pdev->dev, "Xilinx AI Engine device %s probed. Device generation: %u\n",
		 dev_name(&pdev->dev), aie_gen);

	return 0;
}

static int xilinx_ai_engine_remove(struct platform_device *pdev)
{
	struct aie_device *adev = platform_get_drvdata(pdev);
	struct list_head *node, *pos;

	list_for_each_safe(pos, node, &adev->apertures) {
		struct aie_aperture *aperture;
		int ret;

		aperture = list_entry(pos, struct aie_aperture, node);
		ret = aie_aperture_remove(aperture);
		if (ret)
			return ret;
	}

	device_del(&adev->dev);
	put_device(&adev->dev);

	return 0;
}

static const struct of_device_id xilinx_ai_engine_of_match[] = {
	{ .compatible = "xlnx,ai-engine-v2.0", },
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
 * of_ai_engine_class_find() - find AI engine device with device node
 * @np: device node
 * @return: AI engine device pointer if found, NULL if it is not found
 *
 * This function checks every AI engine device of the aie_class, returns the
 * one whose of_node matches the input of node.
 */
struct aie_device *of_ai_engine_class_find(struct device_node *np)
{
	struct device *dev;

	dev = class_find_device(aie_class, NULL, np, device_match_of_node);
	if (!dev)
		return NULL;

	return dev_to_aiedev(dev);
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
	struct device *dev;
	struct class_dev_iter iter;

	if (!req)
		return false;

	class_dev_iter_init(&iter, aie_class, NULL, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		struct aie_aperture *aperture;
		int ret;

		if (strncmp(dev_name(dev), "aieaperture",
			    strlen("aieaperture")))
			continue;

		aperture = dev_get_drvdata(dev);
		ret = aie_aperture_check_part_avail(aperture, req);
		if (ret == XAIE_PART_STATUS_INUSE) {
			class_dev_iter_exit(&iter);
			return false;
		} else if (ret == XAIE_PART_STATUS_IDLE) {
			class_dev_iter_exit(&iter);
			return true;
		}

		continue;
	}
	class_dev_iter_exit(&iter);

	return false;
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
	struct aie_partition *apart = NULL;
	struct device *dev;
	struct class_dev_iter iter;
	int ret;

	if (!req)
		return ERR_PTR(-EINVAL);

	class_dev_iter_init(&iter, aie_class, NULL, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		struct aie_aperture *aperture;
		int ret;

		if (strncmp(dev_name(dev), "aieaperture",
			    strlen("aieaperture")))
			continue;

		aperture = dev_get_drvdata(dev);
		ret = aie_aperture_check_part_avail(aperture, req);
		if (ret == XAIE_PART_STATUS_INVALID) {
			continue;
		} else if (ret == XAIE_PART_STATUS_INUSE) {
			class_dev_iter_exit(&iter);
			dev_err(&aperture->dev,
				"failed to request partition %u: in use.\n",
				req->partition_id);
			return ERR_PTR(-EBUSY);
		}

		class_dev_iter_exit(&iter);

		apart = aie_aperture_request_part_from_id(aperture,
							  req->partition_id);
		if (IS_ERR(apart))
			return ERR_PTR(PTR_ERR(apart));
		break;
	}

	if (!apart) {
		pr_err("failed to request partition %u: invalid partition.\n",
		       req->partition_id);
		return ERR_PTR(-EINVAL);
	}

	ret = aie_partition_get(apart, req);
	if (ret) {
		if (mutex_lock_interruptible(&apart->aperture->mlock))
			return ERR_PTR(ret);

		list_del(&apart->node);
		aie_part_remove(apart);
		mutex_unlock(&apart->aperture->mlock);
	}

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

	ret = aie_overlay_register_notifier();
	if (ret) {
		pr_err("aie: failed to register device tree overlay notifier.\n");
		return ret;
	}

	platform_driver_register(&xilinx_ai_engine_driver);

	return 0;
}
postcore_initcall(xilinx_ai_engine_init);

static void __exit xilinx_ai_engine_exit(void)
{
	aie_overlay_unregister_notifier();
	platform_driver_unregister(&xilinx_ai_engine_driver);
	class_destroy(aie_class);
	unregister_chrdev_region(aie_major, AIE_DEV_MAX);
}
module_exit(xilinx_ai_engine_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_LICENSE("GPL v2");

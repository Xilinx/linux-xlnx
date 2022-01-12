// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine partition driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 */

#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <uapi/linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

/* AI engine SHIM DMA address width is 48bits */
#define XAIE_DMA_BIT_MASK	48U

/**
 * aie_aperture_get_num_parts() - get number of AI engine partitions of aperture
 * @aperture: AI engine aperture
 * @return: number of partitions of this aperture
 *
 * This function returns the number of AI engine partitions of the aperture.
 * It includes the number of partitions in use and the number of available
 * of partitions. If no partitions are in use, the number of available
 * partitions is 1. One available partition is the max contiguous available
 * columns region. E.g. if there is only one partition in use starting from
 * column 10 to 14 in the aperture. The number of all partitions of this
 * aperture is 3. They are column 0 to 9, 10 to 14 and 15+. This function
 * returns 3, for which columns for each partition, and whether they are in
 * use will be returned by another function @aie_aperture_enquire_parts().
 */
unsigned int aie_aperture_get_num_parts(struct aie_aperture *aperture)
{
	struct aie_partition *apart;
	int ret;
	unsigned int rs, re, num_parts = 0;

	ret = mutex_lock_interruptible(&aperture->mlock);
	if (ret)
		return ret;

	list_for_each_entry(apart, &aperture->partitions, node) {
		num_parts++;
	}

	bitmap_for_each_clear_region(aperture->cols_res.bitmap, rs, re, 0,
				     (aperture->range.size.col - 1)) {
		num_parts++;
	}

	mutex_unlock(&aperture->mlock);

	return num_parts;
}

/**
 * aie_aperture_enquire_parts() - get partitions information
 * @aperture: AI engine aperture`
 * @num_queries: number of queries entries to return
 * @queries: return the partitions information
 * @num_parts_left: number of partitions not filled
 * @to_user: indicate it if information is required to user space
 * @return: number of partitions have been filled in if succeeded,
 *	    negative value for error.
 *
 * This function returns each columns information and if the partition is
 * in use for each partition until the queries array is filled up. The
 * @num_parts_left will contains the partitions whose information are not
 * able to put into the queries due to the queries array is full.
 * Internal function, will not validate the queries, and num_parts_left
 * pointers. Caller should not pass invalid values.
 */
int aie_aperture_enquire_parts(struct aie_aperture *aperture,
			       unsigned int num_queries,
			       struct aie_range_args  *queries,
			       int *num_parts_left, bool to_user)
{
	struct aie_partition *apart;
	int ret;
	unsigned int rs, re, num_queries_left;

	*num_parts_left = 0;
	num_queries_left = num_queries;
	ret = mutex_lock_interruptible(&aperture->mlock);
	if (ret)
		return ret;

	list_for_each_entry(apart, &aperture->partitions, node) {
		struct aie_range_args query;

		if (!num_queries_left) {
			*num_parts_left += 1;
			continue;
		}
		query.partition_id = apart->range.start.col <<
				     AIE_PART_ID_START_COL_SHIFT;
		query.partition_id += apart->range.size.col <<
				      AIE_PART_ID_NUM_COLS_SHIFT;
		query.range.start.col = apart->range.start.col;
		query.range.size.col = apart->range.size.col;
		query.range.start.row = apart->range.start.row;
		query.range.size.row = apart->range.size.row;
		query.status = apart->status;

		if (to_user) {
			if (copy_to_user((void __user *)queries, &query,
					 sizeof(query))) {
				mutex_unlock(&aperture->mlock);
				return -EFAULT;
			}
		} else {
			memcpy(queries, &query, sizeof(query));
		}
		queries++;
		num_queries_left--;
	}

	bitmap_for_each_clear_region(aperture->cols_res.bitmap, rs, re, 0,
				     (aperture->range.size.col - 1)) {
		struct aie_range_args query;

		if (!num_queries_left) {
			*num_parts_left += 1;
			continue;
		}
		query.partition_id = (rs & AIE_PART_ID_START_COL_MASK) <<
				     AIE_PART_ID_START_COL_SHIFT;
		query.partition_id += ((re - rs + 1) &
				       AIE_PART_ID_NUM_COLS_MASK) <<
				      AIE_PART_ID_NUM_COLS_SHIFT;
		query.range.start.col = rs;
		query.range.size.col = re - rs + 1;
		query.range.start.row = aperture->range.start.row;
		query.range.size.row = aperture->range.size.row;
		query.status = 0;

		if (to_user) {
			if (copy_to_user((void __user *)queries, &query,
					 sizeof(query))) {
				mutex_unlock(&aperture->mlock);
				return -EFAULT;
			}
		} else {
			memcpy(queries, &query, sizeof(query));
		}
		queries++;
		num_queries_left--;
	}

	mutex_unlock(&aperture->mlock);

	return (num_queries - num_queries_left);
}

/**
 * aie_aperture_request_part_from_id() - request AI engine partition from id
 * @aperture: AI engine aperture
 * @partition_id: partition id to check
 * @return: partition pointer for success, and error pointer for failure
 *
 * The partition ID contains the start column and number of columns
 * information for the partition.
 */
struct aie_partition *
aie_aperture_request_part_from_id(struct aie_aperture *aperture,
				  u32 partition_id)
{
	struct aie_partition *apart = NULL;
	u32 in_partition_id = partition_id;
	u8 start_col, num_cols;
	int ret;

	start_col = aie_part_id_get_start_col(partition_id);
	num_cols = aie_part_id_get_num_cols(partition_id);
	/*
	 * TODO: this is for backward compatibility, once zocl update
	 * to pass the expected partition id format, can remove the
	 * num_cols.
	 */
	if (num_cols == 0) {
		start_col = aperture->range.start.col;
		num_cols = aperture->range.size.col;
		partition_id = ((u32)start_col << AIE_PART_ID_START_COL_SHIFT) |
			       ((u32)num_cols << AIE_PART_ID_NUM_COLS_SHIFT);
	}

	if (start_col < aperture->range.start.col ||
	    num_cols > aperture->range.size.col ||
	    (start_col + num_cols) >
	    (aperture->range.start.col + aperture->range.size.col)) {
		dev_err(&aperture->dev, "invalid partition %u: %u,%u.\n",
			partition_id, start_col, num_cols);
		return ERR_PTR(-EINVAL);
	}

	ret = mutex_lock_interruptible(&aperture->mlock);
	if (ret)
		return ERR_PTR(ret);

	ret = aie_resource_get_region(&aperture->cols_res, start_col, num_cols);
	if (ret != (u32)start_col) {
		dev_err(&aperture->dev, "partition %u already requested.\n",
			in_partition_id);
		mutex_unlock(&aperture->mlock);
		return ERR_PTR(-EINVAL);
	}

	apart = aie_create_partition(aperture, partition_id);
	if (IS_ERR(apart)) {
		dev_err(&aperture->dev, "failed to create partition %u.\n",
			partition_id);
		mutex_unlock(&aperture->mlock);
		return apart;
	}

	list_add_tail(&apart->node, &aperture->partitions);

	mutex_unlock(&aperture->mlock);

	return apart;
}

/**
 * aie_aperture_check_part_avail() - Check an AI engine partition availability
 * @aperture: AI engine aperture
 * @req: AI engine partition requesting arguments
 * @return: if AI engine partition is available, in use or not valid for the
 *	    aperture.
 *
 * This functions checks the specified partition availability in the aperture.
 * This function is internal call, it will not valid the input pointers.
 */
int aie_aperture_check_part_avail(struct aie_aperture *aperture,
				  struct aie_partition_req *req)
{
	unsigned int start_col, end_col, num_cols;

	start_col = aie_part_id_get_start_col(req->partition_id);
	num_cols = aie_part_id_get_num_cols(req->partition_id);
	/*
	 * TODO: this is for backward compatibility, once zocl update
	 * to pass the expected partition id format, can remove the
	 * num_cols.
	 */
	if (num_cols == 0) {
		start_col = aperture->range.start.col;
		num_cols = aperture->range.size.col;
	}

	end_col = start_col + num_cols - 1;

	if (start_col < aperture->range.start.col ||
	    end_col >= aperture->range.start.col + aperture->range.size.col) {
		return XAIE_PART_STATUS_INVALID;
	}

	if (aie_resource_check_region(&aperture->cols_res, start_col,
				      num_cols) < 0)
		return XAIE_PART_STATUS_INUSE;

	return XAIE_PART_STATUS_IDLE;
}

/**
 * aie_aperture_release_device() - release an AI engine aperture instance
 * @dev: AI engine aperture device
 *
 * It will be called by device driver core when no one holds a valid
 * pointer to @dev anymore.
 */
static void aie_aperture_release_device(struct device *dev)
{
	struct aie_aperture *aperture = dev_get_drvdata(dev);

	aie_resource_uninitialize(&aperture->cols_res);
	aie_resource_uninitialize(&aperture->l2_mask);
	kfree(aperture);
}

/**
 * aie_aperture_remove() - destroy AI engine aperture
 * @aperture: AI engine aperture
 * @return: 0 for success, negative value for failure
 *
 * This function will remove AI engine aperture.
 */
int aie_aperture_remove(struct aie_aperture *aperture)
{
	struct list_head *node, *pos;
	int ret;

	ret = mutex_lock_interruptible(&aperture->mlock);
	if (ret)
		return ret;

	list_for_each_safe(pos, node, &aperture->partitions) {
		struct aie_partition *apart;

		apart = list_entry(pos, struct aie_partition, node);
		list_del(&apart->node);
		aie_part_remove(apart);
	}
	mutex_unlock(&aperture->mlock);

	of_node_clear_flag(aperture->dev.of_node, OF_POPULATED);
	device_del(&aperture->dev);
	put_device(&aperture->dev);

	return 0;
}

/**
 * aie_aperture_add_dev() - initialize and add AI engine aperture device
 * @aperture: AI engine aperture
 * @nc: AI engine aperture device node
 * @return: 0 for success, negative value for failure
 *
 * This function will initialize and add AI engine aperture device to Linux
 * kernel device framework.
 * TODO: This function should be moved back to of_aie_aperture_probe()
 * implementation once v1.0 device node support is removed.
 */
int aie_aperture_add_dev(struct aie_aperture *aperture,
			 struct device_node *nc)
{
	struct device *dev = &aperture->dev;

	/* register device for aperture */
	dev = &aperture->dev;
	dev->class = aie_class;
	dev->parent = &aperture->adev->dev;
	dev->of_node = nc;
	dev->driver_data = aperture;
	dev_set_name(dev, "aieaperture_%u_%u", aperture->range.start.col,
		     aperture->range.size.col);
	/* We can now rely on the release function for cleanup */
	dev->release = aie_aperture_release_device;

	return device_register(&aperture->dev);
}

/**
 * of_aie_aperture_probe() - probes AI engine aperture node
 * @adev: AI engine device
 * @nc: aperture device node
 * @return: AI engine aperture pointer for success, error pointer for failure.
 *
 * This function will probe AI engine aperture node and will create an AI
 * engine aperture instance for the node.
 * It requires the caller to lock the @adev before calling this function.
 */
struct aie_aperture *
of_aie_aperture_probe(struct aie_device *adev, struct device_node *nc)
{
	struct aie_aperture *aperture, *laperture;
	struct device *dev;
	struct aie_range *range;
	u32 regs[2];
	int ret;

	aperture = kzalloc(sizeof(*aperture), GFP_KERNEL);
	if (!aperture)
		return ERR_PTR(-ENOMEM);

	aperture->adev = adev;
	INIT_LIST_HEAD(&aperture->partitions);
	mutex_init(&aperture->mlock);

	range = &aperture->range;
	ret = of_property_read_u32_array(nc, "xlnx,columns", regs,
					 ARRAY_SIZE(regs));
	if (ret < 0) {
		dev_err(&adev->dev,
			"probe %pOF failed, no tiles range information.\n",
			nc);
		goto free_aperture;
	}
	range->start.col = regs[0] & aligned_byte_mask(1);
	range->size.col = regs[1] & aligned_byte_mask(1);

	/*
	 * Row information is used to calculate the clock or other resources
	 * bitmaps. It can be moved aie_device later.
	 */
	range->start.row = 0;
	range->size.row = adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].num_rows +
			  adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows;

	ret = of_property_read_u32_index(nc, "xlnx,node-id", 0,
					 &aperture->node_id);
	if (ret < 0) {
		dev_err(&adev->dev,
			"probe %pOF failed, no aperture node id.\n", nc);
		goto free_aperture;
	}

	/* Validate the aperture */
	list_for_each_entry(laperture, &adev->apertures, node) {
		u32 start_col, end_col, check_start_col, check_end_col;

		if (laperture->node_id == aperture->node_id) {
			dev_err(&adev->dev,
				"probe failed, aperture %u exists.\n",
				aperture->node_id);
			ret = -EINVAL;
			goto free_aperture;
		}

		start_col = range->start.col;
		end_col  = start_col + range->size.col - 1;
		check_start_col = laperture->range.start.col;
		check_end_col = check_start_col + laperture->range.size.col - 1;
		if ((start_col >= check_start_col &&
		     start_col <= check_end_col) ||
		    (end_col >= check_start_col &&
		     end_col <= check_end_col)) {
			dev_err(&adev->dev,
				"probe failed, aperture %u overlaps other aperture.\n",
				aperture->node_id);
			ret = -EINVAL;
			goto free_aperture;
		}
	}

	/* register device for aperture */
	ret = aie_aperture_add_dev(aperture, nc);
	if (ret) {
		dev_err(&aperture->dev, "device_add failed: %d\n", ret);
		goto free_aperture;
	}
	dev = &aperture->dev;

	/*
	 * Initialize columns resource map to remember which columns have been
	 * assigned. Used for partition management.
	 */
	ret = aie_resource_initialize(&aperture->cols_res,
				      aperture->range.size.col);
	if (ret) {
		dev_err(dev, "failed to initialize columns resource.\n");
		goto put_aperture_dev;
	}

	ret = of_address_to_resource(nc, 0, &aperture->res);
	if (ret < 0) {
		dev_err(dev, "failed to get address from device node.\n");
		goto put_aperture_dev;
	}
	aperture->base = devm_ioremap_resource(dev, &aperture->res);
	if (!aperture->base) {
		ret = -ENOMEM;
		goto put_aperture_dev;
	}

	/* Get device node DMA setting */
	dev->coherent_dma_mask = DMA_BIT_MASK(XAIE_DMA_BIT_MASK);
	dev->dma_mask = &dev->coherent_dma_mask;
	ret = of_dma_configure(&aperture->dev, nc, true);
	if (ret)
		dev_warn(&aperture->dev, "Failed to configure DMA.\n");

	/* Initialize interrupt */
	ret = of_irq_get_byname(nc, "interrupt1");
	if (ret < 0) {
		dev_warn(dev, "no interrupt in device node.");
	} else {
		aperture->irq = ret;
		INIT_WORK(&aperture->backtrack, aie_aperture_backtrack);
		ret = aie_aperture_create_l2_bitmap(aperture);
		if (ret) {
			dev_err(dev,
				"failed to initialize l2 mask resource.\n");
			goto put_aperture_dev;
		}

		ret = devm_request_threaded_irq(dev, aperture->irq, NULL,
						aie_interrupt, IRQF_ONESHOT,
						dev_name(dev), aperture);
		if (ret) {
			dev_err(dev, "Failed to request AIE IRQ.\n");
			goto put_aperture_dev;
		}
	}

	of_node_get(nc);

	dev_info(dev,
		 "AI engine aperture %s, id %u, cols(%u, %u) rows(%u, %u) is probed successfully.\n",
		 dev_name(dev), aperture->node_id,
		 aperture->range.start.col, aperture->range.size.col,
		 aperture->range.start.row, aperture->range.size.row);

	return aperture;

put_aperture_dev:
	put_device(dev);
	return ERR_PTR(ret);

free_aperture:
	kfree(aperture);
	return ERR_PTR(ret);
}

/*
 * XILINX PS PCIe DMA Engine test module
 *
 * Copyright (C) 2017 Xilinx, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cdev.h>
#include <linux/dma-direction.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/dma/xilinx_ps_pcie_dma.h>

#include "../dmaengine.h"

#define DRV_MODULE_NAME		"ps_pcie_dma_client"

#define DMA_SCRATCH0_REG_OFFSET   (0x50)
#define DMA_SCRATCH1_REG_OFFSET   (0x54)
#define DMA_AXI_INTR_ASSRT_REG_OFFSET   (0x74)

#define DMA_SW_INTR_ASSRT_BIT      BIT(3)

#define DMA_BAR_NUMBER 0

#define CHAR_DRIVER_NAME               "ps_pcie_dmachan"

#define PIO_CHAR_DRIVER_NAME           "ps_pcie_pio"
#define EP_TRANSLATION_CHECK            0xCCCCCCCC

#define PIO_MEMORY_BAR_NUMBER            2

#define XPIO_CLIENT_MAGIC 'P'
#define IOCTL_EP_CHECK_TRANSLATION     _IO(XPIO_CLIENT_MAGIC, 0x01)

#define XPS_PCIE_DMA_CLIENT_MAGIC 'S'

#define IGET_ASYNC_TRANSFERINFO   _IO(XPS_PCIE_DMA_CLIENT_MAGIC, 0x01)
#define ISET_ASYNC_TRANSFERINFO   _IO(XPS_PCIE_DMA_CLIENT_MAGIC, 0x02)

#define DMA_TRANSACTION_SUCCESSFUL 1
#define DMA_TRANSACTION_FAILURE    0

#define MAX_LIST 1024

struct dma_transfer_info {
	char __user *buff_address;
	unsigned int buff_size;
	loff_t    offset;
	enum dma_data_direction direction;
};

struct buff_info {
	bool status;
	unsigned int buff_size;
	char __user *buff_address;
};

struct usrbuff_info {
	struct buff_info buff_list[MAX_LIST];
	unsigned int expected;
};

enum pio_status {
	PIO_SUPPORTED = 0,
	PIO_NOT_SUPPORTED
};

enum dma_transfer_mode {
	MEMORY_MAPPED = 0,
	STREAMING
};

struct dma_deviceproperties {
	u16     pci_vendorid;
	u16     pci_deviceid;
	u16     board_number;
	enum pio_status pio_transfers;
	enum dma_transfer_mode mode;
	enum dma_data_direction direction[MAX_ALLOWED_CHANNELS_IN_HW];
};

struct xlnx_completed_info {
	struct list_head clist;
	struct buff_info buffer;
};

struct xlnx_ps_pcie_dma_client_channel {
	struct device *dev;
	struct dma_chan *chan;
	struct ps_pcie_dma_channel_match match;
	enum dma_data_direction direction;
	enum dma_transfer_mode mode;
	struct xlnx_completed_info completed;
	spinlock_t channel_lock; /* Lock to serialize transfers on channel */
};

struct xlnx_ps_pcie_dma_client_device {
	struct dma_deviceproperties *properties;

	struct xlnx_ps_pcie_dma_client_channel
		pcie_dma_chan[MAX_ALLOWED_CHANNELS_IN_HW];

	dev_t char_device;
	struct cdev xps_pcie_chardev;
	struct device *chardev[MAX_ALLOWED_CHANNELS_IN_HW];

	dev_t pio_char_device;
	struct cdev xpio_char_dev;
	struct device *xpio_char_device;
	struct mutex  pio_chardev_mutex; /* Exclusive access to ioctl */
	struct completion trans_cmpltn;
	u32 pio_translation_size;

	struct list_head dev_node;
};

struct xlnx_ps_pcie_dma_asynchronous_transaction {
	dma_cookie_t cookie;
	struct page **cache_pages;
	unsigned int num_pages;
	struct sg_table *sg;
	struct xlnx_ps_pcie_dma_client_channel *chan;
	struct xlnx_completed_info *buffer_info;
	struct dma_async_tx_descriptor **txd;
};

static struct class *g_ps_pcie_dma_client_class; /* global device class */
static struct list_head g_ps_pcie_dma_client_list;

/*
 * Keep adding to this list to interact with multiple DMA devices
 */
static struct dma_deviceproperties g_dma_deviceproperties_list[] = {
		{
			.pci_vendorid = PCI_VENDOR_ID_XILINX,
			.pci_deviceid = ZYNQMP_DMA_DEVID,
			.board_number = 0,
			.pio_transfers = PIO_SUPPORTED,
			.mode          = MEMORY_MAPPED,
			/* Make sure the channel direction is same
			 * as what is configured in DMA device
			 */
			.direction = {DMA_TO_DEVICE, DMA_FROM_DEVICE,
					DMA_TO_DEVICE, DMA_FROM_DEVICE}
		}
};

/**
 * ps_pcie_dma_sync_transfer_cbk - Callback handler for Synchronous transfers.
 * Handles both S2C and C2S transfer call backs.
 * Indicates to blocked applications that DMA transfers are complete
 *
 * @data: Callback parameter
 *
 * Return: void
 */
static void ps_pcie_dma_sync_transfer_cbk(void *data)
{
	struct completion *compl = (struct completion *)data;

	if (compl)
		complete(compl);
}

/**
 * initiate_sync_transfer - Programs both Source Q
 * and Destination Q of channel after setting up sg lists and transaction
 * specific data. This functions waits until transaction completion is notified
 *
 * @channel: Pointer to the PS PCIe DMA channel structure
 * @buffer: User land virtual address containing data to be sent or received
 * @length: Length of user land buffer
 * @f_offset: AXI domain address to which data pointed by user buffer has to
 *	      be sent/received from
 * @direction: Transfer of data direction
 *
 * Return: 0 on success and non zero value for failure
 */
static ssize_t initiate_sync_transfer(
			struct xlnx_ps_pcie_dma_client_channel *channel,
			const char __user *buffer, size_t length,
			loff_t *f_offset, enum dma_data_direction direction)
{
	int offset;
	unsigned int alloc_pages;
	unsigned long first, last, nents = 1;
	struct page **cache_pages;
	struct dma_chan *chan = NULL;
	struct dma_device *device;
	struct dma_async_tx_descriptor **txd = NULL;
	dma_cookie_t cookie = 0;
	enum dma_ctrl_flags flags = 0;
	int err;
	struct sg_table *sg;
	enum dma_transfer_direction d_direction;
	int i;
	struct completion *cmpl_ptr;
	enum dma_status status;
	struct scatterlist *selem;
	size_t elem_len = 0;

	chan = channel->chan;
	device = chan->device;

	offset = offset_in_page(buffer);
	first = ((unsigned long)buffer & PAGE_MASK) >> PAGE_SHIFT;
	last = (((unsigned long)buffer + length - 1) & PAGE_MASK) >>
		PAGE_SHIFT;
	alloc_pages = (last - first) + 1;

	cache_pages = devm_kzalloc(channel->dev,
				   (alloc_pages * (sizeof(struct page *))),
				   GFP_ATOMIC);
	if (!cache_pages) {
		dev_err(channel->dev,
			"Unable to allocate memory for page table holder\n");
		err = PTR_ERR(cache_pages);
		goto err_out_cachepages_alloc;
	}

	err = get_user_pages_fast((unsigned long)buffer, alloc_pages,
				  !(direction), cache_pages);
	if (err <= 0) {
		dev_err(channel->dev, "Unable to pin user pages\n");
		err = PTR_ERR(cache_pages);
		goto err_out_pin_pages;
	} else if (err < alloc_pages) {
		dev_err(channel->dev, "Only pinned few user pages %d\n", err);
		err = PTR_ERR(cache_pages);
		for (i = 0; i < err; i++)
			put_page(cache_pages[i]);
		goto err_out_pin_pages;
	}

	sg = devm_kzalloc(channel->dev, sizeof(struct sg_table), GFP_ATOMIC);
	if (!sg) {
		err = PTR_ERR(sg);
		goto err_out_alloc_sg_table;
	}

	err = sg_alloc_table_from_pages(sg, cache_pages, alloc_pages, offset,
					length, GFP_ATOMIC);
	if (err < 0) {
		dev_err(channel->dev, "Unable to create sg table\n");
		goto err_out_sg_to_sgl;
	}

	err = dma_map_sg(channel->dev, sg->sgl, sg->nents, direction);
	if (err == 0) {
		dev_err(channel->dev, "Unable to map buffer to sg table\n");
		err = PTR_ERR(sg);
		goto err_out_dma_map_sg;
	}

	cmpl_ptr = devm_kzalloc(channel->dev, sizeof(struct completion),
				GFP_ATOMIC);
	if (!cmpl_ptr) {
		err = PTR_ERR(cmpl_ptr);
		goto err_out_cmpl_ptr;
	}

	init_completion(cmpl_ptr);

	if (channel->mode == MEMORY_MAPPED)
		nents = sg->nents;

	txd = devm_kzalloc(channel->dev, sizeof(*txd)
					* nents, GFP_ATOMIC);
	if (!txd) {
		err = PTR_ERR(txd);
		goto err_out_cmpl_ptr;
	}

	if (channel->mode == MEMORY_MAPPED) {
		for (i = 0, selem = (sg->sgl); i < sg->nents; i++,
		     selem = sg_next(selem)) {
			if ((i + 1) == sg->nents)
				flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;

			if (direction == DMA_TO_DEVICE) {
				txd[i] = device->device_prep_dma_memcpy(chan,
					(dma_addr_t)(*f_offset) + elem_len,
					selem->dma_address, selem->length,
					flags);
			} else {
				txd[i] = device->device_prep_dma_memcpy(chan,
					selem->dma_address,
					(dma_addr_t)(*f_offset) + elem_len,
					selem->length, flags);
			}

			elem_len += selem->length;

			if (!txd[i]) {
				err = PTR_ERR(txd[i]);
				goto err_out_no_prep_sg_async_desc;
			}
		}
	} else {
		if (direction == DMA_TO_DEVICE)
			d_direction = DMA_MEM_TO_DEV;
		else
			d_direction = DMA_DEV_TO_MEM;

		flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

		txd[0] = device->device_prep_slave_sg(chan, sg->sgl, sg->nents,
						   d_direction, flags, NULL);
		if (!txd[0]) {
			err = PTR_ERR(txd[0]);
			goto err_out_no_slave_sg_async_descriptor;
		}
	}

	if (channel->mode == MEMORY_MAPPED) {
		for (i = 0; i < sg->nents; i++) {
			if ((i + 1) == sg->nents) {
				txd[i]->callback =
					ps_pcie_dma_sync_transfer_cbk;
				txd[i]->callback_param = cmpl_ptr;
			}

			cookie = txd[i]->tx_submit(txd[i]);
			if (dma_submit_error(cookie)) {
				err = (int)cookie;
				dev_err(channel->dev,
					"Unable to submit transaction\n");
				goto free_transaction;
			}
		}
	} else {
		txd[0]->callback = ps_pcie_dma_sync_transfer_cbk;
		txd[0]->callback_param = cmpl_ptr;

		cookie = txd[0]->tx_submit(txd[0]);
		if (dma_submit_error(cookie)) {
			err = (int)cookie;
			dev_err(channel->dev,
				"Unable to submit transaction\n");
			goto free_transaction;
		}
	}

	dma_async_issue_pending(chan);

	wait_for_completion_killable(cmpl_ptr);

	status = dmaengine_tx_status(chan, cookie, NULL);
	if (status == DMA_COMPLETE)
		err = length;
	else
		err = -1;

	dma_unmap_sg(channel->dev, sg->sgl, sg->nents, direction);
	devm_kfree(channel->dev, cmpl_ptr);
	devm_kfree(channel->dev, txd);
	sg_free_table(sg);
	devm_kfree(channel->dev, sg);
	for (i = 0; i < alloc_pages; i++)
		put_page(cache_pages[i]);
	devm_kfree(channel->dev, cache_pages);

	return (ssize_t)err;

free_transaction:
err_out_no_prep_sg_async_desc:
err_out_no_slave_sg_async_descriptor:
	devm_kfree(channel->dev, cmpl_ptr);
	devm_kfree(channel->dev, txd);
err_out_cmpl_ptr:
	dma_unmap_sg(channel->dev, sg->sgl, sg->nents, direction);
err_out_dma_map_sg:
	sg_free_table(sg);
err_out_sg_to_sgl:
	devm_kfree(channel->dev, sg);
err_out_alloc_sg_table:
	for (i = 0; i < alloc_pages; i++)
		put_page(cache_pages[i]);
err_out_pin_pages:
	devm_kfree(channel->dev, cache_pages);
err_out_cachepages_alloc:

	return (ssize_t)err;
}

static ssize_t
ps_pcie_dma_read(struct file *file,
		 char __user *buffer,
		 size_t length,
		 loff_t *f_offset)
{
	struct xlnx_ps_pcie_dma_client_channel *chan;
	ssize_t ret;

	chan = file->private_data;

	if (chan->direction != DMA_FROM_DEVICE) {
		dev_err(chan->dev, "Invalid data direction for channel\n");
		ret = -EINVAL;
		goto c2s_err_direction;
	}

	ret = initiate_sync_transfer(chan, buffer, length, f_offset,
				     DMA_FROM_DEVICE);

	if (ret != length)
		dev_dbg(chan->dev, "Read synchronous transfer unsuccessful\n");

c2s_err_direction:
	return ret;
}

static ssize_t
ps_pcie_dma_write(struct file *file,
		  const char __user *buffer,
		  size_t length,
		  loff_t *f_offset)
{
	struct xlnx_ps_pcie_dma_client_channel *chan;
	ssize_t ret;

	chan =   file->private_data;

	if (chan->direction != DMA_TO_DEVICE) {
		dev_err(chan->dev,
			"Invalid data direction for channel\n");
		ret = -EINVAL;
		goto s2c_err_direction;
	}

	ret = initiate_sync_transfer(chan, buffer, length, f_offset,
				     DMA_TO_DEVICE);

	if (ret != length)
		dev_dbg(chan->dev, "Write synchronous transfer unsuccessful\n");

s2c_err_direction:
	return ret;
}

static int ps_pcie_dma_open(struct inode *in, struct file *file)
{
	struct xlnx_ps_pcie_dma_client_device *xdev;
	int minor_num = iminor(in);

	xdev = container_of(in->i_cdev,
			    struct xlnx_ps_pcie_dma_client_device,
			    xps_pcie_chardev);

	file->private_data = &xdev->pcie_dma_chan[minor_num];

	return 0;
}

static int ps_pcie_dma_release(struct inode *in, struct file *filp)
{
	return 0;
}

static int update_completed_info(struct xlnx_ps_pcie_dma_client_channel *chan,
				 struct usrbuff_info *usr_buff)
{
	int retval = 0;
	unsigned int expected, count = 0;
	struct xlnx_completed_info *entry;
	struct xlnx_completed_info *next;

	if (list_empty(&chan->completed.clist))
		goto update_expected;

	if (copy_from_user((void *)&expected,
			   (void __user *)&usr_buff->expected,
			   sizeof(unsigned int)) != 0) {
		pr_err("Expected count copy failure\n");
		retval = -ENXIO;
		return retval;
	}

	if (expected > MAX_LIST) {
		retval = -ENXIO;
		return retval;
	}

	list_for_each_entry_safe(entry, next, &chan->completed.clist, clist) {
		if (copy_to_user((void __user *)(usr_buff->buff_list + count),
				 (void *)&entry->buffer,
				 sizeof(struct buff_info)) != 0) {
			pr_err("update user completed count copy failed\n");
			retval = -ENXIO;
			break;
		}
		count++;
		spin_lock(&chan->channel_lock);
		list_del(&entry->clist);
		spin_unlock(&chan->channel_lock);
		devm_kfree(chan->dev, entry);
		if (count == expected)
			break;
	}

update_expected:
	if (copy_to_user((void __user *)&usr_buff->expected, (void *)&count,
			 (sizeof(unsigned int))) != 0) {
		pr_err("update user expected count copy failure\n");
		retval = -ENXIO;
	}

	return retval;
}

/**
 * ps_pcie_dma_async_transfer_cbk - Callback handler for Asynchronous transfers.
 * Handles both S2C and C2S transfer call backs. Stores transaction information
 * in a list for a user application to poll for this information
 *
 * @data: Callback parameter
 *
 * Return: void
 */
static void ps_pcie_dma_async_transfer_cbk(void *data)
{
	struct xlnx_ps_pcie_dma_asynchronous_transaction *trans =
		(struct xlnx_ps_pcie_dma_asynchronous_transaction *)data;
	enum dma_status status;
	struct dma_tx_state state;
	unsigned int i;

	dma_unmap_sg(trans->chan->dev, trans->sg->sgl, trans->sg->nents,
		     trans->chan->direction);
	sg_free_table(trans->sg);
	devm_kfree(trans->chan->dev, trans->sg);
	devm_kfree(trans->chan->dev, trans->txd);
	for (i = 0; i < trans->num_pages; i++)
		put_page(trans->cache_pages[i]);
	devm_kfree(trans->chan->dev, trans->cache_pages);

	status = dmaengine_tx_status(trans->chan->chan, trans->cookie, &state);

	if (status == DMA_COMPLETE)
		trans->buffer_info->buffer.status = DMA_TRANSACTION_SUCCESSFUL;
	else
		trans->buffer_info->buffer.status = DMA_TRANSACTION_SUCCESSFUL;

	spin_lock(&trans->chan->channel_lock);
	list_add_tail(&trans->buffer_info->clist,
		      &trans->chan->completed.clist);
	spin_unlock(&trans->chan->channel_lock);
	devm_kfree(trans->chan->dev, trans);
}

/**
 * initiate_async_transfer - Programs both Source Q
 * and Destination Q of channel after setting up sg lists and transaction
 * specific data. This functions returns after setting up transfer
 *
 * @channel: Pointer to the PS PCIe DMA channel structure
 * @buffer: User land virtual address containing data to be sent or received
 * @length: Length of user land buffer
 * @f_offset: AXI domain address to which data pointed by user buffer has to
 *	      be sent/received from
 * @direction: Transfer of data direction
 *
 * Return: 0 on success and non zero value for failure
 */
static int initiate_async_transfer(
		struct xlnx_ps_pcie_dma_client_channel *channel,
		char __user *buffer, size_t length, loff_t *f_offset,
		enum dma_data_direction direction)
{
	int offset;
	unsigned int alloc_pages;
	unsigned long first, last, nents = 1;
	struct page **cache_pages;
	struct dma_chan *chan = NULL;
	struct dma_device *device;
	struct dma_async_tx_descriptor **txd = NULL;
	dma_cookie_t cookie;
	enum dma_ctrl_flags flags = 0;
	struct xlnx_ps_pcie_dma_asynchronous_transaction *trans;
	int err;
	struct sg_table *sg;
	enum dma_transfer_direction d_direction;
	int i;
	struct scatterlist *selem;
	size_t elem_len = 0;

	chan = channel->chan;
	device = chan->device;

	offset = offset_in_page(buffer);
	first = ((unsigned long)buffer & PAGE_MASK) >> PAGE_SHIFT;
	last = (((unsigned long)buffer + length - 1) & PAGE_MASK) >>
		PAGE_SHIFT;
	alloc_pages = (last - first) + 1;

	cache_pages = devm_kzalloc(channel->dev,
				   (alloc_pages * (sizeof(struct page *))),
				   GFP_ATOMIC);
	if (!cache_pages) {
		err = PTR_ERR(cache_pages);
		goto err_out_cachepages_alloc;
	}

	err = get_user_pages_fast((unsigned long)buffer, alloc_pages,
				  !(direction), cache_pages);
	if (err <= 0) {
		dev_err(channel->dev, "Unable to pin user pages\n");
		err = PTR_ERR(cache_pages);
		goto err_out_pin_pages;
	} else if (err < alloc_pages) {
		dev_err(channel->dev, "Only pinned few user pages %d\n", err);
		err = PTR_ERR(cache_pages);
		for (i = 0; i < err; i++)
			put_page(cache_pages[i]);
		goto err_out_pin_pages;
	}

	sg = devm_kzalloc(channel->dev, sizeof(struct sg_table), GFP_ATOMIC);
	if (!sg) {
		err = PTR_ERR(sg);
		goto err_out_alloc_sg_table;
	}

	err = sg_alloc_table_from_pages(sg, cache_pages, alloc_pages, offset,
					length, GFP_ATOMIC);
	if (err < 0) {
		dev_err(channel->dev, "Unable to create sg table\n");
		goto err_out_sg_to_sgl;
	}

	err = dma_map_sg(channel->dev, sg->sgl, sg->nents, direction);
	if (err == 0) {
		dev_err(channel->dev,
			"Unable to map user buffer to sg table\n");
		err = PTR_ERR(sg);
		goto err_out_dma_map_sg;
	}

	trans = devm_kzalloc(channel->dev, sizeof(*trans), GFP_ATOMIC);
	if (!trans) {
		err = PTR_ERR(trans);
		goto err_out_trans_ptr;
	}

	trans->buffer_info = devm_kzalloc(channel->dev,
					  sizeof(struct xlnx_completed_info),
					  GFP_ATOMIC);

	if (!trans->buffer_info) {
		err = PTR_ERR(trans->buffer_info);
		goto err_out_no_completion_info;
	}

	if (channel->mode == MEMORY_MAPPED)
		nents = sg->nents;

	txd = devm_kzalloc(channel->dev,
			   sizeof(*txd) * nents, GFP_ATOMIC);
	if (!txd) {
		err = PTR_ERR(txd);
		goto err_out_no_completion_info;
	}

	trans->txd = txd;

	if (channel->mode == MEMORY_MAPPED) {
		for (i = 0, selem = (sg->sgl); i < sg->nents; i++,
		     selem = sg_next(selem)) {
			if ((i + 1) == sg->nents)
				flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;

			if (direction == DMA_TO_DEVICE) {
				txd[i] = device->device_prep_dma_memcpy(chan,
					(dma_addr_t)(*f_offset) + elem_len,
					selem->dma_address, selem->length,
					flags);
			} else {
				txd[i] = device->device_prep_dma_memcpy(chan,
					selem->dma_address,
					(dma_addr_t)(*f_offset) + elem_len,
					selem->length, flags);
			}

			elem_len += selem->length;

			if (!txd[i]) {
				err = PTR_ERR(txd[i]);
				goto err_out_no_prep_sg_async_desc;
			}
		}
	} else {
		if (direction == DMA_TO_DEVICE)
			d_direction = DMA_MEM_TO_DEV;
		else
			d_direction = DMA_DEV_TO_MEM;

		flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

		txd[0] = device->device_prep_slave_sg(chan, sg->sgl, sg->nents,
						   d_direction, flags, NULL);
		if (!txd[0]) {
			err = PTR_ERR(txd[0]);
			goto err_out_no_slave_sg_async_descriptor;
		}
	}

	trans->buffer_info->buffer.buff_address = buffer;
	trans->buffer_info->buffer.buff_size = length;
	trans->cache_pages = cache_pages;
	trans->num_pages   = alloc_pages;
	trans->chan = channel;
	trans->sg = sg;

	if (channel->mode == MEMORY_MAPPED) {
		for (i = 0; i < sg->nents; i++) {
			cookie = txd[i]->tx_submit(txd[i]);
			if (dma_submit_error(cookie)) {
				err = (int)cookie;
				dev_err(channel->dev,
					"Unable to submit transaction\n");
				goto free_transaction;
			}

			if ((i + 1) == sg->nents) {
				txd[i]->callback =
					ps_pcie_dma_async_transfer_cbk;
				txd[i]->callback_param = trans;
				trans->cookie = cookie;
			}
		}

	} else {
		txd[0]->callback = ps_pcie_dma_async_transfer_cbk;
		txd[0]->callback_param = trans;

		cookie = txd[0]->tx_submit(txd[0]);
		if (dma_submit_error(cookie)) {
			err = (int)cookie;
			dev_err(channel->dev,
				"Unable to submit transaction\n");
			goto free_transaction;
		}

		trans->cookie = cookie;
	}

	dma_async_issue_pending(chan);

	return length;

free_transaction:
err_out_no_prep_sg_async_desc:
err_out_no_slave_sg_async_descriptor:
	devm_kfree(channel->dev, trans->buffer_info);
	devm_kfree(channel->dev, txd);
err_out_no_completion_info:
	devm_kfree(channel->dev, trans);
err_out_trans_ptr:
	dma_unmap_sg(channel->dev, sg->sgl, sg->nents, direction);
err_out_dma_map_sg:
	sg_free_table(sg);
err_out_sg_to_sgl:
	devm_kfree(channel->dev, sg);
err_out_alloc_sg_table:
	for (i = 0; i < alloc_pages; i++)
		put_page(cache_pages[i]);
err_out_pin_pages:
	devm_kfree(channel->dev, cache_pages);
err_out_cachepages_alloc:

	return err;
}

static long ps_pcie_dma_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	int retval = 0;
	struct xlnx_ps_pcie_dma_client_channel *chan;
	struct dma_transfer_info transfer_info;

	if (_IOC_TYPE(cmd) != XPS_PCIE_DMA_CLIENT_MAGIC)
		return -ENOTTY;

	chan = filp->private_data;

	switch (cmd) {
	case ISET_ASYNC_TRANSFERINFO:
		if (copy_from_user((void *)&transfer_info,
				   (void __user *)arg,
				   sizeof(struct dma_transfer_info)) != 0) {
			pr_err("Copy from user asynchronous params\n");
			retval = -ENXIO;
			return retval;
		}
		if (transfer_info.direction != chan->direction) {
			retval = -EINVAL;
			return retval;
		}
		retval = initiate_async_transfer(chan,
						 transfer_info.buff_address,
						 transfer_info.buff_size,
						 &transfer_info.offset,
						 transfer_info.direction);
		break;
	case IGET_ASYNC_TRANSFERINFO:
		retval = update_completed_info(chan,
					       (struct usrbuff_info *)arg);
		break;
	default:
		pr_err("Unsupported ioctl command received\n");
		retval = -1;
	}

	return (long)retval;
}

static const struct file_operations ps_pcie_dma_comm_fops = {
	.owner		= THIS_MODULE,
	.read		= ps_pcie_dma_read,
	.write		= ps_pcie_dma_write,
	.unlocked_ioctl = ps_pcie_dma_ioctl,
	.open		= ps_pcie_dma_open,
	.release	= ps_pcie_dma_release,
};

static void pio_sw_intr_cbk(void *data)
{
	struct completion *compl = (struct completion *)data;

	if (compl)
		complete(compl);
}

static long pio_ioctl(struct file *filp, unsigned int cmd,
		      unsigned long arg)
{
	char *bar_memory = NULL;
	u32 translation_size = 0;
	long err = 0;
	struct dma_async_tx_descriptor *intr_txd = NULL;
	dma_cookie_t cookie;
	struct dma_chan *chan = NULL;
	struct dma_device *device;
	enum dma_ctrl_flags flags;
	struct xlnx_ps_pcie_dma_client_device *xdev;
	struct ps_pcie_dma_channel_match *xlnx_match;
	struct BAR_PARAMS *barinfo;

	xdev = filp->private_data;
	chan = xdev->pcie_dma_chan[0].chan;
	device = chan->device;
	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	xlnx_match =
		(struct ps_pcie_dma_channel_match *)chan->private;

	barinfo = ((struct BAR_PARAMS *)(xlnx_match->bar_params) +
			DMA_BAR_NUMBER);
	bar_memory = (__force char *)barinfo->BAR_VIRT_ADDR;

	xdev = filp->private_data;

	switch (cmd) {
	case IOCTL_EP_CHECK_TRANSLATION:

		mutex_lock(&xdev->pio_chardev_mutex);
		reinit_completion(&xdev->trans_cmpltn);

		intr_txd = device->device_prep_dma_interrupt(chan, flags);
		if (!intr_txd) {
			err = -EAGAIN;
			mutex_unlock(&xdev->pio_chardev_mutex);
			return err;
		}

		intr_txd->callback       = pio_sw_intr_cbk;
		intr_txd->callback_param = &xdev->trans_cmpltn;

		cookie = intr_txd->tx_submit(intr_txd);
		if (dma_submit_error(cookie)) {
			err =  cookie;
			pr_err("Unable to submit interrupt transaction\n");
			mutex_unlock(&xdev->pio_chardev_mutex);
			return err;
		}

		dma_async_issue_pending(chan);

		iowrite32(EP_TRANSLATION_CHECK, (void __iomem *)(bar_memory +
						DMA_SCRATCH0_REG_OFFSET));
		iowrite32(DMA_SW_INTR_ASSRT_BIT, (void __iomem *)(bar_memory +
						DMA_AXI_INTR_ASSRT_REG_OFFSET));

		wait_for_completion_interruptible(&xdev->trans_cmpltn);
		translation_size = ioread32((void __iomem *)bar_memory +
					    DMA_SCRATCH1_REG_OFFSET);
		if (translation_size > 0)
			xdev->pio_translation_size = translation_size;
		else
			err = -EAGAIN;
		iowrite32(0, (void __iomem *)(bar_memory +
					    DMA_SCRATCH1_REG_OFFSET));
		mutex_unlock(&xdev->pio_chardev_mutex);
		break;

	default:
		err = -EINVAL;
	}
	return err;
}

static ssize_t
pio_read(struct file *file, char __user *buffer, size_t length,
	 loff_t *f_offset)
{
	char *bar_memory = NULL;
	struct xlnx_ps_pcie_dma_client_device *xdev;
	struct ps_pcie_dma_channel_match *xlnx_match;
	ssize_t num_bytes = 0;
	struct BAR_PARAMS *barinfo;

	xdev = file->private_data;
	xlnx_match = (struct ps_pcie_dma_channel_match *)
				xdev->pcie_dma_chan[0].chan->private;

	barinfo = ((struct BAR_PARAMS *)(xlnx_match->bar_params) +
			PIO_MEMORY_BAR_NUMBER);
	bar_memory = (__force char *)barinfo->BAR_VIRT_ADDR;

	if (length > xdev->pio_translation_size) {
		pr_err("Error! Invalid buffer length supplied at PIO read\n");
		num_bytes = -1;
		return num_bytes;
	}

	if ((length + *f_offset)
			> xdev->pio_translation_size) {
		pr_err("Error! Invalid buffer offset supplied at PIO read\n");
		num_bytes = -1;
		return num_bytes;
	}

	bar_memory += *f_offset;

	num_bytes = copy_to_user(buffer, bar_memory, length);
	if (num_bytes != 0) {
		pr_err("Error! copy_to_user failed at PIO read\n");
		num_bytes = length - num_bytes;
	} else {
		num_bytes = length;
	}

	return num_bytes;
}

static ssize_t
pio_write(struct file *file, const char __user *buffer,
	  size_t length, loff_t *f_offset)
{
	char *bar_memory = NULL;
	struct xlnx_ps_pcie_dma_client_device *xdev;
	struct ps_pcie_dma_channel_match *xlnx_match;
	ssize_t num_bytes = 0;
	struct BAR_PARAMS *barinfo;

	xdev = file->private_data;
	xlnx_match = (struct ps_pcie_dma_channel_match *)
			xdev->pcie_dma_chan[0].chan->private;

	barinfo = ((struct BAR_PARAMS *)(xlnx_match->bar_params) +
			PIO_MEMORY_BAR_NUMBER);
	bar_memory = (__force char *)barinfo->BAR_VIRT_ADDR;

	if (length > xdev->pio_translation_size) {
		pr_err("Error! Invalid buffer length supplied at PIO write\n");
		num_bytes = -1;
		return num_bytes;
	}

	if ((length + *f_offset)
			> xdev->pio_translation_size) {
		pr_err("Error! Invalid buffer offset supplied at PIO write\n");
		num_bytes = -1;
		return num_bytes;
	}

	bar_memory += *f_offset;

	num_bytes = copy_from_user(bar_memory, buffer, length);

	if (num_bytes != 0) {
		pr_err("Error! copy_from_user failed at PIO write\n");
		num_bytes = length - num_bytes;
	} else {
		num_bytes = length;
	}

	return num_bytes;
}

static int pio_open(struct inode *in, struct file *file)
{
	struct xlnx_ps_pcie_dma_client_device *xdev;

	xdev = container_of(in->i_cdev,
			    struct xlnx_ps_pcie_dma_client_device,
			    xpio_char_dev);

	file->private_data = xdev;

	return 0;
}

static int pio_release(struct inode *in, struct file *filp)
{
	return 0;
}

static const struct file_operations ps_pcie_pio_fops = {
	.owner		= THIS_MODULE,
	.read		= pio_read,
	.write          = pio_write,
	.unlocked_ioctl = pio_ioctl,
	.open		= pio_open,
	.release	= pio_release,
};

static void destroy_char_iface_for_pio(
		struct xlnx_ps_pcie_dma_client_device *xdev)
{
	device_destroy(g_ps_pcie_dma_client_class,
		       MKDEV(MAJOR(xdev->pio_char_device), 0));
	cdev_del(&xdev->xpio_char_dev);
	unregister_chrdev_region(xdev->pio_char_device, 1);
}

static void destroy_char_iface_for_dma(
		struct xlnx_ps_pcie_dma_client_device *xdev)
{
	int i;
	struct xlnx_completed_info *entry, *next;

	for (i = 0; i < MAX_ALLOWED_CHANNELS_IN_HW; i++) {
		list_for_each_entry_safe(entry, next,
					 &xdev->pcie_dma_chan[i].completed.clist,
					 clist) {
			spin_lock(&xdev->pcie_dma_chan[i].channel_lock);
			list_del(&entry->clist);
			spin_unlock(&xdev->pcie_dma_chan[i].channel_lock);
			kfree(entry);
		}
		device_destroy(g_ps_pcie_dma_client_class,
			       MKDEV(MAJOR(xdev->char_device), i));
	}
	cdev_del(&xdev->xps_pcie_chardev);
	unregister_chrdev_region(xdev->char_device, MAX_ALLOWED_CHANNELS_IN_HW);
}

static void delete_char_dev_interfaces(
	struct xlnx_ps_pcie_dma_client_device *xdev)
{
	destroy_char_iface_for_dma(xdev);
	if (xdev->properties->pio_transfers == PIO_SUPPORTED)
		destroy_char_iface_for_pio(xdev);
}

static void release_dma_channels(struct xlnx_ps_pcie_dma_client_device *xdev)
{
	int i;

	for (i = 0; i < MAX_ALLOWED_CHANNELS_IN_HW; i++)
		dma_release_channel(xdev->pcie_dma_chan[i].chan);
}

static void delete_char_devices(void)
{
	struct xlnx_ps_pcie_dma_client_device *entry, *next;

	list_for_each_entry_safe(entry, next, &g_ps_pcie_dma_client_list,
				 dev_node) {
		list_del(&entry->dev_node);
		delete_char_dev_interfaces(entry);
		release_dma_channels(entry);
		kfree(entry);
	}
}

static bool ps_pcie_dma_filter(struct dma_chan *chan, void *param)
{
	struct ps_pcie_dma_channel_match *client_match =
		(struct ps_pcie_dma_channel_match *)param;

	struct ps_pcie_dma_channel_match *dma_channel_match =
		(struct ps_pcie_dma_channel_match *)chan->private;

	if (client_match && dma_channel_match) {
		if (client_match->pci_vendorid != 0 &&
		    dma_channel_match->pci_vendorid != 0) {
			if (client_match->pci_vendorid == dma_channel_match->pci_vendorid) {
				if (client_match->pci_deviceid == dma_channel_match->pci_deviceid &&
				    client_match->channel_number == dma_channel_match->channel_number &&
				    client_match->direction == dma_channel_match->direction) {
					return true;
				}
			}
		}
	}
	return false;
}

static int acquire_dma_channels(struct xlnx_ps_pcie_dma_client_device *xdev)
{
	int err;
	int i;
	dma_cap_mask_t mask;
	struct ps_pcie_dma_channel_match *match;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE | DMA_PRIVATE, mask);

	for (i = 0; i < MAX_ALLOWED_CHANNELS_IN_HW; i++) {
		match = &xdev->pcie_dma_chan[i].match;
		match->board_number = xdev->properties->board_number;
		match->pci_deviceid = xdev->properties->pci_deviceid;
		match->pci_vendorid = xdev->properties->pci_vendorid;
		match->channel_number = i;
		match->direction = xdev->properties->direction[i];

		xdev->pcie_dma_chan[i].chan =
			dma_request_channel(mask, ps_pcie_dma_filter, match);

		if (!xdev->pcie_dma_chan[i].chan) {
			pr_err("Error channel handle %d board %d channel\n",
			       match->board_number,
			       match->channel_number);
			err = -EINVAL;
			goto err_out_no_channels;
		}
		xdev->pcie_dma_chan[i].dev =
				xdev->pcie_dma_chan[i].chan->device->dev;
		xdev->pcie_dma_chan[i].direction =
				xdev->properties->direction[i];
		xdev->pcie_dma_chan[i].mode =
				xdev->properties->mode;
		INIT_LIST_HEAD(&xdev->pcie_dma_chan[i].completed.clist);
		spin_lock_init(&xdev->pcie_dma_chan[i].channel_lock);
	}

	return 0;

err_out_no_channels:
	while (i > 0) {
		i--;
		dma_release_channel(xdev->pcie_dma_chan[i].chan);
	}
	return err;
}

static int create_char_dev_iface_for_dma_device(
		struct xlnx_ps_pcie_dma_client_device *xdev)
{
	int err = 0;
	int i;

	WARN_ON(!xdev);

	err = alloc_chrdev_region(&xdev->char_device, 0,
				  MAX_ALLOWED_CHANNELS_IN_HW,
				  CHAR_DRIVER_NAME);
	if (err < 0) {
		pr_err("Unable to allocate char device region\n");
		return err;
	}

	xdev->xps_pcie_chardev.owner = THIS_MODULE;
	cdev_init(&xdev->xps_pcie_chardev, &ps_pcie_dma_comm_fops);
	xdev->xps_pcie_chardev.dev = xdev->char_device;

	err = cdev_add(&xdev->xps_pcie_chardev, xdev->char_device,
		       MAX_ALLOWED_CHANNELS_IN_HW);
	if (err < 0) {
		pr_err("PS PCIe DMA unable to add cdev\n");
		goto err_out_cdev_add;
	}

	for (i = 0; i < MAX_ALLOWED_CHANNELS_IN_HW; i++) {
		xdev->chardev[i] =
			device_create(g_ps_pcie_dma_client_class,
				      xdev->pcie_dma_chan[i].dev,
				      MKDEV(MAJOR(xdev->char_device), i),
				      xdev,
				      "%s%d_%d", CHAR_DRIVER_NAME,
				      i, xdev->properties->board_number);

		if (!xdev->chardev[i]) {
			err = PTR_ERR(xdev->chardev[i]);
			pr_err(
			"PS PCIe DMA Unable to create device %d\n", i);
			goto err_out_dev_create;
		}
	}

	return 0;

err_out_dev_create:
	while (--i >= 0) {
		device_destroy(g_ps_pcie_dma_client_class,
			       MKDEV(MAJOR(xdev->char_device), i));
	}
	cdev_del(&xdev->xps_pcie_chardev);
err_out_cdev_add:
	unregister_chrdev_region(xdev->char_device, MAX_ALLOWED_CHANNELS_IN_HW);
	return err;
}

static int create_char_dev_iface_for_pio(
		struct xlnx_ps_pcie_dma_client_device *xdev)
{
	int err;

	err = alloc_chrdev_region(&xdev->pio_char_device, 0, 1,
				  PIO_CHAR_DRIVER_NAME);
	if (err < 0) {
		pr_err("Unable to allocate pio character device region\n");
		return err;
	}

	xdev->xpio_char_dev.owner = THIS_MODULE;
	cdev_init(&xdev->xpio_char_dev, &ps_pcie_pio_fops);
	xdev->xpio_char_dev.dev = xdev->pio_char_device;

	err = cdev_add(&xdev->xpio_char_dev, xdev->pio_char_device, 1);
	if (err < 0) {
		pr_err("PS PCIe DMA unable to add cdev for pio\n");
		goto err_out_pio_cdev_add;
	}

	xdev->xpio_char_device =
		device_create(g_ps_pcie_dma_client_class,
			      xdev->pcie_dma_chan[0].dev,
			      MKDEV(MAJOR(xdev->pio_char_device), 0),
			      xdev, "%s_%d", PIO_CHAR_DRIVER_NAME,
			      xdev->properties->board_number);

	if (!xdev->xpio_char_device) {
		err = PTR_ERR(xdev->xpio_char_device);
		pr_err("PS PCIe DMA Unable to create pio device\n");
		goto err_out_pio_dev_create;
	}

	mutex_init(&xdev->pio_chardev_mutex);
	xdev->pio_translation_size = 0;
	init_completion(&xdev->trans_cmpltn);

	return 0;

err_out_pio_dev_create:
	cdev_del(&xdev->xpio_char_dev);
err_out_pio_cdev_add:
	unregister_chrdev_region(xdev->pio_char_device, 1);
	return err;
}

static int create_char_dev_interfaces(
	struct xlnx_ps_pcie_dma_client_device *xdev)
{
	int err;

	err = create_char_dev_iface_for_dma_device(xdev);

	if (err != 0) {
		pr_err("Unable to create char dev dma iface %d\n",
		       xdev->properties->pci_deviceid);
		goto no_char_iface_for_dma;
	}

	if (xdev->properties->pio_transfers == PIO_SUPPORTED) {
		err = create_char_dev_iface_for_pio(xdev);
		if (err != 0) {
			pr_err("Unable to create char dev pio iface %d\n",
			       xdev->properties->pci_deviceid);
			goto no_char_iface_for_pio;
		}
	}

	return 0;

no_char_iface_for_pio:
	destroy_char_iface_for_dma(xdev);
no_char_iface_for_dma:
	return err;
}

static int setup_char_devices(u16 dev_prop_index)
{
	struct xlnx_ps_pcie_dma_client_device *xdev;
	int err;
	int i;

	xdev = kzalloc(sizeof(*xdev), GFP_KERNEL);
	if (!xdev) {
		err = -ENOMEM;
		return err;
	}

	xdev->properties = &g_dma_deviceproperties_list[dev_prop_index];

	err = acquire_dma_channels(xdev);
	if (err != 0) {
		pr_err("Unable to acquire dma channels %d\n",
		       dev_prop_index);
		goto err_no_dma_channels;
	}

	err = create_char_dev_interfaces(xdev);
	if (err != 0) {
		pr_err("Unable to create char dev interfaces %d\n",
		       dev_prop_index);
		goto err_no_char_dev_ifaces;
	}

	list_add_tail(&xdev->dev_node, &g_ps_pcie_dma_client_list);

	return 0;

err_no_char_dev_ifaces:
	for (i = 0; i < MAX_ALLOWED_CHANNELS_IN_HW; i++)
		dma_release_channel(xdev->pcie_dma_chan[i].chan);
err_no_dma_channels:
	kfree(xdev);
	return err;
}

/**
 * ps_pcie_dma_client_init - Driver init function
 *
 * Return: 0 on success. Non zero on failure
 */
static int __init ps_pcie_dma_client_init(void)
{
	int err;
	int i;
	size_t num_dma_dev_properties;

	INIT_LIST_HEAD(&g_ps_pcie_dma_client_list);

	g_ps_pcie_dma_client_class = class_create(THIS_MODULE, DRV_MODULE_NAME);
	if (IS_ERR(g_ps_pcie_dma_client_class)) {
		pr_err("%s failed to create class\n", DRV_MODULE_NAME);
		return PTR_ERR(g_ps_pcie_dma_client_class);
	}

	num_dma_dev_properties = ARRAY_SIZE(g_dma_deviceproperties_list);
	for (i = 0; i < num_dma_dev_properties; i++) {
		err = setup_char_devices(i);
		if (err) {
			pr_err("Error creating char devices for %d\n", i);
			goto err_no_char_devices;
		}
	}

	pr_info("PS PCIe DMA Client Driver Init successful\n");
	return 0;

err_no_char_devices:
	delete_char_devices();

	if (g_ps_pcie_dma_client_class)
		class_destroy(g_ps_pcie_dma_client_class);
	return err;
}
late_initcall(ps_pcie_dma_client_init);

/**
 * ps_pcie_dma_client_exit - Driver exit function
 *
 */
static void __exit ps_pcie_dma_client_exit(void)
{
	delete_char_devices();

	if (g_ps_pcie_dma_client_class)
		class_destroy(g_ps_pcie_dma_client_class);
}

module_exit(ps_pcie_dma_client_exit);

MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("Xilinx PS PCIe DMA client Driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq R5 Remote Processor driver
 *
 * Copyright (C) 2015 - 2018 Xilinx Inc.
 * Copyright (C) 2015 Jason Wu <j.wu@xilinx.com>
 *
 * Based on origin OMAP and Zynq Remote Processor driver
 *
 * Copyright (C) 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012 PetaLogix
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/genalloc.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/zynqmp-ipi-message.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pfn.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "remoteproc_internal.h"

#define MAX_RPROCS	2 /* Support up to 2 RPU */
#define MAX_MEM_PNODES	4 /* Max power nodes for one RPU memory instance */

#define DEFAULT_FIRMWARE_NAME	"rproc-rpu-fw"

/* PM proc states */
#define PM_PROC_STATE_ACTIVE 1U

/* IPI buffer MAX length */
#define IPI_BUF_LEN_MAX	32U
/* RX mailbox client buffer max length */
#define RX_MBOX_CLIENT_BUF_MAX	(IPI_BUF_LEN_MAX + \
				 sizeof(struct zynqmp_ipi_message))

static bool autoboot __read_mostly;
static bool allow_sysfs_kick __read_mostly;

struct zynqmp_rpu_domain_pdata;

static const struct zynqmp_eemi_ops *eemi_ops;

/**
 * struct zynqmp_r5_mem - zynqmp rpu memory data
 * @pnode_id: TCM power domain ids
 * @res: memory resource
 * @node: list node
 */
struct zynqmp_r5_mem {
	u32 pnode_id[MAX_MEM_PNODES];
	struct resource res;
	struct list_head node;
};

/**
 * struct zynqmp_r5_pdata - zynqmp rpu remote processor private data
 * @dev: device of RPU instance
 * @rproc: rproc handle
 * @parent: RPU slot platform data
 * @pnode_id: RPU CPU power domain id
 * @mems: memory resources
 * @is_r5_mode_set: indicate if r5 operation mode is set
 * @tx_mc: tx mailbox client
 * @rx_mc: rx mailbox client
 * @tx_chan: tx mailbox channel
 * @rx_chan: rx mailbox channel
 * @workqueue: workqueue for the RPU remoteproc
 * @tx_mc_skbs: socket buffers for tx mailbox client
 * @rx_mc_buf: rx mailbox client buffer to save the rx message
 * @remote_kick: flag to indicate if there is a kick from remote
 */
struct zynqmp_r5_pdata {
	struct device dev;
	struct rproc *rproc;
	struct zynqmp_rpu_domain_pdata *parent;
	u32 pnode_id;
	struct list_head mems;
	bool is_r5_mode_set;
	struct mbox_client tx_mc;
	struct mbox_client rx_mc;
	struct mbox_chan *tx_chan;
	struct mbox_chan *rx_chan;
	struct work_struct workqueue;
	struct sk_buff_head tx_mc_skbs;
	unsigned char rx_mc_buf[RX_MBOX_CLIENT_BUF_MAX];
	atomic_t remote_kick;
};

/**
 * struct zynqmp_rpu_domain_pdata - zynqmp rpu platform data
 * @rpus: table of RPUs
 * @rpu_mode: RPU core configuration
 */
struct zynqmp_rpu_domain_pdata {
	struct zynqmp_r5_pdata rpus[MAX_RPROCS];
	enum rpu_oper_mode rpu_mode;
};

/*
 * r5_set_mode - set RPU operation mode
 * @pdata: Remote processor private data
 *
 * set RPU oepration mode
 *
 * Return: 0 for success, negative value for failure
 */
static int r5_set_mode(struct zynqmp_r5_pdata *pdata)
{
	u32 val[PAYLOAD_ARG_CNT] = {0}, expect;
	struct zynqmp_rpu_domain_pdata *parent;
	struct device *dev = &pdata->dev;
	int ret;

	if (pdata->is_r5_mode_set)
		return 0;
	parent = pdata->parent;
	expect = (u32)parent->rpu_mode;
	ret = eemi_ops->ioctl(pdata->pnode_id, IOCTL_GET_RPU_OPER_MODE,
			  0, 0, val);
	if (ret < 0) {
		dev_err(dev, "failed to get RPU oper mode.\n");
		return ret;
	}
	if (val[0] == expect) {
		dev_dbg(dev, "RPU mode matches: %x\n", val[0]);
	} else {
		ret = eemi_ops->ioctl(pdata->pnode_id,
				  IOCTL_SET_RPU_OPER_MODE,
				  expect, 0, val);
		if (ret < 0) {
			dev_err(dev,
				"failed to set RPU oper mode.\n");
			return ret;
		}
	}
	if (expect == (u32)PM_RPU_MODE_LOCKSTEP)
		expect = (u32)PM_RPU_TCM_COMB;
	else
		expect = (u32)PM_RPU_TCM_SPLIT;
	ret = eemi_ops->ioctl(pdata->pnode_id, IOCTL_TCM_COMB_CONFIG,
			  expect, 0, val);
	if (ret < 0) {
		dev_err(dev, "failed to config TCM to %x.\n",
			expect);
		return ret;
	}
	pdata->is_r5_mode_set = true;
	return 0;
}

/**
 * r5_is_running - check if r5 is running
 * @pdata: Remote processor private data
 *
 * check if R5 is running
 *
 * Return: true if r5 is running, false otherwise
 */
static bool r5_is_running(struct zynqmp_r5_pdata *pdata)
{
	u32 status, requirements, usage;
	struct device *dev = &pdata->dev;

	if (eemi_ops->get_node_status(pdata->pnode_id,
				      &status, &requirements, &usage)) {
		dev_err(dev, "Failed to get RPU node %d status.\n",
			pdata->pnode_id);
		return false;
	} else if (status != PM_PROC_STATE_ACTIVE) {
		dev_dbg(dev, "RPU is not running.\n");
		return false;
	}

	dev_dbg(dev, "RPU is running.\n");
	return true;
}

/**
 * r5_request_mem - request RPU memory
 * @rproc: pointer to remoteproc instance
 * @mem: pointer to RPU memory
 *
 * Request RPU memory resource to make it accessible by the kernel.
 *
 * Return: 0 if success, negative value for failure.
 */
static int r5_request_mem(struct rproc *rproc, struct zynqmp_r5_mem *mem)
{
	int i, ret;
	struct device *dev = &rproc->dev;
	struct zynqmp_r5_pdata *local = rproc->priv;

	for (i = 0; i < MAX_MEM_PNODES; i++) {
		if (mem->pnode_id[i]) {
			ret = eemi_ops->request_node(mem->pnode_id[i],
						 ZYNQMP_PM_CAPABILITY_ACCESS,
						 0,
						 ZYNQMP_PM_REQUEST_ACK_BLOCKING
						);
			if (ret < 0) {
				dev_err(dev,
					"failed to request power node: %u\n",
					mem->pnode_id[i]);
				return ret;
			}
		} else {
			break;
		}
	}

	ret = r5_set_mode(local);
	if (ret < 0) {
		dev_err(dev, "failed to set R5 operation mode.\n");
		return ret;
	}
	return 0;
}

/*
 * ZynqMP R5 remoteproc memory release function
 */
static int zynqmp_r5_mem_release(struct rproc *rproc,
				 struct rproc_mem_entry *mem)
{
	struct zynqmp_r5_mem *priv;
	int i, ret;
	struct device *dev = &rproc->dev;

	priv = mem->priv;
	if (!priv)
		return 0;
	for (i = 0; i < MAX_MEM_PNODES; i++) {
		if (priv->pnode_id[i]) {
			dev_dbg(dev, "%s, pnode %d\n",
				__func__, priv->pnode_id[i]);
			ret = eemi_ops->release_node(priv->pnode_id[i]);
			if (ret < 0) {
				dev_err(dev,
					"failed to release power node: %u\n",
					priv->pnode_id[i]);
				return ret;
			}
		} else {
			break;
		}
	}
	return 0;
}

/*
 * ZynqMP R5 remoteproc operations
 */
static int zynqmp_r5_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_pdata *local = rproc->priv;
	enum rpu_boot_mem bootmem;
	int ret;

	/* Set up R5 */
	ret = r5_set_mode(local);
	if (ret) {
		dev_err(dev, "failed to set R5 operation mode.\n");
		return ret;
	}
	if ((rproc->bootaddr & 0xF0000000) == 0xF0000000)
		bootmem = PM_RPU_BOOTMEM_HIVEC;
	else
		bootmem = PM_RPU_BOOTMEM_LOVEC;
	dev_info(dev, "RPU boot from %s.",
		 bootmem == PM_RPU_BOOTMEM_HIVEC ? "OCM" : "TCM");

	ret = eemi_ops->request_wakeup(local->pnode_id, 1, bootmem,
				       ZYNQMP_PM_REQUEST_ACK_NO);
	if (ret < 0) {
		dev_err(dev, "failed to boot R5.\n");
		return ret;
	}
	return 0;
}

static int zynqmp_r5_rproc_stop(struct rproc *rproc)
{
	struct zynqmp_r5_pdata *local = rproc->priv;
	int ret;

	ret = eemi_ops->force_powerdown(local->pnode_id,
					ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	if (ret < 0) {
		dev_err(&local->dev, "failed to shutdown R5.\n");
		return ret;
	}
	local->is_r5_mode_set = false;
	return 0;
}

static int zynqmp_r5_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret == -EINVAL)
		ret = 0;
	return ret;
}

static void *zynqmp_r5_da_to_va(struct rproc *rproc, u64 da, int len)
{
	struct zynqmp_r5_pdata *local = rproc->priv;
	struct zynqmp_r5_mem *mem;
	struct device *dev;

	dev = &local->dev;
	list_for_each_entry(mem, &local->mems, node) {
		struct rproc_mem_entry *rproc_mem;
		struct resource *res = &mem->res;
		u64 res_da = (u64)res->start;
		resource_size_t size;
		int offset, ret;
		void *va;
		dma_addr_t dma;

		if ((res_da & 0xfff00000) == 0xffe00000) {
			res_da &= 0x000fffff;
			if (res_da & 0x80000)
				res_da -= 0x90000;
		}

		offset = (int)(da - res_da);
		if (offset < 0)
			continue;
		size = resource_size(res);
		if (offset + len > (int)size)
			continue;

		ret = r5_request_mem(rproc, mem);
		if (ret < 0) {
			dev_err(dev, "failed to request memory %pad.\n",
				&res->start);
			return NULL;
		}

		va = devm_ioremap_wc(dev, res->start, size);
		dma = (dma_addr_t)res->start;
		da = (u32)res_da;
		rproc_mem = rproc_mem_entry_init(dev, va, dma, (int)size, da,
						 NULL, zynqmp_r5_mem_release,
						 res->name);
		if (!rproc_mem)
			return NULL;
		rproc_mem->priv = mem;
		dev_dbg(dev, "%s: %s, va = %p, da = 0x%x dma = 0x%llx\n",
			__func__, rproc_mem->name, rproc_mem->va,
			rproc_mem->da, rproc_mem->dma);
		rproc_add_carveout(rproc, rproc_mem);
		return (char *)va + offset;
	}
	return NULL;
}

/* kick a firmware */
static void zynqmp_r5_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_pdata *local = rproc->priv;

	dev_dbg(dev, "KICK Firmware to start send messages vqid %d\n", vqid);

	if (vqid < 0) {
		/* If vqid is negative, does not pass the vqid to
		 * mailbox. As vqid is supposed to be 0 or possive.
		 * It also gives a way to just kick instead but
		 * not use the IPI buffer. It is better to provide
		 * a proper way to pass the short message, which will
		 * need to sync to upstream first, for now,
		 * use negative vqid to assume no message will be
		 * passed with IPI buffer, but just raise interrupt.
		 * This will be faster as it doesn't need to copy the
		 * message to the IPI buffer.
		 *
		 * It will ignore the return, as failure is due to
		 * there already kicks in the mailbox queue.
		 */
		(void)mbox_send_message(local->tx_chan, NULL);
	} else {
		struct sk_buff *skb;
		unsigned int skb_len;
		struct zynqmp_ipi_message *mb_msg;
		int ret;

		skb_len = (unsigned int)(sizeof(vqid) + sizeof(mb_msg));
		skb = alloc_skb(skb_len, GFP_ATOMIC);
		if (!skb) {
			dev_err(dev,
				"Failed to allocate skb to kick remote.\n");
			return;
		}
		mb_msg = (struct zynqmp_ipi_message *)skb_put(skb, skb_len);
		mb_msg->len = sizeof(vqid);
		memcpy(mb_msg->data, &vqid, sizeof(vqid));
		skb_queue_tail(&local->tx_mc_skbs, skb);
		ret = mbox_send_message(local->tx_chan, mb_msg);
		if (ret < 0) {
			dev_warn(dev, "Failed to kick remote.\n");
			skb_dequeue_tail(&local->tx_mc_skbs);
			kfree_skb(skb);
		}
	}
}

static bool zynqmp_r5_rproc_peek_remote_kick(struct rproc *rproc,
					     char *buf, size_t *len)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_pdata *local = rproc->priv;

	dev_dbg(dev, "Peek if remote has kicked\n");

	if (atomic_read(&local->remote_kick) != 0) {
		if (buf && len) {
			struct zynqmp_ipi_message *msg;

			msg = (struct zynqmp_ipi_message *)local->rx_mc_buf;
			memcpy(buf, msg->data, msg->len);
			*len = (size_t)msg->len;
		}
		return true;
	} else {
		return false;
	}
}

static void zynqmp_r5_rproc_ack_remote_kick(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_pdata *local = rproc->priv;

	dev_dbg(dev, "Ack remote\n");

	atomic_set(&local->remote_kick, 0);
	(void)mbox_send_message(local->rx_chan, NULL);
}

static struct rproc_ops zynqmp_r5_rproc_ops = {
	.start		= zynqmp_r5_rproc_start,
	.stop		= zynqmp_r5_rproc_stop,
	.load		= rproc_elf_load_segments,
	.parse_fw	= zynqmp_r5_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check	= rproc_elf_sanity_check,
	.get_boot_addr	= rproc_elf_get_boot_addr,
	.da_to_va	= zynqmp_r5_da_to_va,
	.kick		= zynqmp_r5_rproc_kick,
	.peek_remote_kick	= zynqmp_r5_rproc_peek_remote_kick,
	.ack_remote_kick	= zynqmp_r5_rproc_ack_remote_kick,
};

/* zynqmp_r5_get_reserved_mems() - get reserved memories
 * @pdata: pointer to the RPU remoteproc private data
 *
 * Function to retrieve the memories resources from memory-region
 * property.
 */
static int zynqmp_r5_get_reserved_mems(struct zynqmp_r5_pdata *pdata)
{
	struct device *dev = &pdata->dev;
	struct device_node *np = dev->of_node;
	int num_mems;
	int i;

	num_mems = of_count_phandle_with_args(np, "memory-region", NULL);
	if (num_mems <= 0)
		return 0;
	for (i = 0; i < num_mems; i++) {
		struct device_node *node;
		struct zynqmp_r5_mem *mem;
		int ret;

		node = of_parse_phandle(np, "memory-region", i);
		ret = of_device_is_compatible(node, "shared-dma-pool");
		if (ret) {
			/* it is DMA memory. */
			ret = of_reserved_mem_device_init_by_idx(dev, np, i);
			if (ret) {
				dev_err(dev, "unable to reserve DMA mem.\n");
				return ret;
			}
			dev_dbg(dev, "%s, dma memory %s.\n",
				__func__, of_node_full_name(node));
			continue;
		}
		/*
		 * It is non-DMA memory, used for firmware loading.
		 * It will be added to the R5 remoteproc mappings later.
		 */
		mem = devm_kzalloc(dev, sizeof(*mem), GFP_KERNEL);
		if (!mem)
			return -ENOMEM;
		ret = of_address_to_resource(node, 0, &mem->res);
		if (ret) {
			dev_err(dev, "unable to resolve memory region.\n");
			return ret;
		}
		list_add_tail(&mem->node, &pdata->mems);
		dev_dbg(dev, "%s, non-dma mem %s\n",
			__func__, of_node_full_name(node));
	}
	return 0;
}

/* zynqmp_r5_mem_probe() - probes RPU TCM memory device
 * @pdata: pointer to the RPU remoteproc private data
 * @node: pointer to the memory node
 *
 * Function to retrieve memories resources for RPU TCM memory device.
 */
static int zynqmp_r5_mem_probe(struct zynqmp_r5_pdata *pdata,
			       struct device_node *node)
{
	struct device *dev;
	struct zynqmp_r5_mem *mem;
	int ret;

	dev = &pdata->dev;
	mem = devm_kzalloc(dev, sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;
	ret = of_address_to_resource(node, 0, &mem->res);
	if (ret < 0) {
		dev_err(dev, "failed to get resource of memory %s",
			of_node_full_name(node));
		return -EINVAL;
	}

	/* Get the power domain id */
	if (of_find_property(node, "pnode-id", NULL)) {
		struct property *prop;
		const __be32 *cur;
		u32 val;
		int i = 0;

		of_property_for_each_u32(node, "pnode-id", prop, cur, val)
			mem->pnode_id[i++] = val;
	}
	list_add_tail(&mem->node, &pdata->mems);
	return 0;
}

/**
 * zynqmp_r5_release() - ZynqMP R5 device release function
 * @dev: pointer to the device struct of ZynqMP R5
 *
 * Function to release ZynqMP R5 device.
 */
static void zynqmp_r5_release(struct device *dev)
{
	struct zynqmp_r5_pdata *pdata;
	struct rproc *rproc;
	struct sk_buff *skb;

	pdata = dev_get_drvdata(dev);
	rproc = pdata->rproc;
	if (rproc) {
		rproc_del(rproc);
		rproc_free(rproc);
	}
	if (pdata->tx_chan)
		mbox_free_channel(pdata->tx_chan);
	if (pdata->rx_chan)
		mbox_free_channel(pdata->rx_chan);
	/* Discard all SKBs */
	while (!skb_queue_empty(&pdata->tx_mc_skbs)) {
		skb = skb_dequeue(&pdata->tx_mc_skbs);
		kfree_skb(skb);
	}

	put_device(dev->parent);
}

/**
 * event_notified_idr_cb() - event notified idr callback
 * @id: idr id
 * @ptr: pointer to idr private data
 * @data: data passed to idr_for_each callback
 *
 * Pass notification to remtoeproc virtio
 *
 * Return: 0. having return is to satisfy the idr_for_each() function
 *          pointer input argument requirement.
 **/
static int event_notified_idr_cb(int id, void *ptr, void *data)
{
	struct rproc *rproc = data;

	(void)rproc_vq_interrupt(rproc, id);
	return 0;
}

/**
 * handle_event_notified() - remoteproc notification work funciton
 * @work: pointer to the work structure
 *
 * It checks each registered remoteproc notify IDs.
 */
static void handle_event_notified(struct work_struct *work)
{
	struct rproc *rproc;
	struct zynqmp_r5_pdata *local;

	local = container_of(work, struct zynqmp_r5_pdata, workqueue);

	(void)mbox_send_message(local->rx_chan, NULL);
	rproc = local->rproc;
	if (rproc->sysfs_kick) {
		sysfs_notify(&rproc->dev.kobj, NULL, "remote_kick");
		return;
	}
	/*
	 * We only use IPI for interrupt. The firmware side may or may
	 * not write the notifyid when it trigger IPI.
	 * And thus, we scan through all the registered notifyids.
	 */
	idr_for_each(&rproc->notifyids, event_notified_idr_cb, rproc);
}

/**
 * zynqmp_r5_mb_rx_cb() - Receive channel mailbox callback
 * @cl: mailbox client
 * @mssg: message pointer
 *
 * It will schedule the R5 notification work.
 */
static void zynqmp_r5_mb_rx_cb(struct mbox_client *cl, void *mssg)
{
	struct zynqmp_r5_pdata *local;

	local = container_of(cl, struct zynqmp_r5_pdata, rx_mc);
	if (mssg) {
		struct zynqmp_ipi_message *ipi_msg, *buf_msg;
		size_t len;

		ipi_msg = (struct zynqmp_ipi_message *)mssg;
		buf_msg = (struct zynqmp_ipi_message *)local->rx_mc_buf;
		len = (ipi_msg->len >= IPI_BUF_LEN_MAX) ?
		      IPI_BUF_LEN_MAX : ipi_msg->len;
		buf_msg->len = len;
		memcpy(buf_msg->data, ipi_msg->data, len);
	}
	atomic_set(&local->remote_kick, 1);
	schedule_work(&local->workqueue);
}

/**
 * zynqmp_r5_mb_tx_done() - Request has been sent to the remote
 * @cl: mailbox client
 * @mssg: pointer to the message which has been sent
 * @r: status of last TX - OK or error
 *
 * It will be called by the mailbox framework when the last TX has done.
 */
static void zynqmp_r5_mb_tx_done(struct mbox_client *cl, void *mssg, int r)
{
	struct zynqmp_r5_pdata *local;
	struct sk_buff *skb;

	if (!mssg)
		return;
	local = container_of(cl, struct zynqmp_r5_pdata, tx_mc);
	skb = skb_dequeue(&local->tx_mc_skbs);
	kfree_skb(skb);
}

/**
 * zynqmp_r5_setup_mbox() - Setup mailboxes
 *
 * @pdata: pointer to the ZynqMP R5 processor platform data
 * @node: pointer of the device node
 *
 * Function to setup mailboxes to talk to RPU.
 *
 * Return: 0 for success, negative value for failure.
 */
static int zynqmp_r5_setup_mbox(struct zynqmp_r5_pdata *pdata,
				struct device_node *node)
{
	struct device *dev = &pdata->dev;
	struct mbox_client *mclient;

	/* Setup TX mailbox channel client */
	mclient = &pdata->tx_mc;
	mclient->dev = dev;
	mclient->rx_callback = NULL;
	mclient->tx_block = false;
	mclient->knows_txdone = false;
	mclient->tx_done = zynqmp_r5_mb_tx_done;

	/* Setup TX mailbox channel client */
	mclient = &pdata->rx_mc;
	mclient->dev = dev;
	mclient->rx_callback = zynqmp_r5_mb_rx_cb;
	mclient->tx_block = false;
	mclient->knows_txdone = false;

	INIT_WORK(&pdata->workqueue, handle_event_notified);

	atomic_set(&pdata->remote_kick, 0);
	/* Request TX and RX channels */
	pdata->tx_chan = mbox_request_channel_byname(&pdata->tx_mc, "tx");
	if (IS_ERR(pdata->tx_chan)) {
		dev_err(dev, "failed to request mbox tx channel.\n");
		pdata->tx_chan = NULL;
		return -EINVAL;
	}
	pdata->rx_chan = mbox_request_channel_byname(&pdata->rx_mc, "rx");
	if (IS_ERR(pdata->rx_chan)) {
		dev_err(dev, "failed to request mbox rx channel.\n");
		pdata->rx_chan = NULL;
		return -EINVAL;
	}
	skb_queue_head_init(&pdata->tx_mc_skbs);
	return 0;
}

/**
 * zynqmp_r5_probe() - Probes ZynqMP R5 processor device node
 * @pdata: pointer to the ZynqMP R5 processor platform data
 * @pdev: parent RPU domain platform device
 * @node: pointer of the device node
 *
 * Function to retrieve the information of the ZynqMP R5 device node.
 *
 * Return: 0 for success, negative value for failure.
 */
static int zynqmp_r5_probe(struct zynqmp_r5_pdata *pdata,
			   struct platform_device *pdev,
			   struct device_node *node)
{
	struct device *dev = &pdata->dev;
	struct rproc *rproc;
	struct device_node *nc;
	int ret;

	/* Create device for ZynqMP R5 device */
	dev->parent = &pdev->dev;
	dev->release = zynqmp_r5_release;
	dev->of_node = node;
	dev_set_name(dev, "%s", of_node_full_name(node));
	dev_set_drvdata(dev, pdata);
	ret = device_register(dev);
	if (ret) {
		dev_err(dev, "failed to register device.\n");
		return ret;
	}
	get_device(&pdev->dev);

	/* Allocate remoteproc instance */
	rproc = rproc_alloc(dev, dev_name(dev), &zynqmp_r5_rproc_ops, NULL, 0);
	if (!rproc) {
		dev_err(dev, "rproc allocation failed.\n");
		ret = -ENOMEM;
		goto error;
	}
	rproc->auto_boot = autoboot;
	pdata->rproc = rproc;
	rproc->priv = pdata;

	/* Probe R5 memory devices */
	INIT_LIST_HEAD(&pdata->mems);
	for_each_available_child_of_node(node, nc) {
		ret = zynqmp_r5_mem_probe(pdata, nc);
		if (ret) {
			dev_err(dev, "failed to probe memory %s.\n",
				of_node_full_name(nc));
			goto error;
		}
	}

	/* Probe reserved system memories used by R5 */
	ret = zynqmp_r5_get_reserved_mems(pdata);
	if (ret) {
		dev_err(dev, "failed to get reserved memory.\n");
		goto error;
	}

	/* Set up DMA mask */
	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_warn(dev, "dma_set_coherent_mask failed: %d\n", ret);
		/* If DMA is not configured yet, try to configure it. */
		ret = of_dma_configure(dev, node, true);
		if (ret) {
			dev_err(dev, "failed to configure DMA.\n");
			goto error;
		}
	}

	/* Get R5 power domain node */
	ret = of_property_read_u32(node, "pnode-id", &pdata->pnode_id);
	if (ret) {
		dev_err(dev, "failed to get power node id.\n");
		goto error;
	}

	/* Check if R5 is running */
	if (r5_is_running(pdata)) {
		atomic_inc(&rproc->power);
		rproc->state = RPROC_RUNNING;
	}

	if (!of_get_property(dev->of_node, "mboxes", NULL)) {
		dev_info(dev, "no mailboxes.\n");
	} else {
		ret = zynqmp_r5_setup_mbox(pdata, node);
		if (ret < 0)
			goto error;
	}

	/* Add R5 remoteproc */
	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc registration failed\n");
		goto error;
	}

	if (allow_sysfs_kick) {
		dev_info(dev, "Trying to create remote sysfs entry.\n");
		rproc->sysfs_kick = 1;
		(void)rproc_create_kick_sysfs(rproc);
	}

	return 0;
error:
	if (pdata->rproc)
		rproc_free(pdata->rproc);
	pdata->rproc = NULL;
	device_unregister(dev);
	put_device(&pdev->dev);
	return ret;
}

static int zynqmp_r5_remoteproc_probe(struct platform_device *pdev)
{
	const unsigned char *prop;
	int ret = 0, i;
	struct zynqmp_rpu_domain_pdata *local;
	struct device *dev = &pdev->dev;
	struct device_node *nc;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

	local = devm_kzalloc(dev, sizeof(*local), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	platform_set_drvdata(pdev, local);

	prop = of_get_property(dev->of_node, "core_conf", NULL);
	if (!prop) {
		dev_err(&pdev->dev, "core_conf is not used.\n");
		return -EINVAL;
	}

	dev_info(dev, "RPU core_conf: %s\n", prop);
	if (!strcmp(prop, "split")) {
		local->rpu_mode = PM_RPU_MODE_SPLIT;
	} else if (!strcmp(prop, "lockstep")) {
		local->rpu_mode = PM_RPU_MODE_LOCKSTEP;
	} else {
		dev_err(dev,
			"Invalid core_conf mode provided - %s , %d\n",
			prop, (int)local->rpu_mode);
		return -EINVAL;
	}

	i = 0;
	for_each_available_child_of_node(dev->of_node, nc) {
		local->rpus[i].parent = local;
		ret = zynqmp_r5_probe(&local->rpus[i], pdev, nc);
		if (ret) {
			dev_err(dev, "failed to probe rpu %s.\n",
				of_node_full_name(nc));
			return ret;
		}
		i++;
	}

	return 0;
}

static int zynqmp_r5_remoteproc_remove(struct platform_device *pdev)
{
	struct zynqmp_rpu_domain_pdata *local = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < MAX_RPROCS; i++) {
		struct zynqmp_r5_pdata *rpu = &local->rpus[i];
		struct rproc *rproc;

		rproc = rpu->rproc;
		if (rproc) {
			rproc_del(rproc);
			rproc_free(rproc);
			rpu->rproc = NULL;
		}
		if (rpu->tx_chan) {
			mbox_free_channel(rpu->tx_chan);
			rpu->tx_chan = NULL;
		}
		if (rpu->rx_chan) {
			mbox_free_channel(rpu->rx_chan);
			rpu->rx_chan = NULL;
		}

		device_unregister(&rpu->dev);
	}

	return 0;
}

/* Match table for OF platform binding */
static const struct of_device_id zynqmp_r5_remoteproc_match[] = {
	{ .compatible = "xlnx,zynqmp-r5-remoteproc-1.0", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_r5_remoteproc_match);

static struct platform_driver zynqmp_r5_remoteproc_driver = {
	.probe = zynqmp_r5_remoteproc_probe,
	.remove = zynqmp_r5_remoteproc_remove,
	.driver = {
		.name = "zynqmp_r5_remoteproc",
		.of_match_table = zynqmp_r5_remoteproc_match,
	},
};
module_platform_driver(zynqmp_r5_remoteproc_driver);

module_param_named(autoboot,  autoboot, bool, 0444);
MODULE_PARM_DESC(autoboot,
		 "enable | disable autoboot. (default: true)");
module_param_named(allow_sysfs_kick, allow_sysfs_kick, bool, 0444);
MODULE_PARM_DESC(allow_sysfs_kick,
		 "enable | disable allow kick from sysfs. (default: false)");

MODULE_AUTHOR("Jason Wu <j.wu@xilinx.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ZynqMP R5 remote processor control driver");

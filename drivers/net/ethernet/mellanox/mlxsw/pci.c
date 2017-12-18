/*
 * drivers/net/ethernet/mellanox/mlxsw/pci.c
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/log2.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#include "pci.h"
#include "core.h"
#include "cmd.h"
#include "port.h"

static const char mlxsw_pci_driver_name[] = "mlxsw_pci";

static const struct pci_device_id mlxsw_pci_id_table[] = {
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SWITCHX2), 0},
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SPECTRUM), 0},
	{0, }
};

static struct dentry *mlxsw_pci_dbg_root;

static const char *mlxsw_pci_device_kind_get(const struct pci_device_id *id)
{
	switch (id->device) {
	case PCI_DEVICE_ID_MELLANOX_SWITCHX2:
		return MLXSW_DEVICE_KIND_SWITCHX2;
	case PCI_DEVICE_ID_MELLANOX_SPECTRUM:
		return MLXSW_DEVICE_KIND_SPECTRUM;
	default:
		BUG();
	}
}

#define mlxsw_pci_write32(mlxsw_pci, reg, val) \
	iowrite32be(val, (mlxsw_pci)->hw_addr + (MLXSW_PCI_ ## reg))
#define mlxsw_pci_read32(mlxsw_pci, reg) \
	ioread32be((mlxsw_pci)->hw_addr + (MLXSW_PCI_ ## reg))

enum mlxsw_pci_queue_type {
	MLXSW_PCI_QUEUE_TYPE_SDQ,
	MLXSW_PCI_QUEUE_TYPE_RDQ,
	MLXSW_PCI_QUEUE_TYPE_CQ,
	MLXSW_PCI_QUEUE_TYPE_EQ,
};

static const char *mlxsw_pci_queue_type_str(enum mlxsw_pci_queue_type q_type)
{
	switch (q_type) {
	case MLXSW_PCI_QUEUE_TYPE_SDQ:
		return "sdq";
	case MLXSW_PCI_QUEUE_TYPE_RDQ:
		return "rdq";
	case MLXSW_PCI_QUEUE_TYPE_CQ:
		return "cq";
	case MLXSW_PCI_QUEUE_TYPE_EQ:
		return "eq";
	}
	BUG();
}

#define MLXSW_PCI_QUEUE_TYPE_COUNT	4

static const u16 mlxsw_pci_doorbell_type_offset[] = {
	MLXSW_PCI_DOORBELL_SDQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_SDQ */
	MLXSW_PCI_DOORBELL_RDQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_RDQ */
	MLXSW_PCI_DOORBELL_CQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_CQ */
	MLXSW_PCI_DOORBELL_EQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_EQ */
};

static const u16 mlxsw_pci_doorbell_arm_type_offset[] = {
	0, /* unused */
	0, /* unused */
	MLXSW_PCI_DOORBELL_ARM_CQ_OFFSET, /* for type MLXSW_PCI_QUEUE_TYPE_CQ */
	MLXSW_PCI_DOORBELL_ARM_EQ_OFFSET, /* for type MLXSW_PCI_QUEUE_TYPE_EQ */
};

struct mlxsw_pci_mem_item {
	char *buf;
	dma_addr_t mapaddr;
	size_t size;
};

struct mlxsw_pci_queue_elem_info {
	char *elem; /* pointer to actual dma mapped element mem chunk */
	union {
		struct {
			struct sk_buff *skb;
		} sdq;
		struct {
			struct sk_buff *skb;
		} rdq;
	} u;
};

struct mlxsw_pci_queue {
	spinlock_t lock; /* for queue accesses */
	struct mlxsw_pci_mem_item mem_item;
	struct mlxsw_pci_queue_elem_info *elem_info;
	u16 producer_counter;
	u16 consumer_counter;
	u16 count; /* number of elements in queue */
	u8 num; /* queue number */
	u8 elem_size; /* size of one element */
	enum mlxsw_pci_queue_type type;
	struct tasklet_struct tasklet; /* queue processing tasklet */
	struct mlxsw_pci *pci;
	union {
		struct {
			u32 comp_sdq_count;
			u32 comp_rdq_count;
		} cq;
		struct {
			u32 ev_cmd_count;
			u32 ev_comp_count;
			u32 ev_other_count;
		} eq;
	} u;
};

struct mlxsw_pci_queue_type_group {
	struct mlxsw_pci_queue *q;
	u8 count; /* number of queues in group */
};

struct mlxsw_pci {
	struct pci_dev *pdev;
	u8 __iomem *hw_addr;
	struct mlxsw_pci_queue_type_group queues[MLXSW_PCI_QUEUE_TYPE_COUNT];
	u32 doorbell_offset;
	struct msix_entry msix_entry;
	struct mlxsw_core *core;
	struct {
		struct mlxsw_pci_mem_item *items;
		unsigned int count;
	} fw_area;
	struct {
		struct mlxsw_pci_mem_item out_mbox;
		struct mlxsw_pci_mem_item in_mbox;
		struct mutex lock; /* Lock access to command registers */
		bool nopoll;
		wait_queue_head_t wait;
		bool wait_done;
		struct {
			u8 status;
			u64 out_param;
		} comp;
	} cmd;
	struct mlxsw_bus_info bus_info;
	struct dentry *dbg_dir;
};

static void mlxsw_pci_queue_tasklet_schedule(struct mlxsw_pci_queue *q)
{
	tasklet_schedule(&q->tasklet);
}

static char *__mlxsw_pci_queue_elem_get(struct mlxsw_pci_queue *q,
					size_t elem_size, int elem_index)
{
	return q->mem_item.buf + (elem_size * elem_index);
}

static struct mlxsw_pci_queue_elem_info *
mlxsw_pci_queue_elem_info_get(struct mlxsw_pci_queue *q, int elem_index)
{
	return &q->elem_info[elem_index];
}

static struct mlxsw_pci_queue_elem_info *
mlxsw_pci_queue_elem_info_producer_get(struct mlxsw_pci_queue *q)
{
	int index = q->producer_counter & (q->count - 1);

	if ((u16) (q->producer_counter - q->consumer_counter) == q->count)
		return NULL;
	return mlxsw_pci_queue_elem_info_get(q, index);
}

static struct mlxsw_pci_queue_elem_info *
mlxsw_pci_queue_elem_info_consumer_get(struct mlxsw_pci_queue *q)
{
	int index = q->consumer_counter & (q->count - 1);

	return mlxsw_pci_queue_elem_info_get(q, index);
}

static char *mlxsw_pci_queue_elem_get(struct mlxsw_pci_queue *q, int elem_index)
{
	return mlxsw_pci_queue_elem_info_get(q, elem_index)->elem;
}

static bool mlxsw_pci_elem_hw_owned(struct mlxsw_pci_queue *q, bool owner_bit)
{
	return owner_bit != !!(q->consumer_counter & q->count);
}

static char *mlxsw_pci_queue_sw_elem_get(struct mlxsw_pci_queue *q,
					 u32 (*get_elem_owner_func)(char *))
{
	struct mlxsw_pci_queue_elem_info *elem_info;
	char *elem;
	bool owner_bit;

	elem_info = mlxsw_pci_queue_elem_info_consumer_get(q);
	elem = elem_info->elem;
	owner_bit = get_elem_owner_func(elem);
	if (mlxsw_pci_elem_hw_owned(q, owner_bit))
		return NULL;
	q->consumer_counter++;
	rmb(); /* make sure we read owned bit before the rest of elem */
	return elem;
}

static struct mlxsw_pci_queue_type_group *
mlxsw_pci_queue_type_group_get(struct mlxsw_pci *mlxsw_pci,
			       enum mlxsw_pci_queue_type q_type)
{
	return &mlxsw_pci->queues[q_type];
}

static u8 __mlxsw_pci_queue_count(struct mlxsw_pci *mlxsw_pci,
				  enum mlxsw_pci_queue_type q_type)
{
	struct mlxsw_pci_queue_type_group *queue_group;

	queue_group = mlxsw_pci_queue_type_group_get(mlxsw_pci, q_type);
	return queue_group->count;
}

static u8 mlxsw_pci_sdq_count(struct mlxsw_pci *mlxsw_pci)
{
	return __mlxsw_pci_queue_count(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_SDQ);
}

static u8 mlxsw_pci_rdq_count(struct mlxsw_pci *mlxsw_pci)
{
	return __mlxsw_pci_queue_count(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_RDQ);
}

static u8 mlxsw_pci_cq_count(struct mlxsw_pci *mlxsw_pci)
{
	return __mlxsw_pci_queue_count(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_CQ);
}

static u8 mlxsw_pci_eq_count(struct mlxsw_pci *mlxsw_pci)
{
	return __mlxsw_pci_queue_count(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_EQ);
}

static struct mlxsw_pci_queue *
__mlxsw_pci_queue_get(struct mlxsw_pci *mlxsw_pci,
		      enum mlxsw_pci_queue_type q_type, u8 q_num)
{
	return &mlxsw_pci->queues[q_type].q[q_num];
}

static struct mlxsw_pci_queue *mlxsw_pci_sdq_get(struct mlxsw_pci *mlxsw_pci,
						 u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci,
				     MLXSW_PCI_QUEUE_TYPE_SDQ, q_num);
}

static struct mlxsw_pci_queue *mlxsw_pci_rdq_get(struct mlxsw_pci *mlxsw_pci,
						 u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci,
				     MLXSW_PCI_QUEUE_TYPE_RDQ, q_num);
}

static struct mlxsw_pci_queue *mlxsw_pci_cq_get(struct mlxsw_pci *mlxsw_pci,
						u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_CQ, q_num);
}

static struct mlxsw_pci_queue *mlxsw_pci_eq_get(struct mlxsw_pci *mlxsw_pci,
						u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_EQ, q_num);
}

static void __mlxsw_pci_queue_doorbell_set(struct mlxsw_pci *mlxsw_pci,
					   struct mlxsw_pci_queue *q,
					   u16 val)
{
	mlxsw_pci_write32(mlxsw_pci,
			  DOORBELL(mlxsw_pci->doorbell_offset,
				   mlxsw_pci_doorbell_type_offset[q->type],
				   q->num), val);
}

static void __mlxsw_pci_queue_doorbell_arm_set(struct mlxsw_pci *mlxsw_pci,
					       struct mlxsw_pci_queue *q,
					       u16 val)
{
	mlxsw_pci_write32(mlxsw_pci,
			  DOORBELL(mlxsw_pci->doorbell_offset,
				   mlxsw_pci_doorbell_arm_type_offset[q->type],
				   q->num), val);
}

static void mlxsw_pci_queue_doorbell_producer_ring(struct mlxsw_pci *mlxsw_pci,
						   struct mlxsw_pci_queue *q)
{
	wmb(); /* ensure all writes are done before we ring a bell */
	__mlxsw_pci_queue_doorbell_set(mlxsw_pci, q, q->producer_counter);
}

static void mlxsw_pci_queue_doorbell_consumer_ring(struct mlxsw_pci *mlxsw_pci,
						   struct mlxsw_pci_queue *q)
{
	wmb(); /* ensure all writes are done before we ring a bell */
	__mlxsw_pci_queue_doorbell_set(mlxsw_pci, q,
				       q->consumer_counter + q->count);
}

static void
mlxsw_pci_queue_doorbell_arm_consumer_ring(struct mlxsw_pci *mlxsw_pci,
					   struct mlxsw_pci_queue *q)
{
	wmb(); /* ensure all writes are done before we ring a bell */
	__mlxsw_pci_queue_doorbell_arm_set(mlxsw_pci, q, q->consumer_counter);
}

static dma_addr_t __mlxsw_pci_queue_page_get(struct mlxsw_pci_queue *q,
					     int page_index)
{
	return q->mem_item.mapaddr + MLXSW_PCI_PAGE_SIZE * page_index;
}

static int mlxsw_pci_sdq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			      struct mlxsw_pci_queue *q)
{
	int i;
	int err;

	q->producer_counter = 0;
	q->consumer_counter = 0;

	/* Set CQ of same number of this SDQ. */
	mlxsw_cmd_mbox_sw2hw_dq_cq_set(mbox, q->num);
	mlxsw_cmd_mbox_sw2hw_dq_sdq_tclass_set(mbox, 3);
	mlxsw_cmd_mbox_sw2hw_dq_log2_dq_sz_set(mbox, 3); /* 8 pages */
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_dq_pa_set(mbox, i, mapaddr);
	}

	err = mlxsw_cmd_sw2hw_sdq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;
	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);
	return 0;
}

static void mlxsw_pci_sdq_fini(struct mlxsw_pci *mlxsw_pci,
			       struct mlxsw_pci_queue *q)
{
	mlxsw_cmd_hw2sw_sdq(mlxsw_pci->core, q->num);
}

static int mlxsw_pci_sdq_dbg_read(struct seq_file *file, void *data)
{
	struct mlxsw_pci *mlxsw_pci = dev_get_drvdata(file->private);
	struct mlxsw_pci_queue *q;
	int i;
	static const char hdr[] =
		"NUM PROD_COUNT CONS_COUNT COUNT\n";

	seq_printf(file, hdr);
	for (i = 0; i < mlxsw_pci_sdq_count(mlxsw_pci); i++) {
		q = mlxsw_pci_sdq_get(mlxsw_pci, i);
		spin_lock_bh(&q->lock);
		seq_printf(file, "%3d %10d %10d %5d\n",
			   i, q->producer_counter, q->consumer_counter,
			   q->count);
		spin_unlock_bh(&q->lock);
	}
	return 0;
}

static int mlxsw_pci_wqe_frag_map(struct mlxsw_pci *mlxsw_pci, char *wqe,
				  int index, char *frag_data, size_t frag_len,
				  int direction)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	dma_addr_t mapaddr;

	mapaddr = pci_map_single(pdev, frag_data, frag_len, direction);
	if (unlikely(pci_dma_mapping_error(pdev, mapaddr))) {
		dev_err_ratelimited(&pdev->dev, "failed to dma map tx frag\n");
		return -EIO;
	}
	mlxsw_pci_wqe_address_set(wqe, index, mapaddr);
	mlxsw_pci_wqe_byte_count_set(wqe, index, frag_len);
	return 0;
}

static void mlxsw_pci_wqe_frag_unmap(struct mlxsw_pci *mlxsw_pci, char *wqe,
				     int index, int direction)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	size_t frag_len = mlxsw_pci_wqe_byte_count_get(wqe, index);
	dma_addr_t mapaddr = mlxsw_pci_wqe_address_get(wqe, index);

	if (!frag_len)
		return;
	pci_unmap_single(pdev, mapaddr, frag_len, direction);
}

static int mlxsw_pci_rdq_skb_alloc(struct mlxsw_pci *mlxsw_pci,
				   struct mlxsw_pci_queue_elem_info *elem_info)
{
	size_t buf_len = MLXSW_PORT_MAX_MTU;
	char *wqe = elem_info->elem;
	struct sk_buff *skb;
	int err;

	elem_info->u.rdq.skb = NULL;
	skb = netdev_alloc_skb_ip_align(NULL, buf_len);
	if (!skb)
		return -ENOMEM;

	/* Assume that wqe was previously zeroed. */

	err = mlxsw_pci_wqe_frag_map(mlxsw_pci, wqe, 0, skb->data,
				     buf_len, DMA_FROM_DEVICE);
	if (err)
		goto err_frag_map;

	elem_info->u.rdq.skb = skb;
	return 0;

err_frag_map:
	dev_kfree_skb_any(skb);
	return err;
}

static void mlxsw_pci_rdq_skb_free(struct mlxsw_pci *mlxsw_pci,
				   struct mlxsw_pci_queue_elem_info *elem_info)
{
	struct sk_buff *skb;
	char *wqe;

	skb = elem_info->u.rdq.skb;
	wqe = elem_info->elem;

	mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, 0, DMA_FROM_DEVICE);
	dev_kfree_skb_any(skb);
}

static int mlxsw_pci_rdq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			      struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_queue_elem_info *elem_info;
	u8 sdq_count = mlxsw_pci_sdq_count(mlxsw_pci);
	int i;
	int err;

	q->producer_counter = 0;
	q->consumer_counter = 0;

	/* Set CQ of same number of this RDQ with base
	 * above SDQ count as the lower ones are assigned to SDQs.
	 */
	mlxsw_cmd_mbox_sw2hw_dq_cq_set(mbox, sdq_count + q->num);
	mlxsw_cmd_mbox_sw2hw_dq_log2_dq_sz_set(mbox, 3); /* 8 pages */
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_dq_pa_set(mbox, i, mapaddr);
	}

	err = mlxsw_cmd_sw2hw_rdq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;

	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);

	for (i = 0; i < q->count; i++) {
		elem_info = mlxsw_pci_queue_elem_info_producer_get(q);
		BUG_ON(!elem_info);
		err = mlxsw_pci_rdq_skb_alloc(mlxsw_pci, elem_info);
		if (err)
			goto rollback;
		/* Everything is set up, ring doorbell to pass elem to HW */
		q->producer_counter++;
		mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);
	}

	return 0;

rollback:
	for (i--; i >= 0; i--) {
		elem_info = mlxsw_pci_queue_elem_info_get(q, i);
		mlxsw_pci_rdq_skb_free(mlxsw_pci, elem_info);
	}
	mlxsw_cmd_hw2sw_rdq(mlxsw_pci->core, q->num);

	return err;
}

static void mlxsw_pci_rdq_fini(struct mlxsw_pci *mlxsw_pci,
			       struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_queue_elem_info *elem_info;
	int i;

	mlxsw_cmd_hw2sw_rdq(mlxsw_pci->core, q->num);
	for (i = 0; i < q->count; i++) {
		elem_info = mlxsw_pci_queue_elem_info_get(q, i);
		mlxsw_pci_rdq_skb_free(mlxsw_pci, elem_info);
	}
}

static int mlxsw_pci_rdq_dbg_read(struct seq_file *file, void *data)
{
	struct mlxsw_pci *mlxsw_pci = dev_get_drvdata(file->private);
	struct mlxsw_pci_queue *q;
	int i;
	static const char hdr[] =
		"NUM PROD_COUNT CONS_COUNT COUNT\n";

	seq_printf(file, hdr);
	for (i = 0; i < mlxsw_pci_rdq_count(mlxsw_pci); i++) {
		q = mlxsw_pci_rdq_get(mlxsw_pci, i);
		spin_lock_bh(&q->lock);
		seq_printf(file, "%3d %10d %10d %5d\n",
			   i, q->producer_counter, q->consumer_counter,
			   q->count);
		spin_unlock_bh(&q->lock);
	}
	return 0;
}

static int mlxsw_pci_cq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			     struct mlxsw_pci_queue *q)
{
	int i;
	int err;

	q->consumer_counter = 0;

	for (i = 0; i < q->count; i++) {
		char *elem = mlxsw_pci_queue_elem_get(q, i);

		mlxsw_pci_cqe_owner_set(elem, 1);
	}

	mlxsw_cmd_mbox_sw2hw_cq_cv_set(mbox, 0); /* CQE ver 0 */
	mlxsw_cmd_mbox_sw2hw_cq_c_eqn_set(mbox, MLXSW_PCI_EQ_COMP_NUM);
	mlxsw_cmd_mbox_sw2hw_cq_oi_set(mbox, 0);
	mlxsw_cmd_mbox_sw2hw_cq_st_set(mbox, 0);
	mlxsw_cmd_mbox_sw2hw_cq_log_cq_size_set(mbox, ilog2(q->count));
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_cq_pa_set(mbox, i, mapaddr);
	}
	err = mlxsw_cmd_sw2hw_cq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;
	mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);
	mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
	return 0;
}

static void mlxsw_pci_cq_fini(struct mlxsw_pci *mlxsw_pci,
			      struct mlxsw_pci_queue *q)
{
	mlxsw_cmd_hw2sw_cq(mlxsw_pci->core, q->num);
}

static int mlxsw_pci_cq_dbg_read(struct seq_file *file, void *data)
{
	struct mlxsw_pci *mlxsw_pci = dev_get_drvdata(file->private);

	struct mlxsw_pci_queue *q;
	int i;
	static const char hdr[] =
		"NUM CONS_INDEX  SDQ_COUNT  RDQ_COUNT COUNT\n";

	seq_printf(file, hdr);
	for (i = 0; i < mlxsw_pci_cq_count(mlxsw_pci); i++) {
		q = mlxsw_pci_cq_get(mlxsw_pci, i);
		spin_lock_bh(&q->lock);
		seq_printf(file, "%3d %10d %10d %10d %5d\n",
			   i, q->consumer_counter, q->u.cq.comp_sdq_count,
			   q->u.cq.comp_rdq_count, q->count);
		spin_unlock_bh(&q->lock);
	}
	return 0;
}

static void mlxsw_pci_cqe_sdq_handle(struct mlxsw_pci *mlxsw_pci,
				     struct mlxsw_pci_queue *q,
				     u16 consumer_counter_limit,
				     char *cqe)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	struct mlxsw_pci_queue_elem_info *elem_info;
	char *wqe;
	struct sk_buff *skb;
	int i;

	spin_lock(&q->lock);
	elem_info = mlxsw_pci_queue_elem_info_consumer_get(q);
	skb = elem_info->u.sdq.skb;
	wqe = elem_info->elem;
	for (i = 0; i < MLXSW_PCI_WQE_SG_ENTRIES; i++)
		mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, i, DMA_TO_DEVICE);
	dev_kfree_skb_any(skb);
	elem_info->u.sdq.skb = NULL;

	if (q->consumer_counter++ != consumer_counter_limit)
		dev_dbg_ratelimited(&pdev->dev, "Consumer counter does not match limit in SDQ\n");
	spin_unlock(&q->lock);
}

static void mlxsw_pci_cqe_rdq_handle(struct mlxsw_pci *mlxsw_pci,
				     struct mlxsw_pci_queue *q,
				     u16 consumer_counter_limit,
				     char *cqe)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	struct mlxsw_pci_queue_elem_info *elem_info;
	char *wqe;
	struct sk_buff *skb;
	struct mlxsw_rx_info rx_info;
	u16 byte_count;
	int err;

	elem_info = mlxsw_pci_queue_elem_info_consumer_get(q);
	skb = elem_info->u.sdq.skb;
	if (!skb)
		return;
	wqe = elem_info->elem;
	mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, 0, DMA_FROM_DEVICE);

	if (q->consumer_counter++ != consumer_counter_limit)
		dev_dbg_ratelimited(&pdev->dev, "Consumer counter does not match limit in RDQ\n");

	if (mlxsw_pci_cqe_lag_get(cqe)) {
		rx_info.is_lag = true;
		rx_info.u.lag_id = mlxsw_pci_cqe_lag_id_get(cqe);
		rx_info.lag_port_index = mlxsw_pci_cqe_lag_port_index_get(cqe);
	} else {
		rx_info.is_lag = false;
		rx_info.u.sys_port = mlxsw_pci_cqe_system_port_get(cqe);
	}

	rx_info.trap_id = mlxsw_pci_cqe_trap_id_get(cqe);

	byte_count = mlxsw_pci_cqe_byte_count_get(cqe);
	if (mlxsw_pci_cqe_crc_get(cqe))
		byte_count -= ETH_FCS_LEN;
	skb_put(skb, byte_count);
	mlxsw_core_skb_receive(mlxsw_pci->core, skb, &rx_info);

	memset(wqe, 0, q->elem_size);
	err = mlxsw_pci_rdq_skb_alloc(mlxsw_pci, elem_info);
	if (err)
		dev_dbg_ratelimited(&pdev->dev, "Failed to alloc skb for RDQ\n");
	/* Everything is set up, ring doorbell to pass elem to HW */
	q->producer_counter++;
	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);
	return;
}

static char *mlxsw_pci_cq_sw_cqe_get(struct mlxsw_pci_queue *q)
{
	return mlxsw_pci_queue_sw_elem_get(q, mlxsw_pci_cqe_owner_get);
}

static void mlxsw_pci_cq_tasklet(unsigned long data)
{
	struct mlxsw_pci_queue *q = (struct mlxsw_pci_queue *) data;
	struct mlxsw_pci *mlxsw_pci = q->pci;
	char *cqe;
	int items = 0;
	int credits = q->count >> 1;

	while ((cqe = mlxsw_pci_cq_sw_cqe_get(q))) {
		u16 wqe_counter = mlxsw_pci_cqe_wqe_counter_get(cqe);
		u8 sendq = mlxsw_pci_cqe_sr_get(cqe);
		u8 dqn = mlxsw_pci_cqe_dqn_get(cqe);

		if (sendq) {
			struct mlxsw_pci_queue *sdq;

			sdq = mlxsw_pci_sdq_get(mlxsw_pci, dqn);
			mlxsw_pci_cqe_sdq_handle(mlxsw_pci, sdq,
						 wqe_counter, cqe);
			q->u.cq.comp_sdq_count++;
		} else {
			struct mlxsw_pci_queue *rdq;

			rdq = mlxsw_pci_rdq_get(mlxsw_pci, dqn);
			mlxsw_pci_cqe_rdq_handle(mlxsw_pci, rdq,
						 wqe_counter, cqe);
			q->u.cq.comp_rdq_count++;
		}
		if (++items == credits)
			break;
	}
	if (items) {
		mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);
		mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
	}
}

static int mlxsw_pci_eq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			     struct mlxsw_pci_queue *q)
{
	int i;
	int err;

	q->consumer_counter = 0;

	for (i = 0; i < q->count; i++) {
		char *elem = mlxsw_pci_queue_elem_get(q, i);

		mlxsw_pci_eqe_owner_set(elem, 1);
	}

	mlxsw_cmd_mbox_sw2hw_eq_int_msix_set(mbox, 1); /* MSI-X used */
	mlxsw_cmd_mbox_sw2hw_eq_oi_set(mbox, 0);
	mlxsw_cmd_mbox_sw2hw_eq_st_set(mbox, 1); /* armed */
	mlxsw_cmd_mbox_sw2hw_eq_log_eq_size_set(mbox, ilog2(q->count));
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_eq_pa_set(mbox, i, mapaddr);
	}
	err = mlxsw_cmd_sw2hw_eq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;
	mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);
	mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
	return 0;
}

static void mlxsw_pci_eq_fini(struct mlxsw_pci *mlxsw_pci,
			      struct mlxsw_pci_queue *q)
{
	mlxsw_cmd_hw2sw_eq(mlxsw_pci->core, q->num);
}

static int mlxsw_pci_eq_dbg_read(struct seq_file *file, void *data)
{
	struct mlxsw_pci *mlxsw_pci = dev_get_drvdata(file->private);
	struct mlxsw_pci_queue *q;
	int i;
	static const char hdr[] =
		"NUM CONS_COUNT     EV_CMD    EV_COMP   EV_OTHER COUNT\n";

	seq_printf(file, hdr);
	for (i = 0; i < mlxsw_pci_eq_count(mlxsw_pci); i++) {
		q = mlxsw_pci_eq_get(mlxsw_pci, i);
		spin_lock_bh(&q->lock);
		seq_printf(file, "%3d %10d %10d %10d %10d %5d\n",
			   i, q->consumer_counter, q->u.eq.ev_cmd_count,
			   q->u.eq.ev_comp_count, q->u.eq.ev_other_count,
			   q->count);
		spin_unlock_bh(&q->lock);
	}
	return 0;
}

static void mlxsw_pci_eq_cmd_event(struct mlxsw_pci *mlxsw_pci, char *eqe)
{
	mlxsw_pci->cmd.comp.status = mlxsw_pci_eqe_cmd_status_get(eqe);
	mlxsw_pci->cmd.comp.out_param =
		((u64) mlxsw_pci_eqe_cmd_out_param_h_get(eqe)) << 32 |
		mlxsw_pci_eqe_cmd_out_param_l_get(eqe);
	mlxsw_pci->cmd.wait_done = true;
	wake_up(&mlxsw_pci->cmd.wait);
}

static char *mlxsw_pci_eq_sw_eqe_get(struct mlxsw_pci_queue *q)
{
	return mlxsw_pci_queue_sw_elem_get(q, mlxsw_pci_eqe_owner_get);
}

static void mlxsw_pci_eq_tasklet(unsigned long data)
{
	struct mlxsw_pci_queue *q = (struct mlxsw_pci_queue *) data;
	struct mlxsw_pci *mlxsw_pci = q->pci;
	u8 cq_count = mlxsw_pci_cq_count(mlxsw_pci);
	unsigned long active_cqns[BITS_TO_LONGS(MLXSW_PCI_CQS_MAX)];
	char *eqe;
	u8 cqn;
	bool cq_handle = false;
	int items = 0;
	int credits = q->count >> 1;

	memset(&active_cqns, 0, sizeof(active_cqns));

	while ((eqe = mlxsw_pci_eq_sw_eqe_get(q))) {
		u8 event_type = mlxsw_pci_eqe_event_type_get(eqe);

		switch (event_type) {
		case MLXSW_PCI_EQE_EVENT_TYPE_CMD:
			mlxsw_pci_eq_cmd_event(mlxsw_pci, eqe);
			q->u.eq.ev_cmd_count++;
			break;
		case MLXSW_PCI_EQE_EVENT_TYPE_COMP:
			cqn = mlxsw_pci_eqe_cqn_get(eqe);
			set_bit(cqn, active_cqns);
			cq_handle = true;
			q->u.eq.ev_comp_count++;
			break;
		default:
			q->u.eq.ev_other_count++;
		}
		if (++items == credits)
			break;
	}
	if (items) {
		mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);
		mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
	}

	if (!cq_handle)
		return;
	for_each_set_bit(cqn, active_cqns, cq_count) {
		q = mlxsw_pci_cq_get(mlxsw_pci, cqn);
		mlxsw_pci_queue_tasklet_schedule(q);
	}
}

struct mlxsw_pci_queue_ops {
	const char *name;
	enum mlxsw_pci_queue_type type;
	int (*init)(struct mlxsw_pci *mlxsw_pci, char *mbox,
		    struct mlxsw_pci_queue *q);
	void (*fini)(struct mlxsw_pci *mlxsw_pci,
		     struct mlxsw_pci_queue *q);
	void (*tasklet)(unsigned long data);
	int (*dbg_read)(struct seq_file *s, void *data);
	u16 elem_count;
	u8 elem_size;
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_sdq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_SDQ,
	.init		= mlxsw_pci_sdq_init,
	.fini		= mlxsw_pci_sdq_fini,
	.dbg_read	= mlxsw_pci_sdq_dbg_read,
	.elem_count	= MLXSW_PCI_WQE_COUNT,
	.elem_size	= MLXSW_PCI_WQE_SIZE,
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_rdq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_RDQ,
	.init		= mlxsw_pci_rdq_init,
	.fini		= mlxsw_pci_rdq_fini,
	.dbg_read	= mlxsw_pci_rdq_dbg_read,
	.elem_count	= MLXSW_PCI_WQE_COUNT,
	.elem_size	= MLXSW_PCI_WQE_SIZE
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_cq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_CQ,
	.init		= mlxsw_pci_cq_init,
	.fini		= mlxsw_pci_cq_fini,
	.tasklet	= mlxsw_pci_cq_tasklet,
	.dbg_read	= mlxsw_pci_cq_dbg_read,
	.elem_count	= MLXSW_PCI_CQE_COUNT,
	.elem_size	= MLXSW_PCI_CQE_SIZE
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_eq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_EQ,
	.init		= mlxsw_pci_eq_init,
	.fini		= mlxsw_pci_eq_fini,
	.tasklet	= mlxsw_pci_eq_tasklet,
	.dbg_read	= mlxsw_pci_eq_dbg_read,
	.elem_count	= MLXSW_PCI_EQE_COUNT,
	.elem_size	= MLXSW_PCI_EQE_SIZE
};

static int mlxsw_pci_queue_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
				const struct mlxsw_pci_queue_ops *q_ops,
				struct mlxsw_pci_queue *q, u8 q_num)
{
	struct mlxsw_pci_mem_item *mem_item = &q->mem_item;
	int i;
	int err;

	spin_lock_init(&q->lock);
	q->num = q_num;
	q->count = q_ops->elem_count;
	q->elem_size = q_ops->elem_size;
	q->type = q_ops->type;
	q->pci = mlxsw_pci;

	if (q_ops->tasklet)
		tasklet_init(&q->tasklet, q_ops->tasklet, (unsigned long) q);

	mem_item->size = MLXSW_PCI_AQ_SIZE;
	mem_item->buf = pci_alloc_consistent(mlxsw_pci->pdev,
					     mem_item->size,
					     &mem_item->mapaddr);
	if (!mem_item->buf)
		return -ENOMEM;
	memset(mem_item->buf, 0, mem_item->size);

	q->elem_info = kcalloc(q->count, sizeof(*q->elem_info), GFP_KERNEL);
	if (!q->elem_info) {
		err = -ENOMEM;
		goto err_elem_info_alloc;
	}

	/* Initialize dma mapped elements info elem_info for
	 * future easy access.
	 */
	for (i = 0; i < q->count; i++) {
		struct mlxsw_pci_queue_elem_info *elem_info;

		elem_info = mlxsw_pci_queue_elem_info_get(q, i);
		elem_info->elem =
			__mlxsw_pci_queue_elem_get(q, q_ops->elem_size, i);
	}

	mlxsw_cmd_mbox_zero(mbox);
	err = q_ops->init(mlxsw_pci, mbox, q);
	if (err)
		goto err_q_ops_init;
	return 0;

err_q_ops_init:
	kfree(q->elem_info);
err_elem_info_alloc:
	pci_free_consistent(mlxsw_pci->pdev, mem_item->size,
			    mem_item->buf, mem_item->mapaddr);
	return err;
}

static void mlxsw_pci_queue_fini(struct mlxsw_pci *mlxsw_pci,
				 const struct mlxsw_pci_queue_ops *q_ops,
				 struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_mem_item *mem_item = &q->mem_item;

	q_ops->fini(mlxsw_pci, q);
	kfree(q->elem_info);
	pci_free_consistent(mlxsw_pci->pdev, mem_item->size,
			    mem_item->buf, mem_item->mapaddr);
}

static int mlxsw_pci_queue_group_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
				      const struct mlxsw_pci_queue_ops *q_ops,
				      u8 num_qs)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	struct mlxsw_pci_queue_type_group *queue_group;
	char tmp[16];
	int i;
	int err;

	queue_group = mlxsw_pci_queue_type_group_get(mlxsw_pci, q_ops->type);
	queue_group->q = kcalloc(num_qs, sizeof(*queue_group->q), GFP_KERNEL);
	if (!queue_group->q)
		return -ENOMEM;

	for (i = 0; i < num_qs; i++) {
		err = mlxsw_pci_queue_init(mlxsw_pci, mbox, q_ops,
					   &queue_group->q[i], i);
		if (err)
			goto err_queue_init;
	}
	queue_group->count = num_qs;

	sprintf(tmp, "%s_stats", mlxsw_pci_queue_type_str(q_ops->type));
	debugfs_create_devm_seqfile(&pdev->dev, tmp, mlxsw_pci->dbg_dir,
				    q_ops->dbg_read);

	return 0;

err_queue_init:
	for (i--; i >= 0; i--)
		mlxsw_pci_queue_fini(mlxsw_pci, q_ops, &queue_group->q[i]);
	kfree(queue_group->q);
	return err;
}

static void mlxsw_pci_queue_group_fini(struct mlxsw_pci *mlxsw_pci,
				       const struct mlxsw_pci_queue_ops *q_ops)
{
	struct mlxsw_pci_queue_type_group *queue_group;
	int i;

	queue_group = mlxsw_pci_queue_type_group_get(mlxsw_pci, q_ops->type);
	for (i = 0; i < queue_group->count; i++)
		mlxsw_pci_queue_fini(mlxsw_pci, q_ops, &queue_group->q[i]);
	kfree(queue_group->q);
}

static int mlxsw_pci_aqs_init(struct mlxsw_pci *mlxsw_pci, char *mbox)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	u8 num_sdqs;
	u8 sdq_log2sz;
	u8 num_rdqs;
	u8 rdq_log2sz;
	u8 num_cqs;
	u8 cq_log2sz;
	u8 num_eqs;
	u8 eq_log2sz;
	int err;

	mlxsw_cmd_mbox_zero(mbox);
	err = mlxsw_cmd_query_aq_cap(mlxsw_pci->core, mbox);
	if (err)
		return err;

	num_sdqs = mlxsw_cmd_mbox_query_aq_cap_max_num_sdqs_get(mbox);
	sdq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_sdq_sz_get(mbox);
	num_rdqs = mlxsw_cmd_mbox_query_aq_cap_max_num_rdqs_get(mbox);
	rdq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_rdq_sz_get(mbox);
	num_cqs = mlxsw_cmd_mbox_query_aq_cap_max_num_cqs_get(mbox);
	cq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_cq_sz_get(mbox);
	num_eqs = mlxsw_cmd_mbox_query_aq_cap_max_num_eqs_get(mbox);
	eq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_eq_sz_get(mbox);

	if (num_sdqs + num_rdqs > num_cqs ||
	    num_cqs > MLXSW_PCI_CQS_MAX || num_eqs != MLXSW_PCI_EQS_COUNT) {
		dev_err(&pdev->dev, "Unsupported number of queues\n");
		return -EINVAL;
	}

	if ((1 << sdq_log2sz != MLXSW_PCI_WQE_COUNT) ||
	    (1 << rdq_log2sz != MLXSW_PCI_WQE_COUNT) ||
	    (1 << cq_log2sz != MLXSW_PCI_CQE_COUNT) ||
	    (1 << eq_log2sz != MLXSW_PCI_EQE_COUNT)) {
		dev_err(&pdev->dev, "Unsupported number of async queue descriptors\n");
		return -EINVAL;
	}

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_eq_ops,
					 num_eqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize event queues\n");
		return err;
	}

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_cq_ops,
					 num_cqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize completion queues\n");
		goto err_cqs_init;
	}

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_sdq_ops,
					 num_sdqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize send descriptor queues\n");
		goto err_sdqs_init;
	}

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_rdq_ops,
					 num_rdqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize receive descriptor queues\n");
		goto err_rdqs_init;
	}

	/* We have to poll in command interface until queues are initialized */
	mlxsw_pci->cmd.nopoll = true;
	return 0;

err_rdqs_init:
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_sdq_ops);
err_sdqs_init:
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_cq_ops);
err_cqs_init:
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_eq_ops);
	return err;
}

static void mlxsw_pci_aqs_fini(struct mlxsw_pci *mlxsw_pci)
{
	mlxsw_pci->cmd.nopoll = false;
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_rdq_ops);
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_sdq_ops);
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_cq_ops);
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_eq_ops);
}

static void
mlxsw_pci_config_profile_swid_config(struct mlxsw_pci *mlxsw_pci,
				     char *mbox, int index,
				     const struct mlxsw_swid_config *swid)
{
	u8 mask = 0;

	if (swid->used_type) {
		mlxsw_cmd_mbox_config_profile_swid_config_type_set(
			mbox, index, swid->type);
		mask |= 1;
	}
	if (swid->used_properties) {
		mlxsw_cmd_mbox_config_profile_swid_config_properties_set(
			mbox, index, swid->properties);
		mask |= 2;
	}
	mlxsw_cmd_mbox_config_profile_swid_config_mask_set(mbox, index, mask);
}

#define MLXSW_RESOURCES_TABLE_END_ID 0xffff
#define MLXSW_MAX_SPAN_ID 0x2420
#define MLXSW_MAX_LAG_ID 0x2520
#define MLXSW_MAX_PORTS_IN_LAG_ID 0x2521
#define MLXSW_KVD_SIZE_ID 0x1001
#define MLXSW_KVD_SINGLE_MIN_SIZE_ID 0x1002
#define MLXSW_KVD_DOUBLE_MIN_SIZE_ID 0x1003
#define MLXSW_MAX_VIRTUAL_ROUTERS_ID 0x2C01
#define MLXSW_MAX_SYSTEM_PORT_ID 0x2502
#define MLXSW_MAX_VLAN_GROUPS_ID 0x2906
#define MLXSW_MAX_REGIONS_ID 0x2901
#define MLXSW_MAX_RIF_ID 0x2C02
#define MLXSW_RESOURCES_QUERY_MAX_QUERIES 100
#define MLXSW_RESOURCES_PER_QUERY 32

static void mlxsw_pci_resources_query_parse(int id, u64 val,
					    struct mlxsw_resources *resources)
{
	switch (id) {
	case MLXSW_MAX_SPAN_ID:
		resources->max_span = val;
		resources->max_span_valid = 1;
		break;
	case MLXSW_MAX_LAG_ID:
		resources->max_lag = val;
		resources->max_lag_valid = 1;
		break;
	case MLXSW_MAX_PORTS_IN_LAG_ID:
		resources->max_ports_in_lag = val;
		resources->max_ports_in_lag_valid = 1;
		break;
	case MLXSW_KVD_SIZE_ID:
		resources->kvd_size = val;
		resources->kvd_size_valid = 1;
		break;
	case MLXSW_KVD_SINGLE_MIN_SIZE_ID:
		resources->kvd_single_min_size = val;
		resources->kvd_single_min_size_valid = 1;
		break;
	case MLXSW_KVD_DOUBLE_MIN_SIZE_ID:
		resources->kvd_double_min_size = val;
		resources->kvd_double_min_size_valid = 1;
		break;
	case MLXSW_MAX_VIRTUAL_ROUTERS_ID:
		resources->max_virtual_routers = val;
		resources->max_virtual_routers_valid = 1;
		break;
	case MLXSW_MAX_SYSTEM_PORT_ID:
		resources->max_system_ports = val;
		resources->max_system_ports_valid = 1;
		break;
	case MLXSW_MAX_VLAN_GROUPS_ID:
		resources->max_vlan_groups = val;
		resources->max_vlan_groups_valid = 1;
		break;
	case MLXSW_MAX_REGIONS_ID:
		resources->max_regions = val;
		resources->max_regions_valid = 1;
		break;
	case MLXSW_MAX_RIF_ID:
		resources->max_rif = val;
		resources->max_rif_valid = 1;
		break;
	default:
		break;
	}
}

static int mlxsw_pci_resources_query(struct mlxsw_pci *mlxsw_pci, char *mbox,
				     struct mlxsw_resources *resources,
				     u8 query_enabled)
{
	int index, i;
	u64 data;
	u16 id;
	int err;

	/* Not all the versions support resources query */
	if (!query_enabled)
		return 0;

	mlxsw_cmd_mbox_zero(mbox);

	for (index = 0; index < MLXSW_RESOURCES_QUERY_MAX_QUERIES; index++) {
		err = mlxsw_cmd_query_resources(mlxsw_pci->core, mbox, index);
		if (err)
			return err;

		for (i = 0; i < MLXSW_RESOURCES_PER_QUERY; i++) {
			id = mlxsw_cmd_mbox_query_resource_id_get(mbox, i);
			data = mlxsw_cmd_mbox_query_resource_data_get(mbox, i);

			if (id == MLXSW_RESOURCES_TABLE_END_ID)
				return 0;

			mlxsw_pci_resources_query_parse(id, data, resources);
		}
	}

	/* If after MLXSW_RESOURCES_QUERY_MAX_QUERIES we still didn't get
	 * MLXSW_RESOURCES_TABLE_END_ID, something went bad in the FW.
	 */
	return -EIO;
}

static int mlxsw_pci_profile_get_kvd_sizes(const struct mlxsw_config_profile *profile,
					   struct mlxsw_resources *resources)
{
	u32 singles_size, doubles_size, linear_size;

	if (!resources->kvd_single_min_size_valid ||
	    !resources->kvd_double_min_size_valid ||
	    !profile->used_kvd_split_data)
		return -EIO;

	linear_size = profile->kvd_linear_size;

	/* The hash part is what left of the kvd without the
	 * linear part. It is split to the single size and
	 * double size by the parts ratio from the profile.
	 * Both sizes must be a multiplications of the
	 * granularity from the profile.
	 */
	doubles_size = (resources->kvd_size - linear_size);
	doubles_size *= profile->kvd_hash_double_parts;
	doubles_size /= (profile->kvd_hash_double_parts +
			 profile->kvd_hash_single_parts);
	doubles_size /= profile->kvd_hash_granularity;
	doubles_size *= profile->kvd_hash_granularity;
	singles_size = resources->kvd_size - doubles_size -
		       linear_size;

	/* Check results are legal. */
	if (singles_size < resources->kvd_single_min_size ||
	    doubles_size < resources->kvd_double_min_size ||
	    resources->kvd_size < linear_size)
		return -EIO;

	resources->kvd_single_size = singles_size;
	resources->kvd_double_size = doubles_size;
	resources->kvd_linear_size = linear_size;

	return 0;
}

static int mlxsw_pci_config_profile(struct mlxsw_pci *mlxsw_pci, char *mbox,
				    const struct mlxsw_config_profile *profile,
				    struct mlxsw_resources *resources)
{
	int i;
	int err;

	mlxsw_cmd_mbox_zero(mbox);

	if (profile->used_max_vepa_channels) {
		mlxsw_cmd_mbox_config_profile_set_max_vepa_channels_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_vepa_channels_set(
			mbox, profile->max_vepa_channels);
	}
	if (profile->used_max_mid) {
		mlxsw_cmd_mbox_config_profile_set_max_mid_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_mid_set(
			mbox, profile->max_mid);
	}
	if (profile->used_max_pgt) {
		mlxsw_cmd_mbox_config_profile_set_max_pgt_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_pgt_set(
			mbox, profile->max_pgt);
	}
	if (profile->used_max_system_port) {
		mlxsw_cmd_mbox_config_profile_set_max_system_port_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_system_port_set(
			mbox, profile->max_system_port);
	}
	if (profile->used_max_vlan_groups) {
		mlxsw_cmd_mbox_config_profile_set_max_vlan_groups_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_vlan_groups_set(
			mbox, profile->max_vlan_groups);
	}
	if (profile->used_max_regions) {
		mlxsw_cmd_mbox_config_profile_set_max_regions_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_regions_set(
			mbox, profile->max_regions);
	}
	if (profile->used_flood_tables) {
		mlxsw_cmd_mbox_config_profile_set_flood_tables_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_flood_tables_set(
			mbox, profile->max_flood_tables);
		mlxsw_cmd_mbox_config_profile_max_vid_flood_tables_set(
			mbox, profile->max_vid_flood_tables);
		mlxsw_cmd_mbox_config_profile_max_fid_offset_flood_tables_set(
			mbox, profile->max_fid_offset_flood_tables);
		mlxsw_cmd_mbox_config_profile_fid_offset_flood_table_size_set(
			mbox, profile->fid_offset_flood_table_size);
		mlxsw_cmd_mbox_config_profile_max_fid_flood_tables_set(
			mbox, profile->max_fid_flood_tables);
		mlxsw_cmd_mbox_config_profile_fid_flood_table_size_set(
			mbox, profile->fid_flood_table_size);
	}
	if (profile->used_flood_mode) {
		mlxsw_cmd_mbox_config_profile_set_flood_mode_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_flood_mode_set(
			mbox, profile->flood_mode);
	}
	if (profile->used_max_ib_mc) {
		mlxsw_cmd_mbox_config_profile_set_max_ib_mc_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_ib_mc_set(
			mbox, profile->max_ib_mc);
	}
	if (profile->used_max_pkey) {
		mlxsw_cmd_mbox_config_profile_set_max_pkey_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_pkey_set(
			mbox, profile->max_pkey);
	}
	if (profile->used_ar_sec) {
		mlxsw_cmd_mbox_config_profile_set_ar_sec_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_ar_sec_set(
			mbox, profile->ar_sec);
	}
	if (profile->used_adaptive_routing_group_cap) {
		mlxsw_cmd_mbox_config_profile_set_adaptive_routing_group_cap_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_adaptive_routing_group_cap_set(
			mbox, profile->adaptive_routing_group_cap);
	}
	if (resources->kvd_size_valid) {
		err = mlxsw_pci_profile_get_kvd_sizes(profile, resources);
		if (err)
			return err;

		mlxsw_cmd_mbox_config_profile_set_kvd_linear_size_set(mbox, 1);
		mlxsw_cmd_mbox_config_profile_kvd_linear_size_set(mbox,
						resources->kvd_linear_size);
		mlxsw_cmd_mbox_config_profile_set_kvd_hash_single_size_set(mbox,
									   1);
		mlxsw_cmd_mbox_config_profile_kvd_hash_single_size_set(mbox,
						resources->kvd_single_size);
		mlxsw_cmd_mbox_config_profile_set_kvd_hash_double_size_set(
								mbox, 1);
		mlxsw_cmd_mbox_config_profile_kvd_hash_double_size_set(mbox,
						resources->kvd_double_size);
	}

	for (i = 0; i < MLXSW_CONFIG_PROFILE_SWID_COUNT; i++)
		mlxsw_pci_config_profile_swid_config(mlxsw_pci, mbox, i,
						     &profile->swid_config[i]);

	return mlxsw_cmd_config_profile_set(mlxsw_pci->core, mbox);
}

static int mlxsw_pci_boardinfo(struct mlxsw_pci *mlxsw_pci, char *mbox)
{
	struct mlxsw_bus_info *bus_info = &mlxsw_pci->bus_info;
	int err;

	mlxsw_cmd_mbox_zero(mbox);
	err = mlxsw_cmd_boardinfo(mlxsw_pci->core, mbox);
	if (err)
		return err;
	mlxsw_cmd_mbox_boardinfo_vsd_memcpy_from(mbox, bus_info->vsd);
	mlxsw_cmd_mbox_boardinfo_psid_memcpy_from(mbox, bus_info->psid);
	return 0;
}

static int mlxsw_pci_fw_area_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
				  u16 num_pages)
{
	struct mlxsw_pci_mem_item *mem_item;
	int nent = 0;
	int i;
	int err;

	mlxsw_pci->fw_area.items = kcalloc(num_pages, sizeof(*mem_item),
					   GFP_KERNEL);
	if (!mlxsw_pci->fw_area.items)
		return -ENOMEM;
	mlxsw_pci->fw_area.count = num_pages;

	mlxsw_cmd_mbox_zero(mbox);
	for (i = 0; i < num_pages; i++) {
		mem_item = &mlxsw_pci->fw_area.items[i];

		mem_item->size = MLXSW_PCI_PAGE_SIZE;
		mem_item->buf = pci_alloc_consistent(mlxsw_pci->pdev,
						     mem_item->size,
						     &mem_item->mapaddr);
		if (!mem_item->buf) {
			err = -ENOMEM;
			goto err_alloc;
		}
		mlxsw_cmd_mbox_map_fa_pa_set(mbox, nent, mem_item->mapaddr);
		mlxsw_cmd_mbox_map_fa_log2size_set(mbox, nent, 0); /* 1 page */
		if (++nent == MLXSW_CMD_MAP_FA_VPM_ENTRIES_MAX) {
			err = mlxsw_cmd_map_fa(mlxsw_pci->core, mbox, nent);
			if (err)
				goto err_cmd_map_fa;
			nent = 0;
			mlxsw_cmd_mbox_zero(mbox);
		}
	}

	if (nent) {
		err = mlxsw_cmd_map_fa(mlxsw_pci->core, mbox, nent);
		if (err)
			goto err_cmd_map_fa;
	}

	return 0;

err_cmd_map_fa:
err_alloc:
	for (i--; i >= 0; i--) {
		mem_item = &mlxsw_pci->fw_area.items[i];

		pci_free_consistent(mlxsw_pci->pdev, mem_item->size,
				    mem_item->buf, mem_item->mapaddr);
	}
	kfree(mlxsw_pci->fw_area.items);
	return err;
}

static void mlxsw_pci_fw_area_fini(struct mlxsw_pci *mlxsw_pci)
{
	struct mlxsw_pci_mem_item *mem_item;
	int i;

	mlxsw_cmd_unmap_fa(mlxsw_pci->core);

	for (i = 0; i < mlxsw_pci->fw_area.count; i++) {
		mem_item = &mlxsw_pci->fw_area.items[i];

		pci_free_consistent(mlxsw_pci->pdev, mem_item->size,
				    mem_item->buf, mem_item->mapaddr);
	}
	kfree(mlxsw_pci->fw_area.items);
}

static irqreturn_t mlxsw_pci_eq_irq_handler(int irq, void *dev_id)
{
	struct mlxsw_pci *mlxsw_pci = dev_id;
	struct mlxsw_pci_queue *q;
	int i;

	for (i = 0; i < MLXSW_PCI_EQS_COUNT; i++) {
		q = mlxsw_pci_eq_get(mlxsw_pci, i);
		mlxsw_pci_queue_tasklet_schedule(q);
	}
	return IRQ_HANDLED;
}

static int mlxsw_pci_mbox_alloc(struct mlxsw_pci *mlxsw_pci,
				struct mlxsw_pci_mem_item *mbox)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	int err = 0;

	mbox->size = MLXSW_CMD_MBOX_SIZE;
	mbox->buf = pci_alloc_consistent(pdev, MLXSW_CMD_MBOX_SIZE,
					 &mbox->mapaddr);
	if (!mbox->buf) {
		dev_err(&pdev->dev, "Failed allocating memory for mailbox\n");
		err = -ENOMEM;
	}

	return err;
}

static void mlxsw_pci_mbox_free(struct mlxsw_pci *mlxsw_pci,
				struct mlxsw_pci_mem_item *mbox)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;

	pci_free_consistent(pdev, MLXSW_CMD_MBOX_SIZE, mbox->buf,
			    mbox->mapaddr);
}

static int mlxsw_pci_init(void *bus_priv, struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_config_profile *profile,
			  struct mlxsw_resources *resources)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	struct pci_dev *pdev = mlxsw_pci->pdev;
	char *mbox;
	u16 num_pages;
	int err;

	mutex_init(&mlxsw_pci->cmd.lock);
	init_waitqueue_head(&mlxsw_pci->cmd.wait);

	mlxsw_pci->core = mlxsw_core;

	mbox = mlxsw_cmd_mbox_alloc();
	if (!mbox)
		return -ENOMEM;

	err = mlxsw_pci_mbox_alloc(mlxsw_pci, &mlxsw_pci->cmd.in_mbox);
	if (err)
		goto mbox_put;

	err = mlxsw_pci_mbox_alloc(mlxsw_pci, &mlxsw_pci->cmd.out_mbox);
	if (err)
		goto err_out_mbox_alloc;

	err = mlxsw_cmd_query_fw(mlxsw_core, mbox);
	if (err)
		goto err_query_fw;

	mlxsw_pci->bus_info.fw_rev.major =
		mlxsw_cmd_mbox_query_fw_fw_rev_major_get(mbox);
	mlxsw_pci->bus_info.fw_rev.minor =
		mlxsw_cmd_mbox_query_fw_fw_rev_minor_get(mbox);
	mlxsw_pci->bus_info.fw_rev.subminor =
		mlxsw_cmd_mbox_query_fw_fw_rev_subminor_get(mbox);

	if (mlxsw_cmd_mbox_query_fw_cmd_interface_rev_get(mbox) != 1) {
		dev_err(&pdev->dev, "Unsupported cmd interface revision ID queried from hw\n");
		err = -EINVAL;
		goto err_iface_rev;
	}
	if (mlxsw_cmd_mbox_query_fw_doorbell_page_bar_get(mbox) != 0) {
		dev_err(&pdev->dev, "Unsupported doorbell page bar queried from hw\n");
		err = -EINVAL;
		goto err_doorbell_page_bar;
	}

	mlxsw_pci->doorbell_offset =
		mlxsw_cmd_mbox_query_fw_doorbell_page_offset_get(mbox);

	num_pages = mlxsw_cmd_mbox_query_fw_fw_pages_get(mbox);
	err = mlxsw_pci_fw_area_init(mlxsw_pci, mbox, num_pages);
	if (err)
		goto err_fw_area_init;

	err = mlxsw_pci_boardinfo(mlxsw_pci, mbox);
	if (err)
		goto err_boardinfo;

	err = mlxsw_pci_resources_query(mlxsw_pci, mbox, resources,
					profile->resource_query_enable);
	if (err)
		goto err_query_resources;

	err = mlxsw_pci_config_profile(mlxsw_pci, mbox, profile, resources);
	if (err)
		goto err_config_profile;

	err = mlxsw_pci_aqs_init(mlxsw_pci, mbox);
	if (err)
		goto err_aqs_init;

	err = request_irq(mlxsw_pci->msix_entry.vector,
			  mlxsw_pci_eq_irq_handler, 0,
			  mlxsw_pci_driver_name, mlxsw_pci);
	if (err) {
		dev_err(&pdev->dev, "IRQ request failed\n");
		goto err_request_eq_irq;
	}

	goto mbox_put;

err_request_eq_irq:
	mlxsw_pci_aqs_fini(mlxsw_pci);
err_aqs_init:
err_config_profile:
err_query_resources:
err_boardinfo:
	mlxsw_pci_fw_area_fini(mlxsw_pci);
err_fw_area_init:
err_doorbell_page_bar:
err_iface_rev:
err_query_fw:
	mlxsw_pci_mbox_free(mlxsw_pci, &mlxsw_pci->cmd.out_mbox);
err_out_mbox_alloc:
	mlxsw_pci_mbox_free(mlxsw_pci, &mlxsw_pci->cmd.in_mbox);
mbox_put:
	mlxsw_cmd_mbox_free(mbox);
	return err;
}

static void mlxsw_pci_fini(void *bus_priv)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;

	free_irq(mlxsw_pci->msix_entry.vector, mlxsw_pci);
	mlxsw_pci_aqs_fini(mlxsw_pci);
	mlxsw_pci_fw_area_fini(mlxsw_pci);
	mlxsw_pci_mbox_free(mlxsw_pci, &mlxsw_pci->cmd.out_mbox);
	mlxsw_pci_mbox_free(mlxsw_pci, &mlxsw_pci->cmd.in_mbox);
}

static struct mlxsw_pci_queue *
mlxsw_pci_sdq_pick(struct mlxsw_pci *mlxsw_pci,
		   const struct mlxsw_tx_info *tx_info)
{
	u8 sdqn = tx_info->local_port % mlxsw_pci_sdq_count(mlxsw_pci);

	return mlxsw_pci_sdq_get(mlxsw_pci, sdqn);
}

static bool mlxsw_pci_skb_transmit_busy(void *bus_priv,
					const struct mlxsw_tx_info *tx_info)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	struct mlxsw_pci_queue *q = mlxsw_pci_sdq_pick(mlxsw_pci, tx_info);

	return !mlxsw_pci_queue_elem_info_producer_get(q);
}

static int mlxsw_pci_skb_transmit(void *bus_priv, struct sk_buff *skb,
				  const struct mlxsw_tx_info *tx_info)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	struct mlxsw_pci_queue *q;
	struct mlxsw_pci_queue_elem_info *elem_info;
	char *wqe;
	int i;
	int err;

	if (skb_shinfo(skb)->nr_frags > MLXSW_PCI_WQE_SG_ENTRIES - 1) {
		err = skb_linearize(skb);
		if (err)
			return err;
	}

	q = mlxsw_pci_sdq_pick(mlxsw_pci, tx_info);
	spin_lock_bh(&q->lock);
	elem_info = mlxsw_pci_queue_elem_info_producer_get(q);
	if (!elem_info) {
		/* queue is full */
		err = -EAGAIN;
		goto unlock;
	}
	elem_info->u.sdq.skb = skb;

	wqe = elem_info->elem;
	mlxsw_pci_wqe_c_set(wqe, 1); /* always report completion */
	mlxsw_pci_wqe_lp_set(wqe, !!tx_info->is_emad);
	mlxsw_pci_wqe_type_set(wqe, MLXSW_PCI_WQE_TYPE_ETHERNET);

	err = mlxsw_pci_wqe_frag_map(mlxsw_pci, wqe, 0, skb->data,
				     skb_headlen(skb), DMA_TO_DEVICE);
	if (err)
		goto unlock;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		err = mlxsw_pci_wqe_frag_map(mlxsw_pci, wqe, i + 1,
					     skb_frag_address(frag),
					     skb_frag_size(frag),
					     DMA_TO_DEVICE);
		if (err)
			goto unmap_frags;
	}

	/* Set unused sq entries byte count to zero. */
	for (i++; i < MLXSW_PCI_WQE_SG_ENTRIES; i++)
		mlxsw_pci_wqe_byte_count_set(wqe, i, 0);

	/* Everything is set up, ring producer doorbell to get HW going */
	q->producer_counter++;
	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);

	goto unlock;

unmap_frags:
	for (; i >= 0; i--)
		mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, i, DMA_TO_DEVICE);
unlock:
	spin_unlock_bh(&q->lock);
	return err;
}

static int mlxsw_pci_cmd_exec(void *bus_priv, u16 opcode, u8 opcode_mod,
			      u32 in_mod, bool out_mbox_direct,
			      char *in_mbox, size_t in_mbox_size,
			      char *out_mbox, size_t out_mbox_size,
			      u8 *p_status)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	dma_addr_t in_mapaddr = mlxsw_pci->cmd.in_mbox.mapaddr;
	dma_addr_t out_mapaddr = mlxsw_pci->cmd.out_mbox.mapaddr;
	bool evreq = mlxsw_pci->cmd.nopoll;
	unsigned long timeout = msecs_to_jiffies(MLXSW_PCI_CIR_TIMEOUT_MSECS);
	bool *p_wait_done = &mlxsw_pci->cmd.wait_done;
	int err;

	*p_status = MLXSW_CMD_STATUS_OK;

	err = mutex_lock_interruptible(&mlxsw_pci->cmd.lock);
	if (err)
		return err;

	if (in_mbox)
		memcpy(mlxsw_pci->cmd.in_mbox.buf, in_mbox, in_mbox_size);
	mlxsw_pci_write32(mlxsw_pci, CIR_IN_PARAM_HI, upper_32_bits(in_mapaddr));
	mlxsw_pci_write32(mlxsw_pci, CIR_IN_PARAM_LO, lower_32_bits(in_mapaddr));

	mlxsw_pci_write32(mlxsw_pci, CIR_OUT_PARAM_HI, upper_32_bits(out_mapaddr));
	mlxsw_pci_write32(mlxsw_pci, CIR_OUT_PARAM_LO, lower_32_bits(out_mapaddr));

	mlxsw_pci_write32(mlxsw_pci, CIR_IN_MODIFIER, in_mod);
	mlxsw_pci_write32(mlxsw_pci, CIR_TOKEN, 0);

	*p_wait_done = false;

	wmb(); /* all needs to be written before we write control register */
	mlxsw_pci_write32(mlxsw_pci, CIR_CTRL,
			  MLXSW_PCI_CIR_CTRL_GO_BIT |
			  (evreq ? MLXSW_PCI_CIR_CTRL_EVREQ_BIT : 0) |
			  (opcode_mod << MLXSW_PCI_CIR_CTRL_OPCODE_MOD_SHIFT) |
			  opcode);

	if (!evreq) {
		unsigned long end;

		end = jiffies + timeout;
		do {
			u32 ctrl = mlxsw_pci_read32(mlxsw_pci, CIR_CTRL);

			if (!(ctrl & MLXSW_PCI_CIR_CTRL_GO_BIT)) {
				*p_wait_done = true;
				*p_status = ctrl >> MLXSW_PCI_CIR_CTRL_STATUS_SHIFT;
				break;
			}
			cond_resched();
		} while (time_before(jiffies, end));
	} else {
		wait_event_timeout(mlxsw_pci->cmd.wait, *p_wait_done, timeout);
		*p_status = mlxsw_pci->cmd.comp.status;
	}

	err = 0;
	if (*p_wait_done) {
		if (*p_status)
			err = -EIO;
	} else {
		err = -ETIMEDOUT;
	}

	if (!err && out_mbox && out_mbox_direct) {
		/* Some commands don't use output param as address to mailbox
		 * but they store output directly into registers. In that case,
		 * copy registers into mbox buffer.
		 */
		__be32 tmp;

		if (!evreq) {
			tmp = cpu_to_be32(mlxsw_pci_read32(mlxsw_pci,
							   CIR_OUT_PARAM_HI));
			memcpy(out_mbox, &tmp, sizeof(tmp));
			tmp = cpu_to_be32(mlxsw_pci_read32(mlxsw_pci,
							   CIR_OUT_PARAM_LO));
			memcpy(out_mbox + sizeof(tmp), &tmp, sizeof(tmp));
		}
	} else if (!err && out_mbox) {
		memcpy(out_mbox, mlxsw_pci->cmd.out_mbox.buf, out_mbox_size);
	}

	mutex_unlock(&mlxsw_pci->cmd.lock);

	return err;
}

static const struct mlxsw_bus mlxsw_pci_bus = {
	.kind			= "pci",
	.init			= mlxsw_pci_init,
	.fini			= mlxsw_pci_fini,
	.skb_transmit_busy	= mlxsw_pci_skb_transmit_busy,
	.skb_transmit		= mlxsw_pci_skb_transmit,
	.cmd_exec		= mlxsw_pci_cmd_exec,
};

static int mlxsw_pci_sw_reset(struct mlxsw_pci *mlxsw_pci,
			      const struct pci_device_id *id)
{
	unsigned long end;

	mlxsw_pci_write32(mlxsw_pci, SW_RESET, MLXSW_PCI_SW_RESET_RST_BIT);
	if (id->device == PCI_DEVICE_ID_MELLANOX_SWITCHX2) {
		msleep(MLXSW_PCI_SW_RESET_TIMEOUT_MSECS);
		return 0;
	}

	wmb(); /* reset needs to be written before we read control register */
	end = jiffies + msecs_to_jiffies(MLXSW_PCI_SW_RESET_TIMEOUT_MSECS);
	do {
		u32 val = mlxsw_pci_read32(mlxsw_pci, FW_READY);

		if ((val & MLXSW_PCI_FW_READY_MASK) == MLXSW_PCI_FW_READY_MAGIC)
			break;
		cond_resched();
	} while (time_before(jiffies, end));
	return 0;
}

static int mlxsw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mlxsw_pci *mlxsw_pci;
	int err;

	mlxsw_pci = kzalloc(sizeof(*mlxsw_pci), GFP_KERNEL);
	if (!mlxsw_pci)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		goto err_pci_enable_device;
	}

	err = pci_request_regions(pdev, mlxsw_pci_driver_name);
	if (err) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		goto err_pci_request_regions;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!err) {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err) {
			dev_err(&pdev->dev, "pci_set_consistent_dma_mask failed\n");
			goto err_pci_set_dma_mask;
		}
	} else {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "pci_set_dma_mask failed\n");
			goto err_pci_set_dma_mask;
		}
	}

	if (pci_resource_len(pdev, 0) < MLXSW_PCI_BAR0_SIZE) {
		dev_err(&pdev->dev, "invalid PCI region size\n");
		err = -EINVAL;
		goto err_pci_resource_len_check;
	}

	mlxsw_pci->hw_addr = ioremap(pci_resource_start(pdev, 0),
				     pci_resource_len(pdev, 0));
	if (!mlxsw_pci->hw_addr) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -EIO;
		goto err_ioremap;
	}
	pci_set_master(pdev);

	mlxsw_pci->pdev = pdev;
	pci_set_drvdata(pdev, mlxsw_pci);

	err = mlxsw_pci_sw_reset(mlxsw_pci, id);
	if (err) {
		dev_err(&pdev->dev, "Software reset failed\n");
		goto err_sw_reset;
	}

	err = pci_enable_msix_exact(pdev, &mlxsw_pci->msix_entry, 1);
	if (err) {
		dev_err(&pdev->dev, "MSI-X init failed\n");
		goto err_msix_init;
	}

	mlxsw_pci->bus_info.device_kind = mlxsw_pci_device_kind_get(id);
	mlxsw_pci->bus_info.device_name = pci_name(mlxsw_pci->pdev);
	mlxsw_pci->bus_info.dev = &pdev->dev;

	mlxsw_pci->dbg_dir = debugfs_create_dir(mlxsw_pci->bus_info.device_name,
						mlxsw_pci_dbg_root);
	if (!mlxsw_pci->dbg_dir) {
		dev_err(&pdev->dev, "Failed to create debugfs dir\n");
		err = -ENOMEM;
		goto err_dbg_create_dir;
	}

	err = mlxsw_core_bus_device_register(&mlxsw_pci->bus_info,
					     &mlxsw_pci_bus, mlxsw_pci);
	if (err) {
		dev_err(&pdev->dev, "cannot register bus device\n");
		goto err_bus_device_register;
	}

	return 0;

err_bus_device_register:
	debugfs_remove_recursive(mlxsw_pci->dbg_dir);
err_dbg_create_dir:
	pci_disable_msix(mlxsw_pci->pdev);
err_msix_init:
err_sw_reset:
	iounmap(mlxsw_pci->hw_addr);
err_ioremap:
err_pci_resource_len_check:
err_pci_set_dma_mask:
	pci_release_regions(pdev);
err_pci_request_regions:
	pci_disable_device(pdev);
err_pci_enable_device:
	kfree(mlxsw_pci);
	return err;
}

static void mlxsw_pci_remove(struct pci_dev *pdev)
{
	struct mlxsw_pci *mlxsw_pci = pci_get_drvdata(pdev);

	mlxsw_core_bus_device_unregister(mlxsw_pci->core);
	debugfs_remove_recursive(mlxsw_pci->dbg_dir);
	pci_disable_msix(mlxsw_pci->pdev);
	iounmap(mlxsw_pci->hw_addr);
	pci_release_regions(mlxsw_pci->pdev);
	pci_disable_device(mlxsw_pci->pdev);
	kfree(mlxsw_pci);
}

static struct pci_driver mlxsw_pci_driver = {
	.name		= mlxsw_pci_driver_name,
	.id_table	= mlxsw_pci_id_table,
	.probe		= mlxsw_pci_probe,
	.remove		= mlxsw_pci_remove,
};

static int __init mlxsw_pci_module_init(void)
{
	int err;

	mlxsw_pci_dbg_root = debugfs_create_dir(mlxsw_pci_driver_name, NULL);
	if (!mlxsw_pci_dbg_root)
		return -ENOMEM;
	err = pci_register_driver(&mlxsw_pci_driver);
	if (err)
		goto err_register_driver;
	return 0;

err_register_driver:
	debugfs_remove_recursive(mlxsw_pci_dbg_root);
	return err;
}

static void __exit mlxsw_pci_module_exit(void)
{
	pci_unregister_driver(&mlxsw_pci_driver);
	debugfs_remove_recursive(mlxsw_pci_dbg_root);
}

module_init(mlxsw_pci_module_init);
module_exit(mlxsw_pci_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox switch PCI interface driver");
MODULE_DEVICE_TABLE(pci, mlxsw_pci_id_table);

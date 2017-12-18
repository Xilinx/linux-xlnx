/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ena_com.h"

/*****************************************************************************/
/*****************************************************************************/

/* Timeout in micro-sec */
#define ADMIN_CMD_TIMEOUT_US (1000000)

#define ENA_ASYNC_QUEUE_DEPTH 4
#define ENA_ADMIN_QUEUE_DEPTH 32

#define MIN_ENA_VER (((ENA_COMMON_SPEC_VERSION_MAJOR) << \
		ENA_REGS_VERSION_MAJOR_VERSION_SHIFT) \
		| (ENA_COMMON_SPEC_VERSION_MINOR))

#define ENA_CTRL_MAJOR		0
#define ENA_CTRL_MINOR		0
#define ENA_CTRL_SUB_MINOR	1

#define MIN_ENA_CTRL_VER \
	(((ENA_CTRL_MAJOR) << \
	(ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT)) | \
	((ENA_CTRL_MINOR) << \
	(ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT)) | \
	(ENA_CTRL_SUB_MINOR))

#define ENA_DMA_ADDR_TO_UINT32_LOW(x)	((u32)((u64)(x)))
#define ENA_DMA_ADDR_TO_UINT32_HIGH(x)	((u32)(((u64)(x)) >> 32))

#define ENA_MMIO_READ_TIMEOUT 0xFFFFFFFF

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

enum ena_cmd_status {
	ENA_CMD_SUBMITTED,
	ENA_CMD_COMPLETED,
	/* Abort - canceled by the driver */
	ENA_CMD_ABORTED,
};

struct ena_comp_ctx {
	struct completion wait_event;
	struct ena_admin_acq_entry *user_cqe;
	u32 comp_size;
	enum ena_cmd_status status;
	/* status from the device */
	u8 comp_status;
	u8 cmd_opcode;
	bool occupied;
};

struct ena_com_stats_ctx {
	struct ena_admin_aq_get_stats_cmd get_cmd;
	struct ena_admin_acq_get_stats_resp get_resp;
};

static inline int ena_com_mem_addr_set(struct ena_com_dev *ena_dev,
				       struct ena_common_mem_addr *ena_addr,
				       dma_addr_t addr)
{
	if ((addr & GENMASK_ULL(ena_dev->dma_addr_bits - 1, 0)) != addr) {
		pr_err("dma address has more bits that the device supports\n");
		return -EINVAL;
	}

	ena_addr->mem_addr_low = (u32)addr;
	ena_addr->mem_addr_high = (u64)addr >> 32;

	return 0;
}

static int ena_com_admin_init_sq(struct ena_com_admin_queue *queue)
{
	struct ena_com_admin_sq *sq = &queue->sq;
	u16 size = ADMIN_SQ_SIZE(queue->q_depth);

	sq->entries = dma_zalloc_coherent(queue->q_dmadev, size, &sq->dma_addr,
					  GFP_KERNEL);

	if (!sq->entries) {
		pr_err("memory allocation failed");
		return -ENOMEM;
	}

	sq->head = 0;
	sq->tail = 0;
	sq->phase = 1;

	sq->db_addr = NULL;

	return 0;
}

static int ena_com_admin_init_cq(struct ena_com_admin_queue *queue)
{
	struct ena_com_admin_cq *cq = &queue->cq;
	u16 size = ADMIN_CQ_SIZE(queue->q_depth);

	cq->entries = dma_zalloc_coherent(queue->q_dmadev, size, &cq->dma_addr,
					  GFP_KERNEL);

	if (!cq->entries) {
		pr_err("memory allocation failed");
		return -ENOMEM;
	}

	cq->head = 0;
	cq->phase = 1;

	return 0;
}

static int ena_com_admin_init_aenq(struct ena_com_dev *dev,
				   struct ena_aenq_handlers *aenq_handlers)
{
	struct ena_com_aenq *aenq = &dev->aenq;
	u32 addr_low, addr_high, aenq_caps;
	u16 size;

	dev->aenq.q_depth = ENA_ASYNC_QUEUE_DEPTH;
	size = ADMIN_AENQ_SIZE(ENA_ASYNC_QUEUE_DEPTH);
	aenq->entries = dma_zalloc_coherent(dev->dmadev, size, &aenq->dma_addr,
					    GFP_KERNEL);

	if (!aenq->entries) {
		pr_err("memory allocation failed");
		return -ENOMEM;
	}

	aenq->head = aenq->q_depth;
	aenq->phase = 1;

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(aenq->dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(aenq->dma_addr);

	writel(addr_low, dev->reg_bar + ENA_REGS_AENQ_BASE_LO_OFF);
	writel(addr_high, dev->reg_bar + ENA_REGS_AENQ_BASE_HI_OFF);

	aenq_caps = 0;
	aenq_caps |= dev->aenq.q_depth & ENA_REGS_AENQ_CAPS_AENQ_DEPTH_MASK;
	aenq_caps |= (sizeof(struct ena_admin_aenq_entry)
		      << ENA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_SHIFT) &
		     ENA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_MASK;
	writel(aenq_caps, dev->reg_bar + ENA_REGS_AENQ_CAPS_OFF);

	if (unlikely(!aenq_handlers)) {
		pr_err("aenq handlers pointer is NULL\n");
		return -EINVAL;
	}

	aenq->aenq_handlers = aenq_handlers;

	return 0;
}

static inline void comp_ctxt_release(struct ena_com_admin_queue *queue,
				     struct ena_comp_ctx *comp_ctx)
{
	comp_ctx->occupied = false;
	atomic_dec(&queue->outstanding_cmds);
}

static struct ena_comp_ctx *get_comp_ctxt(struct ena_com_admin_queue *queue,
					  u16 command_id, bool capture)
{
	if (unlikely(command_id >= queue->q_depth)) {
		pr_err("command id is larger than the queue size. cmd_id: %u queue size %d\n",
		       command_id, queue->q_depth);
		return NULL;
	}

	if (unlikely(queue->comp_ctx[command_id].occupied && capture)) {
		pr_err("Completion context is occupied\n");
		return NULL;
	}

	if (capture) {
		atomic_inc(&queue->outstanding_cmds);
		queue->comp_ctx[command_id].occupied = true;
	}

	return &queue->comp_ctx[command_id];
}

static struct ena_comp_ctx *__ena_com_submit_admin_cmd(struct ena_com_admin_queue *admin_queue,
						       struct ena_admin_aq_entry *cmd,
						       size_t cmd_size_in_bytes,
						       struct ena_admin_acq_entry *comp,
						       size_t comp_size_in_bytes)
{
	struct ena_comp_ctx *comp_ctx;
	u16 tail_masked, cmd_id;
	u16 queue_size_mask;
	u16 cnt;

	queue_size_mask = admin_queue->q_depth - 1;

	tail_masked = admin_queue->sq.tail & queue_size_mask;

	/* In case of queue FULL */
	cnt = admin_queue->sq.tail - admin_queue->sq.head;
	if (cnt >= admin_queue->q_depth) {
		pr_debug("admin queue is FULL (tail %d head %d depth: %d)\n",
			 admin_queue->sq.tail, admin_queue->sq.head,
			 admin_queue->q_depth);
		admin_queue->stats.out_of_space++;
		return ERR_PTR(-ENOSPC);
	}

	cmd_id = admin_queue->curr_cmd_id;

	cmd->aq_common_descriptor.flags |= admin_queue->sq.phase &
		ENA_ADMIN_AQ_COMMON_DESC_PHASE_MASK;

	cmd->aq_common_descriptor.command_id |= cmd_id &
		ENA_ADMIN_AQ_COMMON_DESC_COMMAND_ID_MASK;

	comp_ctx = get_comp_ctxt(admin_queue, cmd_id, true);
	if (unlikely(!comp_ctx))
		return ERR_PTR(-EINVAL);

	comp_ctx->status = ENA_CMD_SUBMITTED;
	comp_ctx->comp_size = (u32)comp_size_in_bytes;
	comp_ctx->user_cqe = comp;
	comp_ctx->cmd_opcode = cmd->aq_common_descriptor.opcode;

	reinit_completion(&comp_ctx->wait_event);

	memcpy(&admin_queue->sq.entries[tail_masked], cmd, cmd_size_in_bytes);

	admin_queue->curr_cmd_id = (admin_queue->curr_cmd_id + 1) &
		queue_size_mask;

	admin_queue->sq.tail++;
	admin_queue->stats.submitted_cmd++;

	if (unlikely((admin_queue->sq.tail & queue_size_mask) == 0))
		admin_queue->sq.phase = !admin_queue->sq.phase;

	writel(admin_queue->sq.tail, admin_queue->sq.db_addr);

	return comp_ctx;
}

static inline int ena_com_init_comp_ctxt(struct ena_com_admin_queue *queue)
{
	size_t size = queue->q_depth * sizeof(struct ena_comp_ctx);
	struct ena_comp_ctx *comp_ctx;
	u16 i;

	queue->comp_ctx = devm_kzalloc(queue->q_dmadev, size, GFP_KERNEL);
	if (unlikely(!queue->comp_ctx)) {
		pr_err("memory allocation failed");
		return -ENOMEM;
	}

	for (i = 0; i < queue->q_depth; i++) {
		comp_ctx = get_comp_ctxt(queue, i, false);
		if (comp_ctx)
			init_completion(&comp_ctx->wait_event);
	}

	return 0;
}

static struct ena_comp_ctx *ena_com_submit_admin_cmd(struct ena_com_admin_queue *admin_queue,
						     struct ena_admin_aq_entry *cmd,
						     size_t cmd_size_in_bytes,
						     struct ena_admin_acq_entry *comp,
						     size_t comp_size_in_bytes)
{
	unsigned long flags;
	struct ena_comp_ctx *comp_ctx;

	spin_lock_irqsave(&admin_queue->q_lock, flags);
	if (unlikely(!admin_queue->running_state)) {
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);
		return ERR_PTR(-ENODEV);
	}
	comp_ctx = __ena_com_submit_admin_cmd(admin_queue, cmd,
					      cmd_size_in_bytes,
					      comp,
					      comp_size_in_bytes);
	if (unlikely(IS_ERR(comp_ctx)))
		admin_queue->running_state = false;
	spin_unlock_irqrestore(&admin_queue->q_lock, flags);

	return comp_ctx;
}

static int ena_com_init_io_sq(struct ena_com_dev *ena_dev,
			      struct ena_com_create_io_ctx *ctx,
			      struct ena_com_io_sq *io_sq)
{
	size_t size;
	int dev_node = 0;

	memset(&io_sq->desc_addr, 0x0, sizeof(struct ena_com_io_desc_addr));

	io_sq->desc_entry_size =
		(io_sq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX) ?
		sizeof(struct ena_eth_io_tx_desc) :
		sizeof(struct ena_eth_io_rx_desc);

	size = io_sq->desc_entry_size * io_sq->q_depth;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST) {
		dev_node = dev_to_node(ena_dev->dmadev);
		set_dev_node(ena_dev->dmadev, ctx->numa_node);
		io_sq->desc_addr.virt_addr =
			dma_zalloc_coherent(ena_dev->dmadev, size,
					    &io_sq->desc_addr.phys_addr,
					    GFP_KERNEL);
		set_dev_node(ena_dev->dmadev, dev_node);
		if (!io_sq->desc_addr.virt_addr) {
			io_sq->desc_addr.virt_addr =
				dma_zalloc_coherent(ena_dev->dmadev, size,
						    &io_sq->desc_addr.phys_addr,
						    GFP_KERNEL);
		}
	} else {
		dev_node = dev_to_node(ena_dev->dmadev);
		set_dev_node(ena_dev->dmadev, ctx->numa_node);
		io_sq->desc_addr.virt_addr =
			devm_kzalloc(ena_dev->dmadev, size, GFP_KERNEL);
		set_dev_node(ena_dev->dmadev, dev_node);
		if (!io_sq->desc_addr.virt_addr) {
			io_sq->desc_addr.virt_addr =
				devm_kzalloc(ena_dev->dmadev, size, GFP_KERNEL);
		}
	}

	if (!io_sq->desc_addr.virt_addr) {
		pr_err("memory allocation failed");
		return -ENOMEM;
	}

	io_sq->tail = 0;
	io_sq->next_to_comp = 0;
	io_sq->phase = 1;

	return 0;
}

static int ena_com_init_io_cq(struct ena_com_dev *ena_dev,
			      struct ena_com_create_io_ctx *ctx,
			      struct ena_com_io_cq *io_cq)
{
	size_t size;
	int prev_node = 0;

	memset(&io_cq->cdesc_addr, 0x0, sizeof(struct ena_com_io_desc_addr));

	/* Use the basic completion descriptor for Rx */
	io_cq->cdesc_entry_size_in_bytes =
		(io_cq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX) ?
		sizeof(struct ena_eth_io_tx_cdesc) :
		sizeof(struct ena_eth_io_rx_cdesc_base);

	size = io_cq->cdesc_entry_size_in_bytes * io_cq->q_depth;

	prev_node = dev_to_node(ena_dev->dmadev);
	set_dev_node(ena_dev->dmadev, ctx->numa_node);
	io_cq->cdesc_addr.virt_addr =
		dma_zalloc_coherent(ena_dev->dmadev, size,
				    &io_cq->cdesc_addr.phys_addr, GFP_KERNEL);
	set_dev_node(ena_dev->dmadev, prev_node);
	if (!io_cq->cdesc_addr.virt_addr) {
		io_cq->cdesc_addr.virt_addr =
			dma_zalloc_coherent(ena_dev->dmadev, size,
					    &io_cq->cdesc_addr.phys_addr,
					    GFP_KERNEL);
	}

	if (!io_cq->cdesc_addr.virt_addr) {
		pr_err("memory allocation failed");
		return -ENOMEM;
	}

	io_cq->phase = 1;
	io_cq->head = 0;

	return 0;
}

static void ena_com_handle_single_admin_completion(struct ena_com_admin_queue *admin_queue,
						   struct ena_admin_acq_entry *cqe)
{
	struct ena_comp_ctx *comp_ctx;
	u16 cmd_id;

	cmd_id = cqe->acq_common_descriptor.command &
		ENA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID_MASK;

	comp_ctx = get_comp_ctxt(admin_queue, cmd_id, false);
	if (unlikely(!comp_ctx)) {
		pr_err("comp_ctx is NULL. Changing the admin queue running state\n");
		admin_queue->running_state = false;
		return;
	}

	comp_ctx->status = ENA_CMD_COMPLETED;
	comp_ctx->comp_status = cqe->acq_common_descriptor.status;

	if (comp_ctx->user_cqe)
		memcpy(comp_ctx->user_cqe, (void *)cqe, comp_ctx->comp_size);

	if (!admin_queue->polling)
		complete(&comp_ctx->wait_event);
}

static void ena_com_handle_admin_completion(struct ena_com_admin_queue *admin_queue)
{
	struct ena_admin_acq_entry *cqe = NULL;
	u16 comp_num = 0;
	u16 head_masked;
	u8 phase;

	head_masked = admin_queue->cq.head & (admin_queue->q_depth - 1);
	phase = admin_queue->cq.phase;

	cqe = &admin_queue->cq.entries[head_masked];

	/* Go over all the completions */
	while ((cqe->acq_common_descriptor.flags &
			ENA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK) == phase) {
		/* Do not read the rest of the completion entry before the
		 * phase bit was validated
		 */
		rmb();
		ena_com_handle_single_admin_completion(admin_queue, cqe);

		head_masked++;
		comp_num++;
		if (unlikely(head_masked == admin_queue->q_depth)) {
			head_masked = 0;
			phase = !phase;
		}

		cqe = &admin_queue->cq.entries[head_masked];
	}

	admin_queue->cq.head += comp_num;
	admin_queue->cq.phase = phase;
	admin_queue->sq.head += comp_num;
	admin_queue->stats.completed_cmd += comp_num;
}

static int ena_com_comp_status_to_errno(u8 comp_status)
{
	if (unlikely(comp_status != 0))
		pr_err("admin command failed[%u]\n", comp_status);

	if (unlikely(comp_status > ENA_ADMIN_UNKNOWN_ERROR))
		return -EINVAL;

	switch (comp_status) {
	case ENA_ADMIN_SUCCESS:
		return 0;
	case ENA_ADMIN_RESOURCE_ALLOCATION_FAILURE:
		return -ENOMEM;
	case ENA_ADMIN_UNSUPPORTED_OPCODE:
		return -EPERM;
	case ENA_ADMIN_BAD_OPCODE:
	case ENA_ADMIN_MALFORMED_REQUEST:
	case ENA_ADMIN_ILLEGAL_PARAMETER:
	case ENA_ADMIN_UNKNOWN_ERROR:
		return -EINVAL;
	}

	return 0;
}

static int ena_com_wait_and_process_admin_cq_polling(struct ena_comp_ctx *comp_ctx,
						     struct ena_com_admin_queue *admin_queue)
{
	unsigned long flags;
	u32 start_time;
	int ret;

	start_time = ((u32)jiffies_to_usecs(jiffies));

	while (comp_ctx->status == ENA_CMD_SUBMITTED) {
		if ((((u32)jiffies_to_usecs(jiffies)) - start_time) >
		    ADMIN_CMD_TIMEOUT_US) {
			pr_err("Wait for completion (polling) timeout\n");
			/* ENA didn't have any completion */
			spin_lock_irqsave(&admin_queue->q_lock, flags);
			admin_queue->stats.no_completion++;
			admin_queue->running_state = false;
			spin_unlock_irqrestore(&admin_queue->q_lock, flags);

			ret = -ETIME;
			goto err;
		}

		spin_lock_irqsave(&admin_queue->q_lock, flags);
		ena_com_handle_admin_completion(admin_queue);
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);

		msleep(100);
	}

	if (unlikely(comp_ctx->status == ENA_CMD_ABORTED)) {
		pr_err("Command was aborted\n");
		spin_lock_irqsave(&admin_queue->q_lock, flags);
		admin_queue->stats.aborted_cmd++;
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);
		ret = -ENODEV;
		goto err;
	}

	WARN(comp_ctx->status != ENA_CMD_COMPLETED, "Invalid comp status %d\n",
	     comp_ctx->status);

	ret = ena_com_comp_status_to_errno(comp_ctx->comp_status);
err:
	comp_ctxt_release(admin_queue, comp_ctx);
	return ret;
}

static int ena_com_wait_and_process_admin_cq_interrupts(struct ena_comp_ctx *comp_ctx,
							struct ena_com_admin_queue *admin_queue)
{
	unsigned long flags;
	int ret;

	wait_for_completion_timeout(&comp_ctx->wait_event,
				    usecs_to_jiffies(ADMIN_CMD_TIMEOUT_US));

	/* In case the command wasn't completed find out the root cause.
	 * There might be 2 kinds of errors
	 * 1) No completion (timeout reached)
	 * 2) There is completion but the device didn't get any msi-x interrupt.
	 */
	if (unlikely(comp_ctx->status == ENA_CMD_SUBMITTED)) {
		spin_lock_irqsave(&admin_queue->q_lock, flags);
		ena_com_handle_admin_completion(admin_queue);
		admin_queue->stats.no_completion++;
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);

		if (comp_ctx->status == ENA_CMD_COMPLETED)
			pr_err("The ena device have completion but the driver didn't receive any MSI-X interrupt (cmd %d)\n",
			       comp_ctx->cmd_opcode);
		else
			pr_err("The ena device doesn't send any completion for the admin cmd %d status %d\n",
			       comp_ctx->cmd_opcode, comp_ctx->status);

		admin_queue->running_state = false;
		ret = -ETIME;
		goto err;
	}

	ret = ena_com_comp_status_to_errno(comp_ctx->comp_status);
err:
	comp_ctxt_release(admin_queue, comp_ctx);
	return ret;
}

/* This method read the hardware device register through posting writes
 * and waiting for response
 * On timeout the function will return ENA_MMIO_READ_TIMEOUT
 */
static u32 ena_com_reg_bar_read32(struct ena_com_dev *ena_dev, u16 offset)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;
	volatile struct ena_admin_ena_mmio_req_read_less_resp *read_resp =
		mmio_read->read_resp;
	u32 mmio_read_reg, ret;
	unsigned long flags;
	int i;

	might_sleep();

	/* If readless is disabled, perform regular read */
	if (!mmio_read->readless_supported)
		return readl(ena_dev->reg_bar + offset);

	spin_lock_irqsave(&mmio_read->lock, flags);
	mmio_read->seq_num++;

	read_resp->req_id = mmio_read->seq_num + 0xDEAD;
	mmio_read_reg = (offset << ENA_REGS_MMIO_REG_READ_REG_OFF_SHIFT) &
			ENA_REGS_MMIO_REG_READ_REG_OFF_MASK;
	mmio_read_reg |= mmio_read->seq_num &
			ENA_REGS_MMIO_REG_READ_REQ_ID_MASK;

	/* make sure read_resp->req_id get updated before the hw can write
	 * there
	 */
	wmb();

	writel(mmio_read_reg, ena_dev->reg_bar + ENA_REGS_MMIO_REG_READ_OFF);

	for (i = 0; i < ENA_REG_READ_TIMEOUT; i++) {
		if (read_resp->req_id == mmio_read->seq_num)
			break;

		udelay(1);
	}

	if (unlikely(i == ENA_REG_READ_TIMEOUT)) {
		pr_err("reading reg failed for timeout. expected: req id[%hu] offset[%hu] actual: req id[%hu] offset[%hu]\n",
		       mmio_read->seq_num, offset, read_resp->req_id,
		       read_resp->reg_off);
		ret = ENA_MMIO_READ_TIMEOUT;
		goto err;
	}

	if (read_resp->reg_off != offset) {
		pr_err("Read failure: wrong offset provided");
		ret = ENA_MMIO_READ_TIMEOUT;
	} else {
		ret = read_resp->reg_val;
	}
err:
	spin_unlock_irqrestore(&mmio_read->lock, flags);

	return ret;
}

/* There are two types to wait for completion.
 * Polling mode - wait until the completion is available.
 * Async mode - wait on wait queue until the completion is ready
 * (or the timeout expired).
 * It is expected that the IRQ called ena_com_handle_admin_completion
 * to mark the completions.
 */
static int ena_com_wait_and_process_admin_cq(struct ena_comp_ctx *comp_ctx,
					     struct ena_com_admin_queue *admin_queue)
{
	if (admin_queue->polling)
		return ena_com_wait_and_process_admin_cq_polling(comp_ctx,
								 admin_queue);

	return ena_com_wait_and_process_admin_cq_interrupts(comp_ctx,
							    admin_queue);
}

static int ena_com_destroy_io_sq(struct ena_com_dev *ena_dev,
				 struct ena_com_io_sq *io_sq)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_destroy_sq_cmd destroy_cmd;
	struct ena_admin_acq_destroy_sq_resp_desc destroy_resp;
	u8 direction;
	int ret;

	memset(&destroy_cmd, 0x0, sizeof(struct ena_admin_aq_destroy_sq_cmd));

	if (io_sq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX)
		direction = ENA_ADMIN_SQ_DIRECTION_TX;
	else
		direction = ENA_ADMIN_SQ_DIRECTION_RX;

	destroy_cmd.sq.sq_identity |= (direction <<
		ENA_ADMIN_SQ_SQ_DIRECTION_SHIFT) &
		ENA_ADMIN_SQ_SQ_DIRECTION_MASK;

	destroy_cmd.sq.sq_idx = io_sq->idx;
	destroy_cmd.aq_common_descriptor.opcode = ENA_ADMIN_DESTROY_SQ;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&destroy_cmd,
					    sizeof(destroy_cmd),
					    (struct ena_admin_acq_entry *)&destroy_resp,
					    sizeof(destroy_resp));

	if (unlikely(ret && (ret != -ENODEV)))
		pr_err("failed to destroy io sq error: %d\n", ret);

	return ret;
}

static void ena_com_io_queue_free(struct ena_com_dev *ena_dev,
				  struct ena_com_io_sq *io_sq,
				  struct ena_com_io_cq *io_cq)
{
	size_t size;

	if (io_cq->cdesc_addr.virt_addr) {
		size = io_cq->cdesc_entry_size_in_bytes * io_cq->q_depth;

		dma_free_coherent(ena_dev->dmadev, size,
				  io_cq->cdesc_addr.virt_addr,
				  io_cq->cdesc_addr.phys_addr);

		io_cq->cdesc_addr.virt_addr = NULL;
	}

	if (io_sq->desc_addr.virt_addr) {
		size = io_sq->desc_entry_size * io_sq->q_depth;

		if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST)
			dma_free_coherent(ena_dev->dmadev, size,
					  io_sq->desc_addr.virt_addr,
					  io_sq->desc_addr.phys_addr);
		else
			devm_kfree(ena_dev->dmadev, io_sq->desc_addr.virt_addr);

		io_sq->desc_addr.virt_addr = NULL;
	}
}

static int wait_for_reset_state(struct ena_com_dev *ena_dev, u32 timeout,
				u16 exp_state)
{
	u32 val, i;

	for (i = 0; i < timeout; i++) {
		val = ena_com_reg_bar_read32(ena_dev, ENA_REGS_DEV_STS_OFF);

		if (unlikely(val == ENA_MMIO_READ_TIMEOUT)) {
			pr_err("Reg read timeout occurred\n");
			return -ETIME;
		}

		if ((val & ENA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK) ==
			exp_state)
			return 0;

		/* The resolution of the timeout is 100ms */
		msleep(100);
	}

	return -ETIME;
}

static bool ena_com_check_supported_feature_id(struct ena_com_dev *ena_dev,
					       enum ena_admin_aq_feature_id feature_id)
{
	u32 feature_mask = 1 << feature_id;

	/* Device attributes is always supported */
	if ((feature_id != ENA_ADMIN_DEVICE_ATTRIBUTES) &&
	    !(ena_dev->supported_features & feature_mask))
		return false;

	return true;
}

static int ena_com_get_feature_ex(struct ena_com_dev *ena_dev,
				  struct ena_admin_get_feat_resp *get_resp,
				  enum ena_admin_aq_feature_id feature_id,
				  dma_addr_t control_buf_dma_addr,
				  u32 control_buff_size)
{
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_get_feat_cmd get_cmd;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev, feature_id)) {
		pr_info("Feature %d isn't supported\n", feature_id);
		return -EPERM;
	}

	memset(&get_cmd, 0x0, sizeof(get_cmd));
	admin_queue = &ena_dev->admin_queue;

	get_cmd.aq_common_descriptor.opcode = ENA_ADMIN_GET_FEATURE;

	if (control_buff_size)
		get_cmd.aq_common_descriptor.flags =
			ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	else
		get_cmd.aq_common_descriptor.flags = 0;

	ret = ena_com_mem_addr_set(ena_dev,
				   &get_cmd.control_buffer.address,
				   control_buf_dma_addr);
	if (unlikely(ret)) {
		pr_err("memory address set failed\n");
		return ret;
	}

	get_cmd.control_buffer.length = control_buff_size;

	get_cmd.feat_common.feature_id = feature_id;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)
					    &get_cmd,
					    sizeof(get_cmd),
					    (struct ena_admin_acq_entry *)
					    get_resp,
					    sizeof(*get_resp));

	if (unlikely(ret))
		pr_err("Failed to submit get_feature command %d error: %d\n",
		       feature_id, ret);

	return ret;
}

static int ena_com_get_feature(struct ena_com_dev *ena_dev,
			       struct ena_admin_get_feat_resp *get_resp,
			       enum ena_admin_aq_feature_id feature_id)
{
	return ena_com_get_feature_ex(ena_dev,
				      get_resp,
				      feature_id,
				      0,
				      0);
}

static int ena_com_hash_key_allocate(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	rss->hash_key =
		dma_zalloc_coherent(ena_dev->dmadev, sizeof(*rss->hash_key),
				    &rss->hash_key_dma_addr, GFP_KERNEL);

	if (unlikely(!rss->hash_key))
		return -ENOMEM;

	return 0;
}

static void ena_com_hash_key_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	if (rss->hash_key)
		dma_free_coherent(ena_dev->dmadev, sizeof(*rss->hash_key),
				  rss->hash_key, rss->hash_key_dma_addr);
	rss->hash_key = NULL;
}

static int ena_com_hash_ctrl_init(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	rss->hash_ctrl =
		dma_zalloc_coherent(ena_dev->dmadev, sizeof(*rss->hash_ctrl),
				    &rss->hash_ctrl_dma_addr, GFP_KERNEL);

	if (unlikely(!rss->hash_ctrl))
		return -ENOMEM;

	return 0;
}

static void ena_com_hash_ctrl_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	if (rss->hash_ctrl)
		dma_free_coherent(ena_dev->dmadev, sizeof(*rss->hash_ctrl),
				  rss->hash_ctrl, rss->hash_ctrl_dma_addr);
	rss->hash_ctrl = NULL;
}

static int ena_com_indirect_table_allocate(struct ena_com_dev *ena_dev,
					   u16 log_size)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	size_t tbl_size;
	int ret;

	ret = ena_com_get_feature(ena_dev, &get_resp,
				  ENA_ADMIN_RSS_REDIRECTION_TABLE_CONFIG);
	if (unlikely(ret))
		return ret;

	if ((get_resp.u.ind_table.min_size > log_size) ||
	    (get_resp.u.ind_table.max_size < log_size)) {
		pr_err("indirect table size doesn't fit. requested size: %d while min is:%d and max %d\n",
		       1 << log_size, 1 << get_resp.u.ind_table.min_size,
		       1 << get_resp.u.ind_table.max_size);
		return -EINVAL;
	}

	tbl_size = (1ULL << log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	rss->rss_ind_tbl =
		dma_zalloc_coherent(ena_dev->dmadev, tbl_size,
				    &rss->rss_ind_tbl_dma_addr, GFP_KERNEL);
	if (unlikely(!rss->rss_ind_tbl))
		goto mem_err1;

	tbl_size = (1ULL << log_size) * sizeof(u16);
	rss->host_rss_ind_tbl =
		devm_kzalloc(ena_dev->dmadev, tbl_size, GFP_KERNEL);
	if (unlikely(!rss->host_rss_ind_tbl))
		goto mem_err2;

	rss->tbl_log_size = log_size;

	return 0;

mem_err2:
	tbl_size = (1ULL << log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	dma_free_coherent(ena_dev->dmadev, tbl_size, rss->rss_ind_tbl,
			  rss->rss_ind_tbl_dma_addr);
	rss->rss_ind_tbl = NULL;
mem_err1:
	rss->tbl_log_size = 0;
	return -ENOMEM;
}

static void ena_com_indirect_table_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;
	size_t tbl_size = (1ULL << rss->tbl_log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	if (rss->rss_ind_tbl)
		dma_free_coherent(ena_dev->dmadev, tbl_size, rss->rss_ind_tbl,
				  rss->rss_ind_tbl_dma_addr);
	rss->rss_ind_tbl = NULL;

	if (rss->host_rss_ind_tbl)
		devm_kfree(ena_dev->dmadev, rss->host_rss_ind_tbl);
	rss->host_rss_ind_tbl = NULL;
}

static int ena_com_create_io_sq(struct ena_com_dev *ena_dev,
				struct ena_com_io_sq *io_sq, u16 cq_idx)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_create_sq_cmd create_cmd;
	struct ena_admin_acq_create_sq_resp_desc cmd_completion;
	u8 direction;
	int ret;

	memset(&create_cmd, 0x0, sizeof(struct ena_admin_aq_create_sq_cmd));

	create_cmd.aq_common_descriptor.opcode = ENA_ADMIN_CREATE_SQ;

	if (io_sq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX)
		direction = ENA_ADMIN_SQ_DIRECTION_TX;
	else
		direction = ENA_ADMIN_SQ_DIRECTION_RX;

	create_cmd.sq_identity |= (direction <<
		ENA_ADMIN_AQ_CREATE_SQ_CMD_SQ_DIRECTION_SHIFT) &
		ENA_ADMIN_AQ_CREATE_SQ_CMD_SQ_DIRECTION_MASK;

	create_cmd.sq_caps_2 |= io_sq->mem_queue_type &
		ENA_ADMIN_AQ_CREATE_SQ_CMD_PLACEMENT_POLICY_MASK;

	create_cmd.sq_caps_2 |= (ENA_ADMIN_COMPLETION_POLICY_DESC <<
		ENA_ADMIN_AQ_CREATE_SQ_CMD_COMPLETION_POLICY_SHIFT) &
		ENA_ADMIN_AQ_CREATE_SQ_CMD_COMPLETION_POLICY_MASK;

	create_cmd.sq_caps_3 |=
		ENA_ADMIN_AQ_CREATE_SQ_CMD_IS_PHYSICALLY_CONTIGUOUS_MASK;

	create_cmd.cq_idx = cq_idx;
	create_cmd.sq_depth = io_sq->q_depth;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST) {
		ret = ena_com_mem_addr_set(ena_dev,
					   &create_cmd.sq_ba,
					   io_sq->desc_addr.phys_addr);
		if (unlikely(ret)) {
			pr_err("memory address set failed\n");
			return ret;
		}
	}

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&create_cmd,
					    sizeof(create_cmd),
					    (struct ena_admin_acq_entry *)&cmd_completion,
					    sizeof(cmd_completion));
	if (unlikely(ret)) {
		pr_err("Failed to create IO SQ. error: %d\n", ret);
		return ret;
	}

	io_sq->idx = cmd_completion.sq_idx;

	io_sq->db_addr = (u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
		(uintptr_t)cmd_completion.sq_doorbell_offset);

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
		io_sq->header_addr = (u8 __iomem *)((uintptr_t)ena_dev->mem_bar
				+ cmd_completion.llq_headers_offset);

		io_sq->desc_addr.pbuf_dev_addr =
			(u8 __iomem *)((uintptr_t)ena_dev->mem_bar +
			cmd_completion.llq_descriptors_offset);
	}

	pr_debug("created sq[%u], depth[%u]\n", io_sq->idx, io_sq->q_depth);

	return ret;
}

static int ena_com_ind_tbl_convert_to_device(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_com_io_sq *io_sq;
	u16 qid;
	int i;

	for (i = 0; i < 1 << rss->tbl_log_size; i++) {
		qid = rss->host_rss_ind_tbl[i];
		if (qid >= ENA_TOTAL_NUM_QUEUES)
			return -EINVAL;

		io_sq = &ena_dev->io_sq_queues[qid];

		if (io_sq->direction != ENA_COM_IO_QUEUE_DIRECTION_RX)
			return -EINVAL;

		rss->rss_ind_tbl[i].cq_idx = io_sq->idx;
	}

	return 0;
}

static int ena_com_ind_tbl_convert_from_device(struct ena_com_dev *ena_dev)
{
	u16 dev_idx_to_host_tbl[ENA_TOTAL_NUM_QUEUES] = { (u16)-1 };
	struct ena_rss *rss = &ena_dev->rss;
	u8 idx;
	u16 i;

	for (i = 0; i < ENA_TOTAL_NUM_QUEUES; i++)
		dev_idx_to_host_tbl[ena_dev->io_sq_queues[i].idx] = i;

	for (i = 0; i < 1 << rss->tbl_log_size; i++) {
		if (rss->rss_ind_tbl[i].cq_idx > ENA_TOTAL_NUM_QUEUES)
			return -EINVAL;
		idx = (u8)rss->rss_ind_tbl[i].cq_idx;

		if (dev_idx_to_host_tbl[idx] > ENA_TOTAL_NUM_QUEUES)
			return -EINVAL;

		rss->host_rss_ind_tbl[i] = dev_idx_to_host_tbl[idx];
	}

	return 0;
}

static int ena_com_init_interrupt_moderation_table(struct ena_com_dev *ena_dev)
{
	size_t size;

	size = sizeof(struct ena_intr_moder_entry) * ENA_INTR_MAX_NUM_OF_LEVELS;

	ena_dev->intr_moder_tbl =
		devm_kzalloc(ena_dev->dmadev, size, GFP_KERNEL);
	if (!ena_dev->intr_moder_tbl)
		return -ENOMEM;

	ena_com_config_default_interrupt_moderation_table(ena_dev);

	return 0;
}

static void ena_com_update_intr_delay_resolution(struct ena_com_dev *ena_dev,
						 u16 intr_delay_resolution)
{
	struct ena_intr_moder_entry *intr_moder_tbl = ena_dev->intr_moder_tbl;
	unsigned int i;

	if (!intr_delay_resolution) {
		pr_err("Illegal intr_delay_resolution provided. Going to use default 1 usec resolution\n");
		intr_delay_resolution = 1;
	}
	ena_dev->intr_delay_resolution = intr_delay_resolution;

	/* update Rx */
	for (i = 0; i < ENA_INTR_MAX_NUM_OF_LEVELS; i++)
		intr_moder_tbl[i].intr_moder_interval /= intr_delay_resolution;

	/* update Tx */
	ena_dev->intr_moder_tx_interval /= intr_delay_resolution;
}

/*****************************************************************************/
/*******************************      API       ******************************/
/*****************************************************************************/

int ena_com_execute_admin_command(struct ena_com_admin_queue *admin_queue,
				  struct ena_admin_aq_entry *cmd,
				  size_t cmd_size,
				  struct ena_admin_acq_entry *comp,
				  size_t comp_size)
{
	struct ena_comp_ctx *comp_ctx;
	int ret;

	comp_ctx = ena_com_submit_admin_cmd(admin_queue, cmd, cmd_size,
					    comp, comp_size);
	if (unlikely(IS_ERR(comp_ctx))) {
		pr_err("Failed to submit command [%ld]\n", PTR_ERR(comp_ctx));
		return PTR_ERR(comp_ctx);
	}

	ret = ena_com_wait_and_process_admin_cq(comp_ctx, admin_queue);
	if (unlikely(ret)) {
		if (admin_queue->running_state)
			pr_err("Failed to process command. ret = %d\n", ret);
		else
			pr_debug("Failed to process command. ret = %d\n", ret);
	}
	return ret;
}

int ena_com_create_io_cq(struct ena_com_dev *ena_dev,
			 struct ena_com_io_cq *io_cq)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_create_cq_cmd create_cmd;
	struct ena_admin_acq_create_cq_resp_desc cmd_completion;
	int ret;

	memset(&create_cmd, 0x0, sizeof(struct ena_admin_aq_create_cq_cmd));

	create_cmd.aq_common_descriptor.opcode = ENA_ADMIN_CREATE_CQ;

	create_cmd.cq_caps_2 |= (io_cq->cdesc_entry_size_in_bytes / 4) &
		ENA_ADMIN_AQ_CREATE_CQ_CMD_CQ_ENTRY_SIZE_WORDS_MASK;
	create_cmd.cq_caps_1 |=
		ENA_ADMIN_AQ_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED_MASK;

	create_cmd.msix_vector = io_cq->msix_vector;
	create_cmd.cq_depth = io_cq->q_depth;

	ret = ena_com_mem_addr_set(ena_dev,
				   &create_cmd.cq_ba,
				   io_cq->cdesc_addr.phys_addr);
	if (unlikely(ret)) {
		pr_err("memory address set failed\n");
		return ret;
	}

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&create_cmd,
					    sizeof(create_cmd),
					    (struct ena_admin_acq_entry *)&cmd_completion,
					    sizeof(cmd_completion));
	if (unlikely(ret)) {
		pr_err("Failed to create IO CQ. error: %d\n", ret);
		return ret;
	}

	io_cq->idx = cmd_completion.cq_idx;

	io_cq->unmask_reg = (u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
		cmd_completion.cq_interrupt_unmask_register_offset);

	if (cmd_completion.cq_head_db_register_offset)
		io_cq->cq_head_db_reg =
			(u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
			cmd_completion.cq_head_db_register_offset);

	if (cmd_completion.numa_node_register_offset)
		io_cq->numa_node_cfg_reg =
			(u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
			cmd_completion.numa_node_register_offset);

	pr_debug("created cq[%u], depth[%u]\n", io_cq->idx, io_cq->q_depth);

	return ret;
}

int ena_com_get_io_handlers(struct ena_com_dev *ena_dev, u16 qid,
			    struct ena_com_io_sq **io_sq,
			    struct ena_com_io_cq **io_cq)
{
	if (qid >= ENA_TOTAL_NUM_QUEUES) {
		pr_err("Invalid queue number %d but the max is %d\n", qid,
		       ENA_TOTAL_NUM_QUEUES);
		return -EINVAL;
	}

	*io_sq = &ena_dev->io_sq_queues[qid];
	*io_cq = &ena_dev->io_cq_queues[qid];

	return 0;
}

void ena_com_abort_admin_commands(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_comp_ctx *comp_ctx;
	u16 i;

	if (!admin_queue->comp_ctx)
		return;

	for (i = 0; i < admin_queue->q_depth; i++) {
		comp_ctx = get_comp_ctxt(admin_queue, i, false);
		if (unlikely(!comp_ctx))
			break;

		comp_ctx->status = ENA_CMD_ABORTED;

		complete(&comp_ctx->wait_event);
	}
}

void ena_com_wait_for_abort_completion(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	unsigned long flags;

	spin_lock_irqsave(&admin_queue->q_lock, flags);
	while (atomic_read(&admin_queue->outstanding_cmds) != 0) {
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);
		msleep(20);
		spin_lock_irqsave(&admin_queue->q_lock, flags);
	}
	spin_unlock_irqrestore(&admin_queue->q_lock, flags);
}

int ena_com_destroy_io_cq(struct ena_com_dev *ena_dev,
			  struct ena_com_io_cq *io_cq)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_destroy_cq_cmd destroy_cmd;
	struct ena_admin_acq_destroy_cq_resp_desc destroy_resp;
	int ret;

	memset(&destroy_cmd, 0x0, sizeof(struct ena_admin_aq_destroy_sq_cmd));

	destroy_cmd.cq_idx = io_cq->idx;
	destroy_cmd.aq_common_descriptor.opcode = ENA_ADMIN_DESTROY_CQ;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&destroy_cmd,
					    sizeof(destroy_cmd),
					    (struct ena_admin_acq_entry *)&destroy_resp,
					    sizeof(destroy_resp));

	if (unlikely(ret && (ret != -ENODEV)))
		pr_err("Failed to destroy IO CQ. error: %d\n", ret);

	return ret;
}

bool ena_com_get_admin_running_state(struct ena_com_dev *ena_dev)
{
	return ena_dev->admin_queue.running_state;
}

void ena_com_set_admin_running_state(struct ena_com_dev *ena_dev, bool state)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	unsigned long flags;

	spin_lock_irqsave(&admin_queue->q_lock, flags);
	ena_dev->admin_queue.running_state = state;
	spin_unlock_irqrestore(&admin_queue->q_lock, flags);
}

void ena_com_admin_aenq_enable(struct ena_com_dev *ena_dev)
{
	u16 depth = ena_dev->aenq.q_depth;

	WARN(ena_dev->aenq.head != depth, "Invalid AENQ state\n");

	/* Init head_db to mark that all entries in the queue
	 * are initially available
	 */
	writel(depth, ena_dev->reg_bar + ENA_REGS_AENQ_HEAD_DB_OFF);
}

int ena_com_set_aenq_config(struct ena_com_dev *ena_dev, u32 groups_flag)
{
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	struct ena_admin_get_feat_resp get_resp;
	int ret;

	ret = ena_com_get_feature(ena_dev, &get_resp, ENA_ADMIN_AENQ_CONFIG);
	if (ret) {
		pr_info("Can't get aenq configuration\n");
		return ret;
	}

	if ((get_resp.u.aenq.supported_groups & groups_flag) != groups_flag) {
		pr_warn("Trying to set unsupported aenq events. supported flag: %x asked flag: %x\n",
			get_resp.u.aenq.supported_groups, groups_flag);
		return -EPERM;
	}

	memset(&cmd, 0x0, sizeof(cmd));
	admin_queue = &ena_dev->admin_queue;

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags = 0;
	cmd.feat_common.feature_id = ENA_ADMIN_AENQ_CONFIG;
	cmd.u.aenq.enabled_groups = groups_flag;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		pr_err("Failed to config AENQ ret: %d\n", ret);

	return ret;
}

int ena_com_get_dma_width(struct ena_com_dev *ena_dev)
{
	u32 caps = ena_com_reg_bar_read32(ena_dev, ENA_REGS_CAPS_OFF);
	int width;

	if (unlikely(caps == ENA_MMIO_READ_TIMEOUT)) {
		pr_err("Reg read timeout occurred\n");
		return -ETIME;
	}

	width = (caps & ENA_REGS_CAPS_DMA_ADDR_WIDTH_MASK) >>
		ENA_REGS_CAPS_DMA_ADDR_WIDTH_SHIFT;

	pr_debug("ENA dma width: %d\n", width);

	if ((width < 32) || width > ENA_MAX_PHYS_ADDR_SIZE_BITS) {
		pr_err("DMA width illegal value: %d\n", width);
		return -EINVAL;
	}

	ena_dev->dma_addr_bits = width;

	return width;
}

int ena_com_validate_version(struct ena_com_dev *ena_dev)
{
	u32 ver;
	u32 ctrl_ver;
	u32 ctrl_ver_masked;

	/* Make sure the ENA version and the controller version are at least
	 * as the driver expects
	 */
	ver = ena_com_reg_bar_read32(ena_dev, ENA_REGS_VERSION_OFF);
	ctrl_ver = ena_com_reg_bar_read32(ena_dev,
					  ENA_REGS_CONTROLLER_VERSION_OFF);

	if (unlikely((ver == ENA_MMIO_READ_TIMEOUT) ||
		     (ctrl_ver == ENA_MMIO_READ_TIMEOUT))) {
		pr_err("Reg read timeout occurred\n");
		return -ETIME;
	}

	pr_info("ena device version: %d.%d\n",
		(ver & ENA_REGS_VERSION_MAJOR_VERSION_MASK) >>
			ENA_REGS_VERSION_MAJOR_VERSION_SHIFT,
		ver & ENA_REGS_VERSION_MINOR_VERSION_MASK);

	if (ver < MIN_ENA_VER) {
		pr_err("ENA version is lower than the minimal version the driver supports\n");
		return -1;
	}

	pr_info("ena controller version: %d.%d.%d implementation version %d\n",
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK) >>
			ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT,
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK) >>
			ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT,
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK),
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_IMPL_ID_MASK) >>
			ENA_REGS_CONTROLLER_VERSION_IMPL_ID_SHIFT);

	ctrl_ver_masked =
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK) |
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK) |
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK);

	/* Validate the ctrl version without the implementation ID */
	if (ctrl_ver_masked < MIN_ENA_CTRL_VER) {
		pr_err("ENA ctrl version is lower than the minimal ctrl version the driver supports\n");
		return -1;
	}

	return 0;
}

void ena_com_admin_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_com_admin_cq *cq = &admin_queue->cq;
	struct ena_com_admin_sq *sq = &admin_queue->sq;
	struct ena_com_aenq *aenq = &ena_dev->aenq;
	u16 size;

	if (admin_queue->comp_ctx)
		devm_kfree(ena_dev->dmadev, admin_queue->comp_ctx);
	admin_queue->comp_ctx = NULL;
	size = ADMIN_SQ_SIZE(admin_queue->q_depth);
	if (sq->entries)
		dma_free_coherent(ena_dev->dmadev, size, sq->entries,
				  sq->dma_addr);
	sq->entries = NULL;

	size = ADMIN_CQ_SIZE(admin_queue->q_depth);
	if (cq->entries)
		dma_free_coherent(ena_dev->dmadev, size, cq->entries,
				  cq->dma_addr);
	cq->entries = NULL;

	size = ADMIN_AENQ_SIZE(aenq->q_depth);
	if (ena_dev->aenq.entries)
		dma_free_coherent(ena_dev->dmadev, size, aenq->entries,
				  aenq->dma_addr);
	aenq->entries = NULL;
}

void ena_com_set_admin_polling_mode(struct ena_com_dev *ena_dev, bool polling)
{
	ena_dev->admin_queue.polling = polling;
}

int ena_com_mmio_reg_read_request_init(struct ena_com_dev *ena_dev)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;

	spin_lock_init(&mmio_read->lock);
	mmio_read->read_resp =
		dma_zalloc_coherent(ena_dev->dmadev,
				    sizeof(*mmio_read->read_resp),
				    &mmio_read->read_resp_dma_addr, GFP_KERNEL);
	if (unlikely(!mmio_read->read_resp))
		return -ENOMEM;

	ena_com_mmio_reg_read_request_write_dev_addr(ena_dev);

	mmio_read->read_resp->req_id = 0x0;
	mmio_read->seq_num = 0x0;
	mmio_read->readless_supported = true;

	return 0;
}

void ena_com_set_mmio_read_mode(struct ena_com_dev *ena_dev, bool readless_supported)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;

	mmio_read->readless_supported = readless_supported;
}

void ena_com_mmio_reg_read_request_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;

	writel(0x0, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_LO_OFF);
	writel(0x0, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_HI_OFF);

	dma_free_coherent(ena_dev->dmadev, sizeof(*mmio_read->read_resp),
			  mmio_read->read_resp, mmio_read->read_resp_dma_addr);

	mmio_read->read_resp = NULL;
}

void ena_com_mmio_reg_read_request_write_dev_addr(struct ena_com_dev *ena_dev)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;
	u32 addr_low, addr_high;

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(mmio_read->read_resp_dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(mmio_read->read_resp_dma_addr);

	writel(addr_low, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_LO_OFF);
	writel(addr_high, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_HI_OFF);
}

int ena_com_admin_init(struct ena_com_dev *ena_dev,
		       struct ena_aenq_handlers *aenq_handlers,
		       bool init_spinlock)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	u32 aq_caps, acq_caps, dev_sts, addr_low, addr_high;
	int ret;

	dev_sts = ena_com_reg_bar_read32(ena_dev, ENA_REGS_DEV_STS_OFF);

	if (unlikely(dev_sts == ENA_MMIO_READ_TIMEOUT)) {
		pr_err("Reg read timeout occurred\n");
		return -ETIME;
	}

	if (!(dev_sts & ENA_REGS_DEV_STS_READY_MASK)) {
		pr_err("Device isn't ready, abort com init\n");
		return -ENODEV;
	}

	admin_queue->q_depth = ENA_ADMIN_QUEUE_DEPTH;

	admin_queue->q_dmadev = ena_dev->dmadev;
	admin_queue->polling = false;
	admin_queue->curr_cmd_id = 0;

	atomic_set(&admin_queue->outstanding_cmds, 0);

	if (init_spinlock)
		spin_lock_init(&admin_queue->q_lock);

	ret = ena_com_init_comp_ctxt(admin_queue);
	if (ret)
		goto error;

	ret = ena_com_admin_init_sq(admin_queue);
	if (ret)
		goto error;

	ret = ena_com_admin_init_cq(admin_queue);
	if (ret)
		goto error;

	admin_queue->sq.db_addr = (u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
		ENA_REGS_AQ_DB_OFF);

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(admin_queue->sq.dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(admin_queue->sq.dma_addr);

	writel(addr_low, ena_dev->reg_bar + ENA_REGS_AQ_BASE_LO_OFF);
	writel(addr_high, ena_dev->reg_bar + ENA_REGS_AQ_BASE_HI_OFF);

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(admin_queue->cq.dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(admin_queue->cq.dma_addr);

	writel(addr_low, ena_dev->reg_bar + ENA_REGS_ACQ_BASE_LO_OFF);
	writel(addr_high, ena_dev->reg_bar + ENA_REGS_ACQ_BASE_HI_OFF);

	aq_caps = 0;
	aq_caps |= admin_queue->q_depth & ENA_REGS_AQ_CAPS_AQ_DEPTH_MASK;
	aq_caps |= (sizeof(struct ena_admin_aq_entry) <<
			ENA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_SHIFT) &
			ENA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_MASK;

	acq_caps = 0;
	acq_caps |= admin_queue->q_depth & ENA_REGS_ACQ_CAPS_ACQ_DEPTH_MASK;
	acq_caps |= (sizeof(struct ena_admin_acq_entry) <<
		ENA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_SHIFT) &
		ENA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_MASK;

	writel(aq_caps, ena_dev->reg_bar + ENA_REGS_AQ_CAPS_OFF);
	writel(acq_caps, ena_dev->reg_bar + ENA_REGS_ACQ_CAPS_OFF);
	ret = ena_com_admin_init_aenq(ena_dev, aenq_handlers);
	if (ret)
		goto error;

	admin_queue->running_state = true;

	return 0;
error:
	ena_com_admin_destroy(ena_dev);

	return ret;
}

int ena_com_create_io_queue(struct ena_com_dev *ena_dev,
			    struct ena_com_create_io_ctx *ctx)
{
	struct ena_com_io_sq *io_sq;
	struct ena_com_io_cq *io_cq;
	int ret;

	if (ctx->qid >= ENA_TOTAL_NUM_QUEUES) {
		pr_err("Qid (%d) is bigger than max num of queues (%d)\n",
		       ctx->qid, ENA_TOTAL_NUM_QUEUES);
		return -EINVAL;
	}

	io_sq = &ena_dev->io_sq_queues[ctx->qid];
	io_cq = &ena_dev->io_cq_queues[ctx->qid];

	memset(io_sq, 0x0, sizeof(struct ena_com_io_sq));
	memset(io_cq, 0x0, sizeof(struct ena_com_io_cq));

	/* Init CQ */
	io_cq->q_depth = ctx->queue_size;
	io_cq->direction = ctx->direction;
	io_cq->qid = ctx->qid;

	io_cq->msix_vector = ctx->msix_vector;

	io_sq->q_depth = ctx->queue_size;
	io_sq->direction = ctx->direction;
	io_sq->qid = ctx->qid;

	io_sq->mem_queue_type = ctx->mem_queue_type;

	if (ctx->direction == ENA_COM_IO_QUEUE_DIRECTION_TX)
		/* header length is limited to 8 bits */
		io_sq->tx_max_header_size =
			min_t(u32, ena_dev->tx_max_header_size, SZ_256);

	ret = ena_com_init_io_sq(ena_dev, ctx, io_sq);
	if (ret)
		goto error;
	ret = ena_com_init_io_cq(ena_dev, ctx, io_cq);
	if (ret)
		goto error;

	ret = ena_com_create_io_cq(ena_dev, io_cq);
	if (ret)
		goto error;

	ret = ena_com_create_io_sq(ena_dev, io_sq, io_cq->idx);
	if (ret)
		goto destroy_io_cq;

	return 0;

destroy_io_cq:
	ena_com_destroy_io_cq(ena_dev, io_cq);
error:
	ena_com_io_queue_free(ena_dev, io_sq, io_cq);
	return ret;
}

void ena_com_destroy_io_queue(struct ena_com_dev *ena_dev, u16 qid)
{
	struct ena_com_io_sq *io_sq;
	struct ena_com_io_cq *io_cq;

	if (qid >= ENA_TOTAL_NUM_QUEUES) {
		pr_err("Qid (%d) is bigger than max num of queues (%d)\n", qid,
		       ENA_TOTAL_NUM_QUEUES);
		return;
	}

	io_sq = &ena_dev->io_sq_queues[qid];
	io_cq = &ena_dev->io_cq_queues[qid];

	ena_com_destroy_io_sq(ena_dev, io_sq);
	ena_com_destroy_io_cq(ena_dev, io_cq);

	ena_com_io_queue_free(ena_dev, io_sq, io_cq);
}

int ena_com_get_link_params(struct ena_com_dev *ena_dev,
			    struct ena_admin_get_feat_resp *resp)
{
	return ena_com_get_feature(ena_dev, resp, ENA_ADMIN_LINK_CONFIG);
}

int ena_com_get_dev_attr_feat(struct ena_com_dev *ena_dev,
			      struct ena_com_dev_get_features_ctx *get_feat_ctx)
{
	struct ena_admin_get_feat_resp get_resp;
	int rc;

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_DEVICE_ATTRIBUTES);
	if (rc)
		return rc;

	memcpy(&get_feat_ctx->dev_attr, &get_resp.u.dev_attr,
	       sizeof(get_resp.u.dev_attr));
	ena_dev->supported_features = get_resp.u.dev_attr.supported_features;

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_MAX_QUEUES_NUM);
	if (rc)
		return rc;

	memcpy(&get_feat_ctx->max_queues, &get_resp.u.max_queue,
	       sizeof(get_resp.u.max_queue));
	ena_dev->tx_max_header_size = get_resp.u.max_queue.max_header_size;

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_AENQ_CONFIG);
	if (rc)
		return rc;

	memcpy(&get_feat_ctx->aenq, &get_resp.u.aenq,
	       sizeof(get_resp.u.aenq));

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_STATELESS_OFFLOAD_CONFIG);
	if (rc)
		return rc;

	memcpy(&get_feat_ctx->offload, &get_resp.u.offload,
	       sizeof(get_resp.u.offload));

	return 0;
}

void ena_com_admin_q_comp_intr_handler(struct ena_com_dev *ena_dev)
{
	ena_com_handle_admin_completion(&ena_dev->admin_queue);
}

/* ena_handle_specific_aenq_event:
 * return the handler that is relevant to the specific event group
 */
static ena_aenq_handler ena_com_get_specific_aenq_cb(struct ena_com_dev *dev,
						     u16 group)
{
	struct ena_aenq_handlers *aenq_handlers = dev->aenq.aenq_handlers;

	if ((group < ENA_MAX_HANDLERS) && aenq_handlers->handlers[group])
		return aenq_handlers->handlers[group];

	return aenq_handlers->unimplemented_handler;
}

/* ena_aenq_intr_handler:
 * handles the aenq incoming events.
 * pop events from the queue and apply the specific handler
 */
void ena_com_aenq_intr_handler(struct ena_com_dev *dev, void *data)
{
	struct ena_admin_aenq_entry *aenq_e;
	struct ena_admin_aenq_common_desc *aenq_common;
	struct ena_com_aenq *aenq  = &dev->aenq;
	ena_aenq_handler handler_cb;
	u16 masked_head, processed = 0;
	u8 phase;

	masked_head = aenq->head & (aenq->q_depth - 1);
	phase = aenq->phase;
	aenq_e = &aenq->entries[masked_head]; /* Get first entry */
	aenq_common = &aenq_e->aenq_common_desc;

	/* Go over all the events */
	while ((aenq_common->flags & ENA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK) ==
	       phase) {
		pr_debug("AENQ! Group[%x] Syndrom[%x] timestamp: [%llus]\n",
			 aenq_common->group, aenq_common->syndrom,
			 (u64)aenq_common->timestamp_low +
				 ((u64)aenq_common->timestamp_high << 32));

		/* Handle specific event*/
		handler_cb = ena_com_get_specific_aenq_cb(dev,
							  aenq_common->group);
		handler_cb(data, aenq_e); /* call the actual event handler*/

		/* Get next event entry */
		masked_head++;
		processed++;

		if (unlikely(masked_head == aenq->q_depth)) {
			masked_head = 0;
			phase = !phase;
		}
		aenq_e = &aenq->entries[masked_head];
		aenq_common = &aenq_e->aenq_common_desc;
	}

	aenq->head += processed;
	aenq->phase = phase;

	/* Don't update aenq doorbell if there weren't any processed events */
	if (!processed)
		return;

	/* write the aenq doorbell after all AENQ descriptors were read */
	mb();
	writel((u32)aenq->head, dev->reg_bar + ENA_REGS_AENQ_HEAD_DB_OFF);
}

int ena_com_dev_reset(struct ena_com_dev *ena_dev)
{
	u32 stat, timeout, cap, reset_val;
	int rc;

	stat = ena_com_reg_bar_read32(ena_dev, ENA_REGS_DEV_STS_OFF);
	cap = ena_com_reg_bar_read32(ena_dev, ENA_REGS_CAPS_OFF);

	if (unlikely((stat == ENA_MMIO_READ_TIMEOUT) ||
		     (cap == ENA_MMIO_READ_TIMEOUT))) {
		pr_err("Reg read32 timeout occurred\n");
		return -ETIME;
	}

	if ((stat & ENA_REGS_DEV_STS_READY_MASK) == 0) {
		pr_err("Device isn't ready, can't reset device\n");
		return -EINVAL;
	}

	timeout = (cap & ENA_REGS_CAPS_RESET_TIMEOUT_MASK) >>
			ENA_REGS_CAPS_RESET_TIMEOUT_SHIFT;
	if (timeout == 0) {
		pr_err("Invalid timeout value\n");
		return -EINVAL;
	}

	/* start reset */
	reset_val = ENA_REGS_DEV_CTL_DEV_RESET_MASK;
	writel(reset_val, ena_dev->reg_bar + ENA_REGS_DEV_CTL_OFF);

	/* Write again the MMIO read request address */
	ena_com_mmio_reg_read_request_write_dev_addr(ena_dev);

	rc = wait_for_reset_state(ena_dev, timeout,
				  ENA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK);
	if (rc != 0) {
		pr_err("Reset indication didn't turn on\n");
		return rc;
	}

	/* reset done */
	writel(0, ena_dev->reg_bar + ENA_REGS_DEV_CTL_OFF);
	rc = wait_for_reset_state(ena_dev, timeout, 0);
	if (rc != 0) {
		pr_err("Reset indication didn't turn off\n");
		return rc;
	}

	return 0;
}

static int ena_get_dev_stats(struct ena_com_dev *ena_dev,
			     struct ena_com_stats_ctx *ctx,
			     enum ena_admin_get_stats_type type)
{
	struct ena_admin_aq_get_stats_cmd *get_cmd = &ctx->get_cmd;
	struct ena_admin_acq_get_stats_resp *get_resp = &ctx->get_resp;
	struct ena_com_admin_queue *admin_queue;
	int ret;

	admin_queue = &ena_dev->admin_queue;

	get_cmd->aq_common_descriptor.opcode = ENA_ADMIN_GET_STATS;
	get_cmd->aq_common_descriptor.flags = 0;
	get_cmd->type = type;

	ret =  ena_com_execute_admin_command(admin_queue,
					     (struct ena_admin_aq_entry *)get_cmd,
					     sizeof(*get_cmd),
					     (struct ena_admin_acq_entry *)get_resp,
					     sizeof(*get_resp));

	if (unlikely(ret))
		pr_err("Failed to get stats. error: %d\n", ret);

	return ret;
}

int ena_com_get_dev_basic_stats(struct ena_com_dev *ena_dev,
				struct ena_admin_basic_stats *stats)
{
	struct ena_com_stats_ctx ctx;
	int ret;

	memset(&ctx, 0x0, sizeof(ctx));
	ret = ena_get_dev_stats(ena_dev, &ctx, ENA_ADMIN_GET_STATS_TYPE_BASIC);
	if (likely(ret == 0))
		memcpy(stats, &ctx.get_resp.basic_stats,
		       sizeof(ctx.get_resp.basic_stats));

	return ret;
}

int ena_com_set_dev_mtu(struct ena_com_dev *ena_dev, int mtu)
{
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev, ENA_ADMIN_MTU)) {
		pr_info("Feature %d isn't supported\n", ENA_ADMIN_MTU);
		return -EPERM;
	}

	memset(&cmd, 0x0, sizeof(cmd));
	admin_queue = &ena_dev->admin_queue;

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags = 0;
	cmd.feat_common.feature_id = ENA_ADMIN_MTU;
	cmd.u.mtu.mtu = mtu;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		pr_err("Failed to set mtu %d. error: %d\n", mtu, ret);

	return ret;
}

int ena_com_get_offload_settings(struct ena_com_dev *ena_dev,
				 struct ena_admin_feature_offload_desc *offload)
{
	int ret;
	struct ena_admin_get_feat_resp resp;

	ret = ena_com_get_feature(ena_dev, &resp,
				  ENA_ADMIN_STATELESS_OFFLOAD_CONFIG);
	if (unlikely(ret)) {
		pr_err("Failed to get offload capabilities %d\n", ret);
		return ret;
	}

	memcpy(offload, &resp.u.offload, sizeof(resp.u.offload));

	return 0;
}

int ena_com_set_hash_function(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	struct ena_admin_get_feat_resp get_resp;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev,
						ENA_ADMIN_RSS_HASH_FUNCTION)) {
		pr_info("Feature %d isn't supported\n",
			ENA_ADMIN_RSS_HASH_FUNCTION);
		return -EPERM;
	}

	/* Validate hash function is supported */
	ret = ena_com_get_feature(ena_dev, &get_resp,
				  ENA_ADMIN_RSS_HASH_FUNCTION);
	if (unlikely(ret))
		return ret;

	if (get_resp.u.flow_hash_func.supported_func & (1 << rss->hash_func)) {
		pr_err("Func hash %d isn't supported by device, abort\n",
		       rss->hash_func);
		return -EPERM;
	}

	memset(&cmd, 0x0, sizeof(cmd));

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags =
		ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	cmd.feat_common.feature_id = ENA_ADMIN_RSS_HASH_FUNCTION;
	cmd.u.flow_hash_func.init_val = rss->hash_init_val;
	cmd.u.flow_hash_func.selected_func = 1 << rss->hash_func;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.control_buffer.address,
				   rss->hash_key_dma_addr);
	if (unlikely(ret)) {
		pr_err("memory address set failed\n");
		return ret;
	}

	cmd.control_buffer.length = sizeof(*rss->hash_key);

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));
	if (unlikely(ret)) {
		pr_err("Failed to set hash function %d. error: %d\n",
		       rss->hash_func, ret);
		return -EINVAL;
	}

	return 0;
}

int ena_com_fill_hash_function(struct ena_com_dev *ena_dev,
			       enum ena_admin_hash_functions func,
			       const u8 *key, u16 key_len, u32 init_val)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	struct ena_admin_feature_rss_flow_hash_control *hash_key =
		rss->hash_key;
	int rc;

	/* Make sure size is a mult of DWs */
	if (unlikely(key_len & 0x3))
		return -EINVAL;

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_HASH_FUNCTION,
				    rss->hash_key_dma_addr,
				    sizeof(*rss->hash_key));
	if (unlikely(rc))
		return rc;

	if (!((1 << func) & get_resp.u.flow_hash_func.supported_func)) {
		pr_err("Flow hash function %d isn't supported\n", func);
		return -EPERM;
	}

	switch (func) {
	case ENA_ADMIN_TOEPLITZ:
		if (key_len > sizeof(hash_key->key)) {
			pr_err("key len (%hu) is bigger than the max supported (%zu)\n",
			       key_len, sizeof(hash_key->key));
			return -EINVAL;
		}

		memcpy(hash_key->key, key, key_len);
		rss->hash_init_val = init_val;
		hash_key->keys_num = key_len >> 2;
		break;
	case ENA_ADMIN_CRC32:
		rss->hash_init_val = init_val;
		break;
	default:
		pr_err("Invalid hash function (%d)\n", func);
		return -EINVAL;
	}

	rc = ena_com_set_hash_function(ena_dev);

	/* Restore the old function */
	if (unlikely(rc))
		ena_com_get_hash_function(ena_dev, NULL, NULL);

	return rc;
}

int ena_com_get_hash_function(struct ena_com_dev *ena_dev,
			      enum ena_admin_hash_functions *func,
			      u8 *key)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	struct ena_admin_feature_rss_flow_hash_control *hash_key =
		rss->hash_key;
	int rc;

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_HASH_FUNCTION,
				    rss->hash_key_dma_addr,
				    sizeof(*rss->hash_key));
	if (unlikely(rc))
		return rc;

	rss->hash_func = get_resp.u.flow_hash_func.selected_func;
	if (func)
		*func = rss->hash_func;

	if (key)
		memcpy(key, hash_key->key, (size_t)(hash_key->keys_num) << 2);

	return 0;
}

int ena_com_get_hash_ctrl(struct ena_com_dev *ena_dev,
			  enum ena_admin_flow_hash_proto proto,
			  u16 *fields)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	int rc;

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_HASH_INPUT,
				    rss->hash_ctrl_dma_addr,
				    sizeof(*rss->hash_ctrl));
	if (unlikely(rc))
		return rc;

	if (fields)
		*fields = rss->hash_ctrl->selected_fields[proto].fields;

	return 0;
}

int ena_com_set_hash_ctrl(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_feature_rss_hash_control *hash_ctrl = rss->hash_ctrl;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev,
						ENA_ADMIN_RSS_HASH_INPUT)) {
		pr_info("Feature %d isn't supported\n", ENA_ADMIN_RSS_HASH_INPUT);
		return -EPERM;
	}

	memset(&cmd, 0x0, sizeof(cmd));

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags =
		ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	cmd.feat_common.feature_id = ENA_ADMIN_RSS_HASH_INPUT;
	cmd.u.flow_hash_input.enabled_input_sort =
		ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L3_SORT_MASK |
		ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L4_SORT_MASK;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.control_buffer.address,
				   rss->hash_ctrl_dma_addr);
	if (unlikely(ret)) {
		pr_err("memory address set failed\n");
		return ret;
	}
	cmd.control_buffer.length = sizeof(*hash_ctrl);

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));
	if (unlikely(ret))
		pr_err("Failed to set hash input. error: %d\n", ret);

	return ret;
}

int ena_com_set_default_hash_ctrl(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_feature_rss_hash_control *hash_ctrl =
		rss->hash_ctrl;
	u16 available_fields = 0;
	int rc, i;

	/* Get the supported hash input */
	rc = ena_com_get_hash_ctrl(ena_dev, 0, NULL);
	if (unlikely(rc))
		return rc;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_TCP4].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_UDP4].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_TCP6].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_UDP6].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_IP4].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_IP6].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_IP4_FRAG].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_IP4_FRAG].fields =
		ENA_ADMIN_RSS_L2_DA | ENA_ADMIN_RSS_L2_SA;

	for (i = 0; i < ENA_ADMIN_RSS_PROTO_NUM; i++) {
		available_fields = hash_ctrl->selected_fields[i].fields &
				hash_ctrl->supported_fields[i].fields;
		if (available_fields != hash_ctrl->selected_fields[i].fields) {
			pr_err("hash control doesn't support all the desire configuration. proto %x supported %x selected %x\n",
			       i, hash_ctrl->supported_fields[i].fields,
			       hash_ctrl->selected_fields[i].fields);
			return -EPERM;
		}
	}

	rc = ena_com_set_hash_ctrl(ena_dev);

	/* In case of failure, restore the old hash ctrl */
	if (unlikely(rc))
		ena_com_get_hash_ctrl(ena_dev, 0, NULL);

	return rc;
}

int ena_com_fill_hash_ctrl(struct ena_com_dev *ena_dev,
			   enum ena_admin_flow_hash_proto proto,
			   u16 hash_fields)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_feature_rss_hash_control *hash_ctrl = rss->hash_ctrl;
	u16 supported_fields;
	int rc;

	if (proto >= ENA_ADMIN_RSS_PROTO_NUM) {
		pr_err("Invalid proto num (%u)\n", proto);
		return -EINVAL;
	}

	/* Get the ctrl table */
	rc = ena_com_get_hash_ctrl(ena_dev, proto, NULL);
	if (unlikely(rc))
		return rc;

	/* Make sure all the fields are supported */
	supported_fields = hash_ctrl->supported_fields[proto].fields;
	if ((hash_fields & supported_fields) != hash_fields) {
		pr_err("proto %d doesn't support the required fields %x. supports only: %x\n",
		       proto, hash_fields, supported_fields);
	}

	hash_ctrl->selected_fields[proto].fields = hash_fields;

	rc = ena_com_set_hash_ctrl(ena_dev);

	/* In case of failure, restore the old hash ctrl */
	if (unlikely(rc))
		ena_com_get_hash_ctrl(ena_dev, 0, NULL);

	return 0;
}

int ena_com_indirect_table_fill_entry(struct ena_com_dev *ena_dev,
				      u16 entry_idx, u16 entry_value)
{
	struct ena_rss *rss = &ena_dev->rss;

	if (unlikely(entry_idx >= (1 << rss->tbl_log_size)))
		return -EINVAL;

	if (unlikely((entry_value > ENA_TOTAL_NUM_QUEUES)))
		return -EINVAL;

	rss->host_rss_ind_tbl[entry_idx] = entry_value;

	return 0;
}

int ena_com_indirect_table_set(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	int ret;

	if (!ena_com_check_supported_feature_id(
		    ena_dev, ENA_ADMIN_RSS_REDIRECTION_TABLE_CONFIG)) {
		pr_info("Feature %d isn't supported\n",
			ENA_ADMIN_RSS_REDIRECTION_TABLE_CONFIG);
		return -EPERM;
	}

	ret = ena_com_ind_tbl_convert_to_device(ena_dev);
	if (ret) {
		pr_err("Failed to convert host indirection table to device table\n");
		return ret;
	}

	memset(&cmd, 0x0, sizeof(cmd));

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags =
		ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	cmd.feat_common.feature_id = ENA_ADMIN_RSS_REDIRECTION_TABLE_CONFIG;
	cmd.u.ind_table.size = rss->tbl_log_size;
	cmd.u.ind_table.inline_index = 0xFFFFFFFF;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.control_buffer.address,
				   rss->rss_ind_tbl_dma_addr);
	if (unlikely(ret)) {
		pr_err("memory address set failed\n");
		return ret;
	}

	cmd.control_buffer.length = (1ULL << rss->tbl_log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		pr_err("Failed to set indirect table. error: %d\n", ret);

	return ret;
}

int ena_com_indirect_table_get(struct ena_com_dev *ena_dev, u32 *ind_tbl)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	u32 tbl_size;
	int i, rc;

	tbl_size = (1ULL << rss->tbl_log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_REDIRECTION_TABLE_CONFIG,
				    rss->rss_ind_tbl_dma_addr,
				    tbl_size);
	if (unlikely(rc))
		return rc;

	if (!ind_tbl)
		return 0;

	rc = ena_com_ind_tbl_convert_from_device(ena_dev);
	if (unlikely(rc))
		return rc;

	for (i = 0; i < (1 << rss->tbl_log_size); i++)
		ind_tbl[i] = rss->host_rss_ind_tbl[i];

	return 0;
}

int ena_com_rss_init(struct ena_com_dev *ena_dev, u16 indr_tbl_log_size)
{
	int rc;

	memset(&ena_dev->rss, 0x0, sizeof(ena_dev->rss));

	rc = ena_com_indirect_table_allocate(ena_dev, indr_tbl_log_size);
	if (unlikely(rc))
		goto err_indr_tbl;

	rc = ena_com_hash_key_allocate(ena_dev);
	if (unlikely(rc))
		goto err_hash_key;

	rc = ena_com_hash_ctrl_init(ena_dev);
	if (unlikely(rc))
		goto err_hash_ctrl;

	return 0;

err_hash_ctrl:
	ena_com_hash_key_destroy(ena_dev);
err_hash_key:
	ena_com_indirect_table_destroy(ena_dev);
err_indr_tbl:

	return rc;
}

void ena_com_rss_destroy(struct ena_com_dev *ena_dev)
{
	ena_com_indirect_table_destroy(ena_dev);
	ena_com_hash_key_destroy(ena_dev);
	ena_com_hash_ctrl_destroy(ena_dev);

	memset(&ena_dev->rss, 0x0, sizeof(ena_dev->rss));
}

int ena_com_allocate_host_info(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	host_attr->host_info =
		dma_zalloc_coherent(ena_dev->dmadev, SZ_4K,
				    &host_attr->host_info_dma_addr, GFP_KERNEL);
	if (unlikely(!host_attr->host_info))
		return -ENOMEM;

	return 0;
}

int ena_com_allocate_debug_area(struct ena_com_dev *ena_dev,
				u32 debug_area_size)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	host_attr->debug_area_virt_addr =
		dma_zalloc_coherent(ena_dev->dmadev, debug_area_size,
				    &host_attr->debug_area_dma_addr, GFP_KERNEL);
	if (unlikely(!host_attr->debug_area_virt_addr)) {
		host_attr->debug_area_size = 0;
		return -ENOMEM;
	}

	host_attr->debug_area_size = debug_area_size;

	return 0;
}

void ena_com_delete_host_info(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	if (host_attr->host_info) {
		dma_free_coherent(ena_dev->dmadev, SZ_4K, host_attr->host_info,
				  host_attr->host_info_dma_addr);
		host_attr->host_info = NULL;
	}
}

void ena_com_delete_debug_area(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	if (host_attr->debug_area_virt_addr) {
		dma_free_coherent(ena_dev->dmadev, host_attr->debug_area_size,
				  host_attr->debug_area_virt_addr,
				  host_attr->debug_area_dma_addr);
		host_attr->debug_area_virt_addr = NULL;
	}
}

int ena_com_set_host_attributes(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;

	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev,
						ENA_ADMIN_HOST_ATTR_CONFIG)) {
		pr_warn("Set host attribute isn't supported\n");
		return -EPERM;
	}

	memset(&cmd, 0x0, sizeof(cmd));
	admin_queue = &ena_dev->admin_queue;

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.feat_common.feature_id = ENA_ADMIN_HOST_ATTR_CONFIG;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.u.host_attr.debug_ba,
				   host_attr->debug_area_dma_addr);
	if (unlikely(ret)) {
		pr_err("memory address set failed\n");
		return ret;
	}

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.u.host_attr.os_info_ba,
				   host_attr->host_info_dma_addr);
	if (unlikely(ret)) {
		pr_err("memory address set failed\n");
		return ret;
	}

	cmd.u.host_attr.debug_area_size = host_attr->debug_area_size;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		pr_err("Failed to set host attributes: %d\n", ret);

	return ret;
}

/* Interrupt moderation */
bool ena_com_interrupt_moderation_supported(struct ena_com_dev *ena_dev)
{
	return ena_com_check_supported_feature_id(ena_dev,
						  ENA_ADMIN_INTERRUPT_MODERATION);
}

int ena_com_update_nonadaptive_moderation_interval_tx(struct ena_com_dev *ena_dev,
						      u32 tx_coalesce_usecs)
{
	if (!ena_dev->intr_delay_resolution) {
		pr_err("Illegal interrupt delay granularity value\n");
		return -EFAULT;
	}

	ena_dev->intr_moder_tx_interval = tx_coalesce_usecs /
		ena_dev->intr_delay_resolution;

	return 0;
}

int ena_com_update_nonadaptive_moderation_interval_rx(struct ena_com_dev *ena_dev,
						      u32 rx_coalesce_usecs)
{
	if (!ena_dev->intr_delay_resolution) {
		pr_err("Illegal interrupt delay granularity value\n");
		return -EFAULT;
	}

	/* We use LOWEST entry of moderation table for storing
	 * nonadaptive interrupt coalescing values
	 */
	ena_dev->intr_moder_tbl[ENA_INTR_MODER_LOWEST].intr_moder_interval =
		rx_coalesce_usecs / ena_dev->intr_delay_resolution;

	return 0;
}

void ena_com_destroy_interrupt_moderation(struct ena_com_dev *ena_dev)
{
	if (ena_dev->intr_moder_tbl)
		devm_kfree(ena_dev->dmadev, ena_dev->intr_moder_tbl);
	ena_dev->intr_moder_tbl = NULL;
}

int ena_com_init_interrupt_moderation(struct ena_com_dev *ena_dev)
{
	struct ena_admin_get_feat_resp get_resp;
	u16 delay_resolution;
	int rc;

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_INTERRUPT_MODERATION);

	if (rc) {
		if (rc == -EPERM) {
			pr_info("Feature %d isn't supported\n",
				ENA_ADMIN_INTERRUPT_MODERATION);
			rc = 0;
		} else {
			pr_err("Failed to get interrupt moderation admin cmd. rc: %d\n",
			       rc);
		}

		/* no moderation supported, disable adaptive support */
		ena_com_disable_adaptive_moderation(ena_dev);
		return rc;
	}

	rc = ena_com_init_interrupt_moderation_table(ena_dev);
	if (rc)
		goto err;

	/* if moderation is supported by device we set adaptive moderation */
	delay_resolution = get_resp.u.intr_moderation.intr_delay_resolution;
	ena_com_update_intr_delay_resolution(ena_dev, delay_resolution);
	ena_com_enable_adaptive_moderation(ena_dev);

	return 0;
err:
	ena_com_destroy_interrupt_moderation(ena_dev);
	return rc;
}

void ena_com_config_default_interrupt_moderation_table(struct ena_com_dev *ena_dev)
{
	struct ena_intr_moder_entry *intr_moder_tbl = ena_dev->intr_moder_tbl;

	if (!intr_moder_tbl)
		return;

	intr_moder_tbl[ENA_INTR_MODER_LOWEST].intr_moder_interval =
		ENA_INTR_LOWEST_USECS;
	intr_moder_tbl[ENA_INTR_MODER_LOWEST].pkts_per_interval =
		ENA_INTR_LOWEST_PKTS;
	intr_moder_tbl[ENA_INTR_MODER_LOWEST].bytes_per_interval =
		ENA_INTR_LOWEST_BYTES;

	intr_moder_tbl[ENA_INTR_MODER_LOW].intr_moder_interval =
		ENA_INTR_LOW_USECS;
	intr_moder_tbl[ENA_INTR_MODER_LOW].pkts_per_interval =
		ENA_INTR_LOW_PKTS;
	intr_moder_tbl[ENA_INTR_MODER_LOW].bytes_per_interval =
		ENA_INTR_LOW_BYTES;

	intr_moder_tbl[ENA_INTR_MODER_MID].intr_moder_interval =
		ENA_INTR_MID_USECS;
	intr_moder_tbl[ENA_INTR_MODER_MID].pkts_per_interval =
		ENA_INTR_MID_PKTS;
	intr_moder_tbl[ENA_INTR_MODER_MID].bytes_per_interval =
		ENA_INTR_MID_BYTES;

	intr_moder_tbl[ENA_INTR_MODER_HIGH].intr_moder_interval =
		ENA_INTR_HIGH_USECS;
	intr_moder_tbl[ENA_INTR_MODER_HIGH].pkts_per_interval =
		ENA_INTR_HIGH_PKTS;
	intr_moder_tbl[ENA_INTR_MODER_HIGH].bytes_per_interval =
		ENA_INTR_HIGH_BYTES;

	intr_moder_tbl[ENA_INTR_MODER_HIGHEST].intr_moder_interval =
		ENA_INTR_HIGHEST_USECS;
	intr_moder_tbl[ENA_INTR_MODER_HIGHEST].pkts_per_interval =
		ENA_INTR_HIGHEST_PKTS;
	intr_moder_tbl[ENA_INTR_MODER_HIGHEST].bytes_per_interval =
		ENA_INTR_HIGHEST_BYTES;
}

unsigned int ena_com_get_nonadaptive_moderation_interval_tx(struct ena_com_dev *ena_dev)
{
	return ena_dev->intr_moder_tx_interval;
}

unsigned int ena_com_get_nonadaptive_moderation_interval_rx(struct ena_com_dev *ena_dev)
{
	struct ena_intr_moder_entry *intr_moder_tbl = ena_dev->intr_moder_tbl;

	if (intr_moder_tbl)
		return intr_moder_tbl[ENA_INTR_MODER_LOWEST].intr_moder_interval;

	return 0;
}

void ena_com_init_intr_moderation_entry(struct ena_com_dev *ena_dev,
					enum ena_intr_moder_level level,
					struct ena_intr_moder_entry *entry)
{
	struct ena_intr_moder_entry *intr_moder_tbl = ena_dev->intr_moder_tbl;

	if (level >= ENA_INTR_MAX_NUM_OF_LEVELS)
		return;

	intr_moder_tbl[level].intr_moder_interval = entry->intr_moder_interval;
	if (ena_dev->intr_delay_resolution)
		intr_moder_tbl[level].intr_moder_interval /=
			ena_dev->intr_delay_resolution;
	intr_moder_tbl[level].pkts_per_interval = entry->pkts_per_interval;

	/* use hardcoded value until ethtool supports bytecount parameter */
	if (entry->bytes_per_interval != ENA_INTR_BYTE_COUNT_NOT_SUPPORTED)
		intr_moder_tbl[level].bytes_per_interval = entry->bytes_per_interval;
}

void ena_com_get_intr_moderation_entry(struct ena_com_dev *ena_dev,
				       enum ena_intr_moder_level level,
				       struct ena_intr_moder_entry *entry)
{
	struct ena_intr_moder_entry *intr_moder_tbl = ena_dev->intr_moder_tbl;

	if (level >= ENA_INTR_MAX_NUM_OF_LEVELS)
		return;

	entry->intr_moder_interval = intr_moder_tbl[level].intr_moder_interval;
	if (ena_dev->intr_delay_resolution)
		entry->intr_moder_interval *= ena_dev->intr_delay_resolution;
	entry->pkts_per_interval =
	intr_moder_tbl[level].pkts_per_interval;
	entry->bytes_per_interval = intr_moder_tbl[level].bytes_per_interval;
}

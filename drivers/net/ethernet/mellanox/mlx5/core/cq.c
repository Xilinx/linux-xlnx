/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <rdma/ib_verbs.h>
#include <linux/mlx5/cq.h>
#include "mlx5_core.h"

#define TASKLET_MAX_TIME 2
#define TASKLET_MAX_TIME_JIFFIES msecs_to_jiffies(TASKLET_MAX_TIME)

void mlx5_cq_tasklet_cb(unsigned long data)
{
	unsigned long flags;
	unsigned long end = jiffies + TASKLET_MAX_TIME_JIFFIES;
	struct mlx5_eq_tasklet *ctx = (struct mlx5_eq_tasklet *)data;
	struct mlx5_core_cq *mcq;
	struct mlx5_core_cq *temp;

	spin_lock_irqsave(&ctx->lock, flags);
	list_splice_tail_init(&ctx->list, &ctx->process_list);
	spin_unlock_irqrestore(&ctx->lock, flags);

	list_for_each_entry_safe(mcq, temp, &ctx->process_list,
				 tasklet_ctx.list) {
		list_del_init(&mcq->tasklet_ctx.list);
		mcq->tasklet_ctx.comp(mcq);
		if (atomic_dec_and_test(&mcq->refcount))
			complete(&mcq->free);
		if (time_after(jiffies, end))
			break;
	}

	if (!list_empty(&ctx->process_list))
		tasklet_schedule(&ctx->task);
}

static void mlx5_add_cq_to_tasklet(struct mlx5_core_cq *cq)
{
	unsigned long flags;
	struct mlx5_eq_tasklet *tasklet_ctx = cq->tasklet_ctx.priv;

	spin_lock_irqsave(&tasklet_ctx->lock, flags);
	/* When migrating CQs between EQs will be implemented, please note
	 * that you need to sync this point. It is possible that
	 * while migrating a CQ, completions on the old EQs could
	 * still arrive.
	 */
	if (list_empty_careful(&cq->tasklet_ctx.list)) {
		atomic_inc(&cq->refcount);
		list_add_tail(&cq->tasklet_ctx.list, &tasklet_ctx->list);
	}
	spin_unlock_irqrestore(&tasklet_ctx->lock, flags);
}

void mlx5_cq_completion(struct mlx5_core_dev *dev, u32 cqn)
{
	struct mlx5_core_cq *cq;
	struct mlx5_cq_table *table = &dev->priv.cq_table;

	spin_lock(&table->lock);
	cq = radix_tree_lookup(&table->tree, cqn);
	if (likely(cq))
		atomic_inc(&cq->refcount);
	spin_unlock(&table->lock);

	if (!cq) {
		mlx5_core_warn(dev, "Completion event for bogus CQ 0x%x\n", cqn);
		return;
	}

	++cq->arm_sn;

	cq->comp(cq);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
}

void mlx5_cq_event(struct mlx5_core_dev *dev, u32 cqn, int event_type)
{
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	struct mlx5_core_cq *cq;

	spin_lock(&table->lock);

	cq = radix_tree_lookup(&table->tree, cqn);
	if (cq)
		atomic_inc(&cq->refcount);

	spin_unlock(&table->lock);

	if (!cq) {
		mlx5_core_warn(dev, "Async event for bogus CQ 0x%x\n", cqn);
		return;
	}

	cq->event(cq, event_type);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
}

int mlx5_core_create_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			u32 *in, int inlen)
{
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	u32 din[MLX5_ST_SZ_DW(destroy_cq_in)];
	u32 dout[MLX5_ST_SZ_DW(destroy_cq_out)];
	int eqn = MLX5_GET(cqc, MLX5_ADDR_OF(create_cq_in, in, cq_context),
			   c_eqn);
	struct mlx5_eq *eq;
	int err;

	eq = mlx5_eqn2eq(dev, eqn);
	if (IS_ERR(eq))
		return PTR_ERR(eq);

	memset(out, 0, sizeof(out));
	MLX5_SET(create_cq_in, in, opcode, MLX5_CMD_OP_CREATE_CQ);
	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err)
		return err;

	cq->cqn = MLX5_GET(create_cq_out, out, cqn);
	cq->cons_index = 0;
	cq->arm_sn     = 0;
	atomic_set(&cq->refcount, 1);
	init_completion(&cq->free);
	if (!cq->comp)
		cq->comp = mlx5_add_cq_to_tasklet;
	/* assuming CQ will be deleted before the EQ */
	cq->tasklet_ctx.priv = &eq->tasklet_ctx;
	INIT_LIST_HEAD(&cq->tasklet_ctx.list);

	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, cq->cqn, cq);
	spin_unlock_irq(&table->lock);
	if (err)
		goto err_cmd;

	cq->pid = current->pid;
	err = mlx5_debug_cq_add(dev, cq);
	if (err)
		mlx5_core_dbg(dev, "failed adding CP 0x%x to debug file system\n",
			      cq->cqn);

	return 0;

err_cmd:
	memset(din, 0, sizeof(din));
	memset(dout, 0, sizeof(dout));
	MLX5_SET(destroy_cq_in, din, opcode, MLX5_CMD_OP_DESTROY_CQ);
	MLX5_SET(destroy_cq_in, din, cqn, cq->cqn);
	mlx5_cmd_exec(dev, din, sizeof(din), dout, sizeof(dout));
	return err;
}
EXPORT_SYMBOL(mlx5_core_create_cq);

int mlx5_core_destroy_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq)
{
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	u32 out[MLX5_ST_SZ_DW(destroy_cq_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_cq_in)] = {0};
	struct mlx5_core_cq *tmp;
	int err;

	spin_lock_irq(&table->lock);
	tmp = radix_tree_delete(&table->tree, cq->cqn);
	spin_unlock_irq(&table->lock);
	if (!tmp) {
		mlx5_core_warn(dev, "cq 0x%x not found in tree\n", cq->cqn);
		return -EINVAL;
	}
	if (tmp != cq) {
		mlx5_core_warn(dev, "corruption on srqn 0x%x\n", cq->cqn);
		return -EINVAL;
	}

	MLX5_SET(destroy_cq_in, in, opcode, MLX5_CMD_OP_DESTROY_CQ);
	MLX5_SET(destroy_cq_in, in, cqn, cq->cqn);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	synchronize_irq(cq->irqn);

	mlx5_debug_cq_remove(dev, cq);
	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
	wait_for_completion(&cq->free);

	return 0;
}
EXPORT_SYMBOL(mlx5_core_destroy_cq);

int mlx5_core_query_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
		       u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_cq_in)] = {0};

	MLX5_SET(query_cq_in, in, opcode, MLX5_CMD_OP_QUERY_CQ);
	MLX5_SET(query_cq_in, in, cqn, cq->cqn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}
EXPORT_SYMBOL(mlx5_core_query_cq);

int mlx5_core_modify_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_cq_out)] = {0};

	MLX5_SET(modify_cq_in, in, opcode, MLX5_CMD_OP_MODIFY_CQ);
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_modify_cq);

int mlx5_core_modify_cq_moderation(struct mlx5_core_dev *dev,
				   struct mlx5_core_cq *cq,
				   u16 cq_period,
				   u16 cq_max_count)
{
	u32 in[MLX5_ST_SZ_DW(modify_cq_in)] = {0};
	void *cqc;

	MLX5_SET(modify_cq_in, in, cqn, cq->cqn);
	cqc = MLX5_ADDR_OF(modify_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, cq_period, cq_period);
	MLX5_SET(cqc, cqc, cq_max_count, cq_max_count);
	MLX5_SET(modify_cq_in, in,
		 modify_field_select_resize_field_select.modify_field_select.modify_field_select,
		 MLX5_CQ_MODIFY_PERIOD | MLX5_CQ_MODIFY_COUNT);

	return mlx5_core_modify_cq(dev, cq, in, sizeof(in));
}
EXPORT_SYMBOL(mlx5_core_modify_cq_moderation);

int mlx5_init_cq_table(struct mlx5_core_dev *dev)
{
	struct mlx5_cq_table *table = &dev->priv.cq_table;
	int err;

	memset(table, 0, sizeof(*table));
	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
	err = mlx5_cq_debugfs_init(dev);

	return err;
}

void mlx5_cleanup_cq_table(struct mlx5_core_dev *dev)
{
	mlx5_cq_debugfs_cleanup(dev);
}

/*
 * Copyright (c) 2013-2016, Mellanox Technologies. All rights reserved.
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
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"

/* Finds an entry where we can register the given rate
 * If the rate already exists, return the entry where it is registered,
 * otherwise return the first available entry.
 * If the table is full, return NULL
 */
static struct mlx5_rl_entry *find_rl_entry(struct mlx5_rl_table *table,
					   u32 rate)
{
	struct mlx5_rl_entry *ret_entry = NULL;
	bool empty_found = false;
	int i;

	for (i = 0; i < table->max_size; i++) {
		if (table->rl_entry[i].rate == rate)
			return &table->rl_entry[i];
		if (!empty_found && !table->rl_entry[i].rate) {
			empty_found = true;
			ret_entry = &table->rl_entry[i];
		}
	}

	return ret_entry;
}

static int mlx5_set_rate_limit_cmd(struct mlx5_core_dev *dev,
				   u32 rate, u16 index)
{
	u32 in[MLX5_ST_SZ_DW(set_rate_limit_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(set_rate_limit_out)] = {0};

	MLX5_SET(set_rate_limit_in, in, opcode,
		 MLX5_CMD_OP_SET_RATE_LIMIT);
	MLX5_SET(set_rate_limit_in, in, rate_limit_index, index);
	MLX5_SET(set_rate_limit_in, in, rate_limit, rate);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

bool mlx5_rl_is_in_range(struct mlx5_core_dev *dev, u32 rate)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;

	return (rate <= table->max_rate && rate >= table->min_rate);
}
EXPORT_SYMBOL(mlx5_rl_is_in_range);

int mlx5_rl_add_rate(struct mlx5_core_dev *dev, u32 rate, u16 *index)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	struct mlx5_rl_entry *entry;
	int err = 0;

	mutex_lock(&table->rl_lock);

	if (!rate || !mlx5_rl_is_in_range(dev, rate)) {
		mlx5_core_err(dev, "Invalid rate: %u, should be %u to %u\n",
			      rate, table->min_rate, table->max_rate);
		err = -EINVAL;
		goto out;
	}

	entry = find_rl_entry(table, rate);
	if (!entry) {
		mlx5_core_err(dev, "Max number of %u rates reached\n",
			      table->max_size);
		err = -ENOSPC;
		goto out;
	}
	if (entry->refcount) {
		/* rate already configured */
		entry->refcount++;
	} else {
		/* new rate limit */
		err = mlx5_set_rate_limit_cmd(dev, rate, entry->index);
		if (err) {
			mlx5_core_err(dev, "Failed configuring rate: %u (%d)\n",
				      rate, err);
			goto out;
		}
		entry->rate = rate;
		entry->refcount = 1;
	}
	*index = entry->index;

out:
	mutex_unlock(&table->rl_lock);
	return err;
}
EXPORT_SYMBOL(mlx5_rl_add_rate);

void mlx5_rl_remove_rate(struct mlx5_core_dev *dev, u32 rate)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	struct mlx5_rl_entry *entry = NULL;

	/* 0 is a reserved value for unlimited rate */
	if (rate == 0)
		return;

	mutex_lock(&table->rl_lock);
	entry = find_rl_entry(table, rate);
	if (!entry || !entry->refcount) {
		mlx5_core_warn(dev, "Rate %u is not configured\n", rate);
		goto out;
	}

	entry->refcount--;
	if (!entry->refcount) {
		/* need to remove rate */
		mlx5_set_rate_limit_cmd(dev, 0, entry->index);
		entry->rate = 0;
	}

out:
	mutex_unlock(&table->rl_lock);
}
EXPORT_SYMBOL(mlx5_rl_remove_rate);

int mlx5_init_rl_table(struct mlx5_core_dev *dev)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	int i;

	mutex_init(&table->rl_lock);
	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, packet_pacing)) {
		table->max_size = 0;
		return 0;
	}

	/* First entry is reserved for unlimited rate */
	table->max_size = MLX5_CAP_QOS(dev, packet_pacing_rate_table_size) - 1;
	table->max_rate = MLX5_CAP_QOS(dev, packet_pacing_max_rate);
	table->min_rate = MLX5_CAP_QOS(dev, packet_pacing_min_rate);

	table->rl_entry = kcalloc(table->max_size, sizeof(struct mlx5_rl_entry),
				  GFP_KERNEL);
	if (!table->rl_entry)
		return -ENOMEM;

	/* The index represents the index in HW rate limit table
	 * Index 0 is reserved for unlimited rate
	 */
	for (i = 0; i < table->max_size; i++)
		table->rl_entry[i].index = i + 1;

	/* Index 0 is reserved */
	mlx5_core_info(dev, "Rate limit: %u rates are supported, range: %uMbps to %uMbps\n",
		       table->max_size,
		       table->min_rate >> 10,
		       table->max_rate >> 10);

	return 0;
}

void mlx5_cleanup_rl_table(struct mlx5_core_dev *dev)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	int i;

	/* Clear all configured rates */
	for (i = 0; i < table->max_size; i++)
		if (table->rl_entry[i].rate)
			mlx5_set_rate_limit_cmd(dev, 0,
						table->rl_entry[i].index);

	kfree(dev->priv.rl_table.rl_entry);
}

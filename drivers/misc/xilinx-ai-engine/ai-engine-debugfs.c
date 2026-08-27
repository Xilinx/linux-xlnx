// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver debugfs entries
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "ai-engine-internal.h"

static int aie_tile_debugfs_avail_rsc_show(struct seq_file *seq, void *data)
{
	struct aie_tile *atile = seq->private;
	struct aie_partition *apart = atile->apart;
	u32 num_rscs[AIE_RSCTYPE_MAX];
	enum aie_module_type mod;
	enum aie_rsc_type r;
	u8 ttype;
	int ret;

	ttype = apart->adev->ops->get_tile_type(apart->adev, &atile->loc);
	switch (ttype) {
	case AIE_TILE_TYPE_TILE:
		mod = AIE_CORE_MOD;
		break;
	case AIE_TILE_TYPE_MEMORY:
		mod = AIE_MEM_MOD;
		break;
	case AIE_TILE_TYPE_SHIMPL:
	case AIE_TILE_TYPE_SHIMNOC:
		mod = AIE_PL_MOD;
		break;
	default:
		return -EINVAL;
	}

	for (r = AIE_RSCTYPE_PERF; r < AIE_RSCTYPE_MAX; r++) {
		if (ttype != AIE_TILE_TYPE_TILE && r == AIE_RSCTYPE_PCEVENT)
			continue;
		ret = aie_part_rscmgr_rsc_get_avail(apart, atile->loc, mod, r,
						    &num_rscs[r]);
		if (ret < 0)
			return ret;
	}

	if (ttype == AIE_TILE_TYPE_TILE)
		seq_puts(seq, "Core mod\n");
	seq_printf(seq, "Performance counters: %u\n", num_rscs[AIE_RSCTYPE_PERF]);
	seq_printf(seq, "User events: %u\n", num_rscs[AIE_RSCTYPE_USEREVENT]);
	seq_printf(seq, "Trace control: %u\n", num_rscs[AIE_RSCTYPE_TRACECONTROL]);
	if (ttype == AIE_TILE_TYPE_TILE)
		seq_printf(seq, "PC events: %u\n", num_rscs[AIE_RSCTYPE_PCEVENT]);
	seq_printf(seq, "Streamswitch events: %u\n", num_rscs[AIE_RSCTYPE_SSSELECT]);
	seq_printf(seq, "Broadcast channels: %u\n", num_rscs[AIE_RSCTYPE_BROADCAST]);
	seq_printf(seq, "Combo events: %u\n", num_rscs[AIE_RSCTYPE_COMBOEVENT]);
	seq_printf(seq, "Group events: %u\n", num_rscs[AIE_RSCTYPE_GROUPEVENTS]);

	if (ttype == AIE_TILE_TYPE_TILE) {
		mod = AIE_MEM_MOD;
		for (r = AIE_RSCTYPE_PERF; r < AIE_RSCTYPE_MAX; r++) {
			if (r == AIE_RSCTYPE_PCEVENT || r == AIE_RSCTYPE_SSSELECT)
				continue;
			ret = aie_part_rscmgr_rsc_get_avail(apart, atile->loc, mod, r,
							    &num_rscs[r]);
			if (ret < 0)
				return ret;
		}
		seq_puts(seq, "Mem mod\n");
		seq_printf(seq, "Performance counters: %u\n", num_rscs[AIE_RSCTYPE_PERF]);
		seq_printf(seq, "User events: %u\n", num_rscs[AIE_RSCTYPE_USEREVENT]);
		seq_printf(seq, "Trace control: %u\n", num_rscs[AIE_RSCTYPE_TRACECONTROL]);
		seq_printf(seq, "Broadcast channels: %u\n", num_rscs[AIE_RSCTYPE_BROADCAST]);
		seq_printf(seq, "Combo events: %u\n", num_rscs[AIE_RSCTYPE_COMBOEVENT]);
		seq_printf(seq, "Group events: %u\n", num_rscs[AIE_RSCTYPE_GROUPEVENTS]);
	}

	return 0;
}

static int aie_tile_debugfs_avail_rsc_open(struct inode *inode, struct file *file)
{
	return single_open(file, aie_tile_debugfs_avail_rsc_show, inode->i_private);
}

static const struct file_operations aie_tile_debugfs_avail_rsc_fops = {
	.owner = THIS_MODULE,
	.open = aie_tile_debugfs_avail_rsc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t aie_tile_debugfs_poke_read(struct file *file, char __user *buf,
					  size_t count, loff_t *ppos)
{
	struct aie_tile *atile = file->private_data;
	char out[32];
	int len;

	len = scnprintf(out, sizeof(out), "0x%08x\n", atile->debug_data.reg_val);
	return simple_read_from_buffer(buf, count, ppos, out, len);
}

static ssize_t
aie_tile_debugfs_poke_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct aie_tile *atile = file->private_data;
	struct aie_location loc;
	u32 reg, val, offset;
	char kbuf[32], cmd;
	void __iomem *va;
	int ret;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	/* atile is created with absolute location, we need to use relative */
	loc.col = atile->loc.col - atile->apart->range.start.col;
	loc.row = atile->loc.row;

	/* read */
	if (sscanf(kbuf, "%c %x", &cmd, &reg) == 2 && cmd == 'r') {
		offset = aie_cal_regoff(atile->apart->adev, loc, reg);
		ret = aie_part_reg_validation(atile->apart, offset, sizeof(u32), 0);
		if (ret < 0)
			return ret;

		offset += aie_aperture_cal_regoff(atile->apart->aperture,
						  atile->apart->range.start, 0);
		va = atile->apart->aperture->base + offset;
		atile->debug_data.reg_val = readl(va);

		return count;
	}

	/* write */
	if (sscanf(kbuf, "%c %x %x", &cmd, &reg, &val) == 3 && cmd == 'w') {
		offset = aie_cal_regoff(atile->apart->adev, loc, reg);
		ret = aie_part_reg_validation(atile->apart, offset, sizeof(u32), 1);
		if (ret < 0)
			return ret;

		offset += aie_aperture_cal_regoff(atile->apart->aperture,
						  atile->apart->range.start, 0);
		va = atile->apart->aperture->base + offset;
		writel(val, va);

		return count;
	}

	return -EINVAL;
}

static const struct file_operations aie_tile_debugfs_poke_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= aie_tile_debugfs_poke_read,
	.write		= aie_tile_debugfs_poke_write,
	.llseek		= default_llseek,
};

/**
 * aie_tile_debugfs_create() - create debugfs directory for the given tile
 * @atile: AI engine tile
 */
void aie_tile_debugfs_create(struct aie_tile *atile)
{
	struct aie_partition *apart = atile->apart;
	char tile_name[32];

	snprintf(tile_name, sizeof(tile_name), "%u_%u",
		 atile->loc.col, atile->loc.row);
	atile->debugfs_dir = debugfs_create_dir(tile_name, apart->debugfs_dir);

	if (IS_ERR_OR_NULL(atile->debugfs_dir)) {
		atile->debugfs_dir = NULL;
		return;
	}

	/* add debugfs files for tile */
	debugfs_create_file("poke", 0600, atile->debugfs_dir, atile,
			    &aie_tile_debugfs_poke_fops);
	debugfs_create_file("available_resources", 0444, atile->debugfs_dir, atile,
			    &aie_tile_debugfs_avail_rsc_fops);
}

static ssize_t
aie_part_debugfs_destroy_part_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct aie_partition *apart = file->private_data;
	struct file *filep = NULL;

	if (!apart->is_part_debugfs)
		return -EPERM;

	mutex_lock(&apart->mlock);
	filep = apart->filep;
	if (filep)
		apart->filep = NULL;
	mutex_unlock(&apart->mlock);

	if (filep)
		fput(filep);

	return count;
}

static const struct file_operations aie_part_debugfs_destroy_part_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= aie_part_debugfs_destroy_part_write,
};

/**
 * aie_part_debugfs_create() - create debugfs directory for partition
 * @apart: AI engine partition
 */
void aie_part_debugfs_create(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	char part_name[64];

	snprintf(part_name, sizeof(part_name), "aiepart_%u_%u",
		 apart->range.start.col, apart->range.size.col);
	apart->debugfs_dir = debugfs_create_dir(part_name, adev->debugfs_dir);

	if (IS_ERR_OR_NULL(apart->debugfs_dir)) {
		apart->debugfs_dir = NULL;
		return;
	}

	debugfs_create_file("destroy_partition", 0200, apart->debugfs_dir, apart,
			    &aie_part_debugfs_destroy_part_fops);
}

/**
 * aie_part_debugfs_remove() - remove debugfs directory for partition
 * @apart: AI engine partition
 */
void aie_part_debugfs_remove(struct aie_partition *apart)
{
	if (!apart->debugfs_dir)
		return;

	debugfs_remove_recursive(apart->debugfs_dir);
	apart->debugfs_dir = NULL;
}

static ssize_t
aie_device_debugfs_create_part_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct aie_device *adev = file->private_data;
	struct aie_partition_req req = { 0 };
	struct aie_partition *apart;
	u32 start_col, num_col;
	char kbuf[32];

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (sscanf(kbuf, "%u %u", &start_col, &num_col) != 2)
		return -EINVAL;

	req.partition_id = aie_calc_part_id(start_col, num_col);
	apart = aie_partition_request_from_adev(adev, &req);
	if (IS_ERR(apart))
		return PTR_ERR(apart);
	apart->is_part_debugfs = true;

	return count;
}

static const struct file_operations aie_device_debugfs_create_part_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= aie_device_debugfs_create_part_write,
};

/**
 * xilinx_ai_engine_debugfs_init() - initialize debugfs directory for AIE device
 * @adev: AI engine device
 */
void xilinx_ai_engine_debugfs_init(struct aie_device *adev)
{
	adev->debugfs_dir = debugfs_create_dir("xilinx_ai_engine", NULL);

	if (IS_ERR_OR_NULL(adev->debugfs_dir)) {
		adev->debugfs_dir = NULL;
		return;
	}

	debugfs_create_file("create_partition", 0200, adev->debugfs_dir, adev,
			    &aie_device_debugfs_create_part_fops);
}

/**
 * xilinx_ai_engine_debugfs_remove() - remove debugfs directory for AIE device
 * @adev: AI engine device
 */
void xilinx_ai_engine_debugfs_remove(struct aie_device *adev)
{
	if (!adev->debugfs_dir)
		return;

	debugfs_remove_recursive(adev->debugfs_dir);
	adev->debugfs_dir = NULL;
}

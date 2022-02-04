// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

/**
 * aie_get_dma_s2mm_status() - reads the DMA stream to memory map status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: 32-bit register value.
 */
static u32 aie_get_dma_s2mm_status(struct aie_partition *apart,
				   struct aie_location *loc)
{
	u32 stsoff, regoff, ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		stsoff = apart->adev->shim_dma->s2mm_sts_regoff;
	else
		stsoff = apart->adev->tile_dma->s2mm_sts_regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_dma_mm2s_status() - reads the DMA memory map to stream status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: 32-bit register value.
 */
static u32 aie_get_dma_mm2s_status(struct aie_partition *apart,
				   struct aie_location *loc)
{
	u32 stsoff, regoff, ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		stsoff = apart->adev->shim_dma->mm2s_sts_regoff;
	else
		stsoff = apart->adev->tile_dma->mm2s_sts_regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_chan_status() - reads the DMA channel status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit status value.
 */
static u8 aie_get_chan_status(struct aie_partition *apart,
			      struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *sts, *stall;
	u32 mask, chan_shift, shift, value, ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE) {
		sts = &apart->adev->shim_dma->sts;
		stall = &apart->adev->shim_dma->stall;
	} else {
		sts = &apart->adev->tile_dma->sts;
		stall = &apart->adev->tile_dma->stall;
	}

	chan_shift = sts->regoff;
	mask = sts->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;
	value = (status & mask) >> shift;

	chan_shift = stall->regoff;
	mask = stall->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;
	value |= (status & mask) >> shift;
	return value;
}

/**
 * aie_get_queue_size() - reads the DMA queue size.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit value.
 */
static u8 aie_get_queue_size(struct aie_partition *apart,
			     struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *qsize;
	u32 mask, chan_shift, shift, ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		qsize = &apart->adev->shim_dma->qsize;
	else
		qsize = &apart->adev->tile_dma->qsize;

	chan_shift = qsize->regoff;
	mask = qsize->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;
	return (status & mask) >> shift;
}

/**
 * aie_get_queue_status() - reads the DMA queue status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit status value.
 */
static u8 aie_get_queue_status(struct aie_partition *apart,
			       struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *qsts;
	u32 mask, chan_shift, shift, ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		qsts = &apart->adev->shim_dma->qsts;
	else
		qsts = &apart->adev->tile_dma->qsts;

	chan_shift = qsts->regoff;
	mask = qsts->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;
	return (status & mask) >> shift;
}

/**
 * aie_get_current_bd() - reads the current buffer descriptor being processed
 *			  by DMA channel.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit buffer descriptor value.
 */
static u8 aie_get_current_bd(struct aie_partition *apart,
			     struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *curbd;
	u32 mask, chan_shift, shift, ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		curbd = &apart->adev->shim_dma->curbd;
	else
		curbd = &apart->adev->tile_dma->curbd;

	chan_shift = curbd->regoff;
	mask = curbd->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;
	return (status & mask) >> shift;
}

/**
 * aie_get_fifo_status() - reads the current value of DMA FIFO counters.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: concatenated value of counters for AIE tiles and 0 for shim tiles.
 */
static u32 aie_get_fifo_status(struct aie_partition *apart,
			       struct aie_location *loc)
{
	u32 fifo_off, regoff, ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		return 0U;

	fifo_off = apart->adev->tile_dma->fifo_cnt_regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, fifo_off);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_fifo_count() - returns the value of a DMA FIFO counter from its
 *			  concatenated register value.
 * @apart: AI engine partition.
 * @status: register value of DMA FIFO counter.
 * @counterid: counter ID.
 * @return: DMA FIFO count.
 */
static u32 aie_get_fifo_count(struct aie_partition *apart, u32 status,
			      u8 counterid)
{
	const struct aie_single_reg_field *fifo;

	fifo = &apart->adev->tile_dma->fifo_cnt;

	status >>= (fifo->regoff * counterid);
	return (status & fifo->mask);
}

/**
 * aie_sysfs_get_dma_status() - returns the status of DMA in string format with
 *				MM2S and S2MM type channel separated by a ','
 *				symbol. Channels with a given type are
 *				separated by a '|' symbol.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @buffer: location to return DMA status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_sysfs_get_dma_status(struct aie_partition *apart,
				 struct aie_location *loc, char *buffer,
				 ssize_t size)
{
	u32 i, ttype, num_s2mm_chan, num_mm2s_chan;
	ssize_t len = 0;
	unsigned long status;
	bool is_delimit_req = false;
	char **str = apart->adev->dma_status_str;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_SHIMPL)
		return len;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		len += scnprintf(buffer, max(0L, size - len),
				 "mm2s: clock_gated%ss2mm: clock_gated",
				 DELIMITER_LEVEL1);
		return len;
	}

	if (ttype != AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = apart->adev->shim_dma->num_mm2s_chan;
		num_s2mm_chan = apart->adev->shim_dma->num_s2mm_chan;
	} else {
		num_mm2s_chan = apart->adev->tile_dma->num_mm2s_chan;
		num_s2mm_chan = apart->adev->tile_dma->num_s2mm_chan;
	}

	/* MM2S */
	len += scnprintf(&buffer[len], max(0L, size - len), "mm2s: ");
	status = aie_get_dma_mm2s_status(apart, loc);
	for (i = 0; i < num_mm2s_chan; i++) {
		u32 value = aie_get_chan_status(apart, loc, status, i);

		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
		}
		len += scnprintf(&buffer[len], max(0L, size - len), str[value]);
		is_delimit_req = true;
	}

	/* S2MM */
	is_delimit_req = false;
	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);
	status = aie_get_dma_s2mm_status(apart, loc);
	for (i = 0; i < num_s2mm_chan; i++) {
		u32 value = aie_get_chan_status(apart, loc, status, i);

		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
		}
		len += scnprintf(&buffer[len], max(0L, size - len), str[value]);
		is_delimit_req = true;
	}
	return len;
}

/**
 * aie_tile_show_dma() - exports AI engine DMA channel status, queue size,
 *			 queue status, and current buffer descriptor ID being
 *			 processed by DMA channel to a tile level sysfs node.
 * @dev: AI engine tile device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_tile_show_dma(struct device *dev, struct device_attribute *attr,
			  char *buffer)
{
	struct aie_tile *atile = container_of(dev, struct aie_tile, dev);
	struct aie_partition *apart = atile->apart;
	u32 ttype, i, num_s2mm_chan, num_mm2s_chan, fifo, fifo0_len, fifo1_len;
	unsigned long status;
	bool is_delimit_req = false;
	ssize_t len = 0, size = PAGE_SIZE, l0 = 0, l1 = 0, l2 = 0, l3 = 0;
	char **qsts_str = apart->adev->queue_status_str;
	char ch_buf[AIE_SYSFS_CHAN_STS_SIZE],
	     qsz_mm2s_buf[AIE_SYSFS_QUEUE_SIZE_SIZE],
	     qsz_s2mm_buf[AIE_SYSFS_QUEUE_SIZE_SIZE],
	     qsts_mm2s_buf[AIE_SYSFS_QUEUE_STS_SIZE],
	     qsts_s2mm_buf[AIE_SYSFS_QUEUE_STS_SIZE],
	     bd_mm2s_buf[AIE_SYSFS_BD_SIZE],
	     bd_s2mm_buf[AIE_SYSFS_BD_SIZE],
	     fifo_len_buf[AIE_SYSFS_FIFO_LEN_SIZE];

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	if (!aie_part_check_clk_enable_loc(apart, &atile->loc)) {
		scnprintf(ch_buf, AIE_SYSFS_CHAN_STS_SIZE,
			  "mm2s: clock_gated%ss2mm: clock_gated",
			  DELIMITER_LEVEL1);
		scnprintf(qsz_mm2s_buf, AIE_SYSFS_QUEUE_SIZE_SIZE,
			  "clock_gated");
		scnprintf(qsts_mm2s_buf, AIE_SYSFS_QUEUE_STS_SIZE,
			  "clock_gated");
		scnprintf(bd_mm2s_buf, AIE_SYSFS_BD_SIZE, "clock_gated");
		scnprintf(qsz_s2mm_buf, AIE_SYSFS_QUEUE_SIZE_SIZE,
			  "clock_gated");
		scnprintf(qsts_s2mm_buf, AIE_SYSFS_QUEUE_STS_SIZE,
			  "clock_gated");
		scnprintf(bd_s2mm_buf, AIE_SYSFS_BD_SIZE, "clock_gated");
		scnprintf(fifo_len_buf, AIE_SYSFS_BD_SIZE, "clock_gated");
		goto print;
	}

	aie_sysfs_get_dma_status(apart, &atile->loc, ch_buf,
				 AIE_SYSFS_CHAN_STS_SIZE);

	ttype = apart->adev->ops->get_tile_type(apart->adev, &atile->loc);
	if (ttype != AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = apart->adev->shim_dma->num_mm2s_chan;
		num_s2mm_chan = apart->adev->shim_dma->num_s2mm_chan;
	} else {
		num_mm2s_chan = apart->adev->tile_dma->num_mm2s_chan;
		num_s2mm_chan = apart->adev->tile_dma->num_s2mm_chan;
	}

	/* MM2S */
	status = aie_get_dma_mm2s_status(apart, &atile->loc);
	for (i = 0; i < num_mm2s_chan; i++) {
		u8 qsize = aie_get_queue_size(apart, &atile->loc, status, i);
		u8 qsts = aie_get_queue_status(apart, &atile->loc, status, i);
		u8 curbd = aie_get_current_bd(apart, &atile->loc, status, i);

		if (is_delimit_req) {
			l0 += scnprintf(&qsz_mm2s_buf[l0],
					max(0L, AIE_SYSFS_QUEUE_SIZE_SIZE - l0),
					DELIMITER_LEVEL0);
			l1 += scnprintf(&qsts_mm2s_buf[l1],
					max(0L, AIE_SYSFS_QUEUE_STS_SIZE - l1),
					DELIMITER_LEVEL0);
			l2 += scnprintf(&bd_mm2s_buf[l2],
					max(0L, AIE_SYSFS_BD_SIZE - l2),
					DELIMITER_LEVEL0);
		}
		l0 += scnprintf(&qsz_mm2s_buf[l0],
				max(0L, AIE_SYSFS_QUEUE_SIZE_SIZE - l0), "%d",
				qsize);
		l1 += scnprintf(&qsts_mm2s_buf[l1],
				max(0L, AIE_SYSFS_QUEUE_STS_SIZE - l1),
				qsts_str[qsts]);
		l2 += scnprintf(&bd_mm2s_buf[l2],
				max(0L, AIE_SYSFS_BD_SIZE - l2), "%d", curbd);
		is_delimit_req = true;
	}

	/* S2MM */
	is_delimit_req = false;
	l0 = 0; l1 = 0; l2 = 0;
	status = aie_get_dma_s2mm_status(apart, &atile->loc);
	for (i = 0; i < num_s2mm_chan; i++) {
		u8 qsize = aie_get_queue_size(apart, &atile->loc, status, i);
		u8 qsts = aie_get_queue_status(apart, &atile->loc, status, i);
		u8 curbd = aie_get_current_bd(apart, &atile->loc, status, i);

		if (is_delimit_req) {
			l0 += scnprintf(&qsz_s2mm_buf[l0],
					max(0L, AIE_SYSFS_QUEUE_SIZE_SIZE - l0),
					DELIMITER_LEVEL0);
			l1 += scnprintf(&qsts_s2mm_buf[l1],
					max(0L, AIE_SYSFS_QUEUE_STS_SIZE - l1),
					DELIMITER_LEVEL0);
			l2 += scnprintf(&bd_s2mm_buf[l2],
					max(0L, AIE_SYSFS_BD_SIZE - l2),
					DELIMITER_LEVEL0);
		}
		l0 += scnprintf(&qsz_s2mm_buf[l0],
				max(0L, AIE_SYSFS_QUEUE_SIZE_SIZE - l0), "%d",
				qsize);
		l1 += scnprintf(&qsts_s2mm_buf[l1],
				max(0L, AIE_SYSFS_QUEUE_STS_SIZE - l1),
				qsts_str[qsts]);
		l2 += scnprintf(&bd_s2mm_buf[l2],
				max(0L, AIE_SYSFS_BD_SIZE - l2), "%d", curbd);
		is_delimit_req = true;
	}

	fifo = aie_get_fifo_status(apart,  &atile->loc);
	fifo0_len = aie_get_fifo_count(apart, fifo, 0);
	fifo1_len = aie_get_fifo_count(apart, fifo, 1);

	l3 += scnprintf(&fifo_len_buf[l3],
			max(0L, AIE_SYSFS_QUEUE_SIZE_SIZE - l3), "%d%s%d",
			fifo0_len, DELIMITER_LEVEL0, fifo1_len);

print:
	mutex_unlock(&apart->mlock);
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "channel_status: %s\n", ch_buf);
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "queue_size: mm2s: %s%ss2mm: %s\n", qsz_mm2s_buf,
			 DELIMITER_LEVEL1, qsz_s2mm_buf);
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "queue_status: mm2s: %s%ss2mm: %s\n", qsts_mm2s_buf,
			 DELIMITER_LEVEL1, qsts_s2mm_buf);
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "current_bd: mm2s: %s%ss2mm: %s\n", bd_mm2s_buf,
			 DELIMITER_LEVEL1, bd_s2mm_buf);
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "fifo_len: %s\n", fifo_len_buf);
	return len;
}

/**
 * aie_part_read_cb_dma() - exports status of all DMAs within a given
 *			    partition to partition level node.
 * @kobj: kobject used to create sysfs node.
 * @buffer: export buffer.
 * @size: length of export buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_read_cb_dma(struct kobject *kobj, char *buffer, ssize_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct aie_partition *apart = dev_to_aiepart(dev);
	struct aie_tile *atile = apart->atiles;
	ssize_t len = 0;
	u32 index;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++, atile++) {
		u32 ttype = apart->adev->ops->get_tile_type(apart->adev,
							    &atile->loc);

		if (ttype == AIE_TILE_TYPE_SHIMPL)
			continue;

		len += scnprintf(&buffer[len], max(0L, size - len), "%d_%d: ",
				 atile->loc.col, atile->loc.row);
		len += aie_sysfs_get_dma_status(apart, &atile->loc,
						&buffer[len], size - len);
		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

	mutex_unlock(&apart->mlock);
	return len;
}

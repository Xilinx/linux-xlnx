/*
 * XILINX VDMA Engine test client driver
 *
 * Copyright (C) 2010-2013 Xilinx, Inc. All rights reserved.
 *
 * Based on Atmel DMA Test Client
 *
 * Description:
 * This is a simple Xilinx VDMA test client for AXI VDMA driver.
 * This test assumes both the channels of VDMA are enabled in the
 * hardware design and configured in back-to-back connection. Test
 * starts by pumping the data onto one channel (MM2S) and then
 * compares the data that is received on the other channel (S2MM).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/amba/xilinx_dma.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/wait.h>

static unsigned int test_buf_size = 64;
module_param(test_buf_size, uint, S_IRUGO);
MODULE_PARM_DESC(test_buf_size, "Size of the memcpy test buffer");

static unsigned int iterations;
module_param(iterations, uint, S_IRUGO);
MODULE_PARM_DESC(iterations,
		"Iterations before stopping test (default: infinite)");

/*
 * Initialization patterns. All bytes in the source buffer has bit 7
 * set, all bytes in the destination buffer has bit 7 cleared.
 *
 * Bit 6 is set for all bytes which are to be copied by the DMA
 * engine. Bit 5 is set for all bytes which are to be overwritten by
 * the DMA engine.
 *
 * The remaining bits are the inverse of a counter which increments by
 * one for each byte address.
 */
#define PATTERN_SRC		0x80
#define PATTERN_DST		0x00
#define PATTERN_COPY		0x40
#define PATTERN_OVERWRITE	0x20
#define PATTERN_COUNT_MASK	0x1f

/* Maximum number of frame buffers */
#define MAX_NUM_FRAMES	32

/**
 * struct vdmatest_slave_thread - VDMA test thread
 * @node: Thread node
 * @task: Task structure pointer
 * @tx_chan: Tx channel pointer
 * @rx_chan: Rx Channel pointer
 * @srcs: Source buffer
 * @dsts: Destination buffer
 * @type: DMA transaction type
 */
struct xilinx_vdmatest_slave_thread {
	struct list_head node;
	struct task_struct *task;
	struct dma_chan *tx_chan;
	struct dma_chan *rx_chan;
	u8 **srcs;
	u8 **dsts;
	enum dma_transaction_type type;
};

/**
 * struct vdmatest_chan - VDMA Test channel
 * @node: Channel node
 * @chan: DMA channel pointer
 * @threads: List of VDMA test threads
 */
struct xilinx_vdmatest_chan {
	struct list_head node;
	struct dma_chan *chan;
	struct list_head threads;
};

/* Global variables */
static LIST_HEAD(xilinx_vdmatest_channels);
static unsigned int nr_channels;
static unsigned int frm_cnt;
static dma_addr_t dma_srcs[MAX_NUM_FRAMES];
static dma_addr_t dma_dsts[MAX_NUM_FRAMES];
static struct scatterlist tx_sg[MAX_NUM_FRAMES];
static struct scatterlist rx_sg[MAX_NUM_FRAMES];

static void xilinx_vdmatest_init_srcs(u8 **bufs, unsigned int start,
					unsigned int len)
{
	unsigned int i;
	u8 *buf;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = PATTERN_SRC | (~i & PATTERN_COUNT_MASK);
		for (; i < start + len; i++)
			buf[i] = PATTERN_SRC | PATTERN_COPY
				| (~i & PATTERN_COUNT_MASK);
		for (; i < test_buf_size; i++)
			buf[i] = PATTERN_SRC | (~i & PATTERN_COUNT_MASK);
		buf++;
	}
}

static void xilinx_vdmatest_init_dsts(u8 **bufs, unsigned int start,
					unsigned int len)
{
	unsigned int i;
	u8 *buf;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = PATTERN_DST | (~i & PATTERN_COUNT_MASK);
		for (; i < start + len; i++)
			buf[i] = PATTERN_DST | PATTERN_OVERWRITE
				| (~i & PATTERN_COUNT_MASK);
		for (; i < test_buf_size; i++)
			buf[i] = PATTERN_DST | (~i & PATTERN_COUNT_MASK);
	}
}

static void xilinx_vdmatest_mismatch(u8 actual, u8 pattern, unsigned int index,
		unsigned int counter, bool is_srcbuf)
{
	u8 diff = actual ^ pattern;
	u8 expected = pattern | (~counter & PATTERN_COUNT_MASK);
	const char *thread_name = current->comm;

	if (is_srcbuf)
		pr_warn(
		"%s: srcbuf[0x%x] overwritten! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else if ((pattern & PATTERN_COPY)
			&& (diff & (PATTERN_COPY | PATTERN_OVERWRITE)))
		pr_warn(
		"%s: dstbuf[0x%x] not copied! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else if (diff & PATTERN_SRC)
		pr_warn(
		"%s: dstbuf[0x%x] was copied! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else
		pr_warn(
		"%s: dstbuf[0x%x] mismatch! Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
}

static unsigned int xilinx_vdmatest_verify(u8 **bufs, unsigned int start,
		unsigned int end, unsigned int counter, u8 pattern,
		bool is_srcbuf)
{
	unsigned int i, error_count = 0;
	u8 actual, expected, *buf;
	unsigned int counter_orig = counter;

	for (; (buf = *bufs); bufs++) {
		counter = counter_orig;
		for (i = start; i < end; i++) {
			actual = buf[i];
			expected = pattern | (~counter & PATTERN_COUNT_MASK);
			if (actual != expected) {
				if (error_count < 32)
					xilinx_vdmatest_mismatch(actual,
							pattern, i,
							counter, is_srcbuf);
				error_count++;
			}
			counter++;
		}
	}

	if (error_count > 32)
		pr_warn("%s: %u errors suppressed\n",
			current->comm, error_count - 32);

	return error_count;
}

static void xilinx_vdmatest_slave_tx_callback(void *completion)
{
	pr_debug("Got tx callback\n");
	complete(completion);
}

static void xilinx_vdmatest_slave_rx_callback(void *completion)
{
	pr_debug("Got rx callback\n");
	complete(completion);
}

/*
 * Function for slave transfers
 * Each thread requires 2 channels, one for transmit, and one for receive
 */
static int xilinx_vdmatest_slave_func(void *data)
{
	struct xilinx_vdmatest_slave_thread *thread = data;
	struct dma_chan *tx_chan, *rx_chan;
	const char *thread_name;
	unsigned int len, error_count;
	unsigned int failed_tests = 0, total_tests = 0;
	dma_cookie_t tx_cookie, rx_cookie;
	enum dma_status status;
	enum dma_ctrl_flags flags;
	int ret = -ENOMEM, i;
	int hsize = 64, vsize = 32;
	struct xilinx_vdma_config config;

	thread_name = current->comm;

	/* Limit testing scope here */
	iterations = 1;
	test_buf_size = hsize * vsize;

	/* This barrier ensures 'thread' is initialized and
	 * we get valid DMA channels
	 */
	smp_rmb();
	tx_chan = thread->tx_chan;
	rx_chan = thread->rx_chan;

	thread->srcs = kcalloc(frm_cnt+1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->srcs)
		goto err_srcs;
	for (i = 0; i < frm_cnt; i++) {
		thread->srcs[i] = kmalloc(test_buf_size, GFP_KERNEL);
		if (!thread->srcs[i])
			goto err_srcbuf;
	}

	thread->dsts = kcalloc(frm_cnt+1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->dsts)
		goto err_dsts;
	for (i = 0; i < frm_cnt; i++) {
		thread->dsts[i] = kmalloc(test_buf_size, GFP_KERNEL);
		if (!thread->dsts[i])
			goto err_dstbuf;
	}

	set_user_nice(current, 10);

	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	while (!kthread_should_stop()
		&& !(iterations && total_tests >= iterations)) {
		struct dma_device *tx_dev = tx_chan->device;
		struct dma_device *rx_dev = rx_chan->device;
		struct dma_async_tx_descriptor *txd = NULL;
		struct dma_async_tx_descriptor *rxd = NULL;
		struct completion rx_cmp, tx_cmp;
		unsigned long rx_tmo =
				msecs_to_jiffies(30000); /* RX takes longer */
		unsigned long tx_tmo = msecs_to_jiffies(30000);
		u8 align = 0;

		total_tests++;

		/* honor larger alignment restrictions */
		align = tx_dev->copy_align;
		if (rx_dev->copy_align > align)
			align = rx_dev->copy_align;

		if (1 << align > test_buf_size) {
			pr_err("%u-byte buffer too small for %d-byte alignment\n",
			       test_buf_size, 1 << align);
			break;
		}

		len = test_buf_size;
		xilinx_vdmatest_init_srcs(thread->srcs, 0, len);
		xilinx_vdmatest_init_dsts(thread->dsts, 0, len);

		sg_init_table(tx_sg, frm_cnt);
		sg_init_table(rx_sg, frm_cnt);

		for (i = 0; i < frm_cnt; i++) {
			u8 *buf = thread->srcs[i];

			dma_srcs[i] = dma_map_single(tx_dev->dev, buf, len,
							DMA_MEM_TO_DEV);
			pr_debug("src buf %x dma %x\n", (unsigned int)buf,
				 (unsigned int)dma_srcs[i]);
			sg_dma_address(&tx_sg[i]) = dma_srcs[i];
			sg_dma_len(&tx_sg[i]) = len;
		}

		for (i = 0; i < frm_cnt; i++) {
			dma_dsts[i] = dma_map_single(rx_dev->dev,
							thread->dsts[i],
							test_buf_size,
							DMA_DEV_TO_MEM);
			pr_debug("dst %x dma %x\n",
				 (unsigned int)thread->dsts[i],
				 (unsigned int)dma_dsts[i]);
			sg_dma_address(&rx_sg[i]) = dma_dsts[i];
			sg_dma_len(&rx_sg[i]) = len;
		}

		/* Zero out configuration */
		memset(&config, 0, sizeof(struct xilinx_vdma_config));

		/* Set up hardware configuration information */
		config.vsize = vsize;
		config.hsize = hsize;
		config.stride = hsize;
		config.frm_cnt_en = 1;
		config.coalesc = frm_cnt * 10;
		config.park = 1;
		tx_dev->device_control(tx_chan, DMA_SLAVE_CONFIG,
					(unsigned long)&config);

		config.park = 0;
		rx_dev->device_control(rx_chan, DMA_SLAVE_CONFIG,
					(unsigned long)&config);

		rxd = rx_dev->device_prep_slave_sg(rx_chan, rx_sg, frm_cnt,
				DMA_DEV_TO_MEM, flags, NULL);

		txd = tx_dev->device_prep_slave_sg(tx_chan, tx_sg, frm_cnt,
				DMA_MEM_TO_DEV, flags, NULL);

		if (!rxd || !txd) {
			for (i = 0; i < frm_cnt; i++)
				dma_unmap_single(tx_dev->dev, dma_srcs[i], len,
						DMA_MEM_TO_DEV);
			for (i = 0; i < frm_cnt; i++)
				dma_unmap_single(rx_dev->dev, dma_dsts[i],
						test_buf_size,
						DMA_DEV_TO_MEM);
			pr_warn("%s: #%u: prep error with len=0x%x ",
					thread_name, total_tests - 1, len);
			msleep(100);
			failed_tests++;
			continue;
		}

		init_completion(&rx_cmp);
		rxd->callback = xilinx_vdmatest_slave_rx_callback;
		rxd->callback_param = &rx_cmp;
		rx_cookie = rxd->tx_submit(rxd);

		init_completion(&tx_cmp);
		txd->callback = xilinx_vdmatest_slave_tx_callback;
		txd->callback_param = &tx_cmp;
		tx_cookie = txd->tx_submit(txd);

		if (dma_submit_error(rx_cookie) ||
				dma_submit_error(tx_cookie)) {
			pr_warn("%s: #%u: submit error %d/%d with len=0x%x ",
					thread_name, total_tests - 1,
					rx_cookie, tx_cookie, len);
			msleep(100);
			failed_tests++;
			continue;
		}
		dma_async_issue_pending(tx_chan);
		dma_async_issue_pending(rx_chan);

		tx_tmo = wait_for_completion_timeout(&tx_cmp, tx_tmo);

		status = dma_async_is_tx_complete(tx_chan, tx_cookie,
							NULL, NULL);

		if (tx_tmo == 0) {
			pr_warn("%s: #%u: tx test timed out\n",
					thread_name, total_tests - 1);
			failed_tests++;
			continue;
		} else if (status != DMA_COMPLETE) {
			pr_warn(
			"%s: #%u: tx got completion callback, ",
				   thread_name, total_tests - 1);
			pr_warn("but status is \'%s\'\n",
				   status == DMA_ERROR ? "error" :
							"in progress");
			failed_tests++;
			continue;
		}

		rx_tmo = wait_for_completion_timeout(&rx_cmp, rx_tmo);
		status = dma_async_is_tx_complete(rx_chan, rx_cookie,
							NULL, NULL);

		if (rx_tmo == 0) {
			pr_warn("%s: #%u: rx test timed out\n",
					thread_name, total_tests - 1);
			failed_tests++;
			continue;
		} else if (status != DMA_COMPLETE) {
			pr_warn(
			"%s: #%u: rx got completion callback, ",
					thread_name, total_tests - 1);
			pr_warn("but status is \'%s\'\n",
					status == DMA_ERROR ? "error" :
							"in progress");
			failed_tests++;
			continue;
		}

		/* Unmap by myself */
		for (i = 0; i < frm_cnt; i++)
			dma_unmap_single(rx_dev->dev, dma_dsts[i],
					 test_buf_size, DMA_DEV_TO_MEM);

		error_count = 0;

		pr_debug("%s: verifying source buffer...\n", thread_name);
		error_count += xilinx_vdmatest_verify(thread->srcs, 0, 0,
				0, PATTERN_SRC, true);
		error_count += xilinx_vdmatest_verify(thread->srcs, 0,
				len, 0, PATTERN_SRC | PATTERN_COPY, true);
		error_count += xilinx_vdmatest_verify(thread->srcs, len,
				test_buf_size, len, PATTERN_SRC, true);

		pr_debug("%s: verifying dest buffer...\n",
				thread->task->comm);
		error_count += xilinx_vdmatest_verify(thread->dsts, 0, 0,
				0, PATTERN_DST, false);
		error_count += xilinx_vdmatest_verify(thread->dsts, 0,
				len, 0, PATTERN_SRC | PATTERN_COPY, false);
		error_count += xilinx_vdmatest_verify(thread->dsts, len,
				test_buf_size, len, PATTERN_DST, false);

		if (error_count) {
			pr_warn("%s: #%u: %u errors with len=0x%x\n",
				thread_name, total_tests - 1, error_count, len);
			failed_tests++;
		} else {
			pr_debug("%s: #%u: No errors with len=0x%x\n",
				thread_name, total_tests - 1, len);
		}
	}

	ret = 0;
	for (i = 0; thread->dsts[i]; i++)
		kfree(thread->dsts[i]);
err_dstbuf:
	kfree(thread->dsts);
err_dsts:
	for (i = 0; thread->srcs[i]; i++)
		kfree(thread->srcs[i]);
err_srcbuf:
	kfree(thread->srcs);
err_srcs:
	pr_notice("%s: terminating after %u tests, %u failures (status %d)\n",
			thread_name, total_tests, failed_tests, ret);

	if (iterations > 0)
		while (!kthread_should_stop()) {
			DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wait_vdmatest_exit);
			interruptible_sleep_on(&wait_vdmatest_exit);
		}

	return ret;
}

static void xilinx_vdmatest_cleanup_channel(struct xilinx_vdmatest_chan *dtc)
{
	struct xilinx_vdmatest_slave_thread *thread, *_thread;
	int ret;

	list_for_each_entry_safe(thread, _thread,
				&dtc->threads, node) {
		ret = kthread_stop(thread->task);
		pr_info("xilinx_vdmatest: thread %s exited with status %d\n",
				thread->task->comm, ret);
		list_del(&thread->node);
		kfree(thread);
	}
	kfree(dtc);
}

static int
xilinx_vdmatest_add_slave_threads(struct xilinx_vdmatest_chan *tx_dtc,
					struct xilinx_vdmatest_chan *rx_dtc)
{
	struct xilinx_vdmatest_slave_thread *thread;
	struct dma_chan *tx_chan = tx_dtc->chan;
	struct dma_chan *rx_chan = rx_dtc->chan;

	thread = kzalloc(sizeof(struct xilinx_vdmatest_slave_thread),
			GFP_KERNEL);
	if (!thread)
		pr_warn("xilinx_vdmatest: No memory for slave thread %s-%s\n",
			   dma_chan_name(tx_chan), dma_chan_name(rx_chan));

	thread->tx_chan = tx_chan;
	thread->rx_chan = rx_chan;
	thread->type = (enum dma_transaction_type)DMA_SLAVE;

	/* This barrier ensures the DMA channels in the 'thread'
	 * are initialized
	 */
	smp_wmb();
	thread->task = kthread_run(xilinx_vdmatest_slave_func, thread, "%s-%s",
		dma_chan_name(tx_chan), dma_chan_name(rx_chan));
	if (IS_ERR(thread->task)) {
		pr_warn("xilinx_vdmatest: Failed to run thread %s-%s\n",
				dma_chan_name(tx_chan), dma_chan_name(rx_chan));
		kfree(thread);
	}

	list_add_tail(&thread->node, &tx_dtc->threads);

	/* Added one thread with 2 channels */
	return 1;
}

static int xilinx_vdmatest_add_slave_channels(struct dma_chan *tx_chan,
					struct dma_chan *rx_chan)
{
	struct xilinx_vdmatest_chan *tx_dtc, *rx_dtc;
	unsigned int thread_count = 0;

	tx_dtc = kmalloc(sizeof(struct xilinx_vdmatest_chan), GFP_KERNEL);
	if (!tx_dtc)
		return -ENOMEM;

	rx_dtc = kmalloc(sizeof(struct xilinx_vdmatest_chan), GFP_KERNEL);
	if (!rx_dtc)
		return -ENOMEM;

	tx_dtc->chan = tx_chan;
	rx_dtc->chan = rx_chan;
	INIT_LIST_HEAD(&tx_dtc->threads);
	INIT_LIST_HEAD(&rx_dtc->threads);

	xilinx_vdmatest_add_slave_threads(tx_dtc, rx_dtc);
	thread_count += 1;

	pr_info("xilinx_vdmatest: Started %u threads using %s %s\n",
		thread_count, dma_chan_name(tx_chan), dma_chan_name(rx_chan));

	list_add_tail(&tx_dtc->node, &xilinx_vdmatest_channels);
	list_add_tail(&rx_dtc->node, &xilinx_vdmatest_channels);
	nr_channels += 2;

	return 0;
}

static int xilinx_vdmatest_probe(struct platform_device *pdev)
{
	struct dma_chan *chan, *rx_chan;
	int err;

	err = of_property_read_u32(pdev->dev.of_node,
					"xlnx,num-fstores", &frm_cnt);
	if (err < 0) {
		pr_err("xilinx_vdmatest: missing xlnx,num-fstores property\n");
		return err;
	}

	chan = dma_request_slave_channel(&pdev->dev, "vdma0");
	if (IS_ERR(chan)) {
		pr_err("xilinx_vdmatest: No Tx channel\n");
		return PTR_ERR(chan);
	}

	rx_chan = dma_request_slave_channel(&pdev->dev, "vdma1");
	if (IS_ERR(rx_chan)) {
		err = PTR_ERR(rx_chan);
		pr_err("xilinx_vdmatest: No Rx channel\n");
		goto free_tx;
	}

	err = xilinx_vdmatest_add_slave_channels(chan, rx_chan);
	if (err) {
		pr_err("xilinx_vdmatest: Unable to add channels\n");
		goto free_rx;
	}
	return 0;

free_rx:
	dma_release_channel(rx_chan);
free_tx:
	dma_release_channel(chan);

	return err;
}

static int xilinx_vdmatest_remove(struct platform_device *pdev)
{
	struct xilinx_vdmatest_chan *dtc, *_dtc;
	struct dma_chan *chan;

	list_for_each_entry_safe(dtc, _dtc, &xilinx_vdmatest_channels, node) {
		list_del(&dtc->node);
		chan = dtc->chan;
		xilinx_vdmatest_cleanup_channel(dtc);
		pr_info("xilinx_vdmatest: dropped channel %s\n",
			dma_chan_name(chan));
		dma_release_channel(chan);
	}
	return 0;
}

static const struct of_device_id xilinx_vdmatest_of_ids[] = {
	{ .compatible = "xlnx,axi-vdma-test",},
	{}
};

static struct platform_driver xilinx_vdmatest_driver = {
	.driver = {
		.name = "xilinx_vdmatest",
		.owner = THIS_MODULE,
		.of_match_table = xilinx_vdmatest_of_ids,
	},
	.probe = xilinx_vdmatest_probe,
	.remove = xilinx_vdmatest_remove,
};

module_platform_driver(xilinx_vdmatest_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx AXI VDMA Test Client");
MODULE_LICENSE("GPL v2");

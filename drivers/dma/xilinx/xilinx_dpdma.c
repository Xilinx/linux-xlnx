/*
 * Xilinx DPDMA Engine driver
 *
 *  Copyright (C) 2015 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "../dmaengine.h"

/* DPDMA registers */
#define XILINX_DPDMA_ERR_CTRL				0x0
#define XILINX_DPDMA_ISR				0x4
#define XILINX_DPDMA_IMR				0x8
#define XILINX_DPDMA_IEN				0xc
#define XILINX_DPDMA_IDS				0x10
#define XILINX_DPDMA_INTR_DESC_DONE_MASK		(0x3f << 0)
#define XILINX_DPDMA_INTR_DESC_DONE_SHIFT		0
#define XILINX_DPDMA_INTR_NO_OSTAND_MASK		(0x3f << 6)
#define XILINX_DPDMA_INTR_NO_OSTAND_SHIFT		6
#define XILINX_DPDMA_INTR_AXI_ERR_MASK			(0x3f << 12)
#define XILINX_DPDMA_INTR_AXI_ERR_SHIFT			12
#define XILINX_DPDMA_INTR_DESC_ERR_MASK			(0x3f << 18)
#define XILINX_DPDMA_INTR_DESC_ERR_SHIFT		16
#define XILINX_DPDMA_INTR_WR_CMD_FIFO_FULL		BIT(24)
#define XILINX_DPDMA_INTR_WR_DATA_FIFO_FULL		BIT(25)
#define XILINX_DPDMA_INTR_AXI_4K_CROSS			BIT(26)
#define XILINX_DPDMA_INTR_VSYNC				BIT(27)
#define XILINX_DPDMA_INTR_CHAN_ERR_MASK			0x41000
#define XILINX_DPDMA_INTR_CHAN_ERR			0xfff000
#define XILINX_DPDMA_INTR_GLOBAL_ERR			0x7000000
#define XILINX_DPDMA_INTR_ERR_ALL			0x7fff000
#define XILINX_DPDMA_INTR_CHAN_MASK			0x41041
#define XILINX_DPDMA_INTR_GLOBAL_MASK			0xf000000
#define XILINX_DPDMA_INTR_ALL				0xfffffff
#define XILINX_DPDMA_EISR				0x14
#define XILINX_DPDMA_EIMR				0x18
#define XILINX_DPDMA_EIEN				0x1c
#define XILINX_DPDMA_EIDS				0x20
#define XILINX_DPDMA_EINTR_INV_APB			BIT(0)
#define XILINX_DPDMA_EINTR_RD_AXI_ERR_MASK		(0x3f << 1)
#define XILINX_DPDMA_EINTR_RD_AXI_ERR_SHIFT		1
#define XILINX_DPDMA_EINTR_PRE_ERR_MASK			(0x3f << 7)
#define XILINX_DPDMA_EINTR_PRE_ERR_SHIFT		7
#define XILINX_DPDMA_EINTR_CRC_ERR_MASK			(0x3f << 13)
#define XILINX_DPDMA_EINTR_CRC_ERR_SHIFT		13
#define XILINX_DPDMA_EINTR_WR_AXI_ERR_MASK		(0x3f << 19)
#define XILINX_DPDMA_EINTR_WR_AXI_ERR_SHIFT		19
#define XILINX_DPDMA_EINTR_DESC_DONE_ERR_MASK		(0x3f << 25)
#define XILINX_DPDMA_EINTR_DESC_DONE_ERR_SHIFT		25
#define XILINX_DPDMA_EINTR_RD_CMD_FIFO_FULL		BIT(32)
#define XILINX_DPDMA_EINTR_CHAN_ERR_MASK		0x2082082
#define XILINX_DPDMA_EINTR_CHAN_ERR			0x7ffffffe
#define XILINX_DPDMA_EINTR_GLOBAL_ERR			0x80000001
#define XILINX_DPDMA_EINTR_ALL				0xffffffff
#define XILINX_DPDMA_CNTL				0x100
#define XILINX_DPDMA_GBL				0x104
#define XILINX_DPDMA_GBL_TRIG_SHIFT			0
#define XILINX_DPDMA_GBL_RETRIG_SHIFT			6
#define XILINX_DPDMA_ALC0_CNTL				0x108
#define XILINX_DPDMA_ALC0_STATUS			0x10c
#define XILINX_DPDMA_ALC0_MAX				0x110
#define XILINX_DPDMA_ALC0_MIN				0x114
#define XILINX_DPDMA_ALC0_ACC				0x118
#define XILINX_DPDMA_ALC0_ACC_TRAN			0x11c
#define XILINX_DPDMA_ALC1_CNTL				0x120
#define XILINX_DPDMA_ALC1_STATUS			0x124
#define XILINX_DPDMA_ALC1_MAX				0x128
#define XILINX_DPDMA_ALC1_MIN				0x12c
#define XILINX_DPDMA_ALC1_ACC				0x130
#define XILINX_DPDMA_ALC1_ACC_TRAN			0x134

/* Channel register */
#define XILINX_DPDMA_CH_BASE				0x200
#define XILINX_DPDMA_CH_OFFSET				0x100
#define XILINX_DPDMA_CH_DESC_START_ADDRE		0x0
#define XILINX_DPDMA_CH_DESC_START_ADDR			0x4
#define XILINX_DPDMA_CH_DESC_NEXT_ADDRE			0x8
#define XILINX_DPDMA_CH_DESC_NEXT_ADDR			0xc
#define XILINX_DPDMA_CH_PYLD_CUR_ADDRE			0x10
#define XILINX_DPDMA_CH_PYLD_CUR_ADDR			0x14
#define XILINX_DPDMA_CH_CNTL				0x18
#define XILINX_DPDMA_CH_CNTL_ENABLE			BIT(0)
#define XILINX_DPDMA_CH_CNTL_PAUSE			BIT(1)
#define XILINX_DPDMA_CH_CNTL_QOS_DSCR_WR_SHIFT		2
#define XILINX_DPDMA_CH_CNTL_QOS_DSCR_RD_SHIFT		6
#define XILINX_DPDMA_CH_CNTL_QOS_DATA_RD_SHIFT		10
#define XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS		11
#define XILINX_DPDMA_CH_STATUS				0x1c
#define XILINX_DPDMA_CH_STATUS_OTRAN_CNT_MASK		(0xf << 21)
#define XILINX_DPDMA_CH_STATUS_OTRAN_CNT_SHIFT		21
#define XILINX_DPDMA_CH_VDO				0x20
#define XILINX_DPDMA_CH_PYLD_SZ				0x24
#define XILINX_DPDMA_CH_DESC_ID				0x28

/* DPDMA descriptor fields */
#define XILINX_DPDMA_DESC_CONTROL_PREEMBLE		(0xa5)
#define XILINX_DPDMA_DESC_CONTROL_COMPLETE_INTR		BIT(8)
#define XILINX_DPDMA_DESC_CONTROL_DESC_UPDATE		BIT(9)
#define XILINX_DPDMA_DESC_CONTROL_IGNORE_DONE		BIT(10)
#define XILINX_DPDMA_DESC_CONTROL_FRAG_MODE		BIT(18)
#define XILINX_DPDMA_DESC_CONTROL_LAST			BIT(19)
#define XILINX_DPDMA_DESC_CONTROL_ENABLE_CRC		BIT(20)
#define XILINX_DPDMA_DESC_CONTROL_LAST_OF_FRAME		BIT(21)
#define XILINX_DPDMA_DESC_ID_MASK			(0xffff << 0)
#define XILINX_DPDMA_DESC_ID_SHIFT			(0)
#define XILINX_DPDMA_DESC_HSIZE_STRIDE_HSIZE_MASK	(0x3ffff << 0)
#define XILINX_DPDMA_DESC_HSIZE_STRIDE_HSIZE_SHIFT	(0)
#define XILINX_DPDMA_DESC_HSIZE_STRIDE_STRIDE_MASK	(0x3fff << 18)
#define XILINX_DPDMA_DESC_HSIZE_STRIDE_STRIDE_SHIFT	(18)
#define XILINX_DPDMA_DESC_ADDR_EXT_ADDR_MASK		(0xfff)
#define XILINX_DPDMA_DESC_ADDR_EXT_ADDR_SHIFT		(16)

#define XILINX_DPDMA_ALIGN_BYTES			256

#define XILINX_DPDMA_NUM_CHAN				6
#define XILINX_DPDMA_PAGE_MASK				((1 << 12) - 1)
#define XILINX_DPDMA_PAGE_SHIFT				12

/**
 * struct xilinx_dpdma_hw_desc - DPDMA hardware descriptor
 * @control: control configuration field
 * @desc_id: descriptor ID
 * @xfer_size: transfer size
 * @hsize_stride: horizontal size and stride
 * @timestamp_lsb: LSB of time stamp
 * @timestamp_msb: MSB of time stamp
 * @addr_ext: upper 16 bit of 48 bit address (next_desc and src_addr)
 * @next_desc: next descriptor 32 bit address
 * @src_addr: payload source address (lower 32 bit of 1st 4KB page)
 * @addr_ext_23: upper 16 bit of 48 bit address (src_addr2 and src_addr3)
 * @addr_ext_45: upper 16 bit of 48 bit address (src_addr4 and src_addr5)
 * @src_addr2: payload source address (lower 32 bit of 2nd 4KB page)
 * @src_addr3: payload source address (lower 32 bit of 3rd 4KB page)
 * @src_addr4: payload source address (lower 32 bit of 4th 4KB page)
 * @src_addr5: payload source address (lower 32 bit of 5th 4KB page)
 * @crc: descriptor CRC
 */
struct xilinx_dpdma_hw_desc {
	u32 control;
	u32 desc_id;
	u32 xfer_size;
	u32 hsize_stride;
	u32 timestamp_lsb;
	u32 timestamp_msb;
	u32 addr_ext;
	u32 next_desc;
	u32 src_addr;
	u32 addr_ext_23;
	u32 addr_ext_45;
	u32 src_addr2;
	u32 src_addr3;
	u32 src_addr4;
	u32 src_addr5;
	u32 crc;
} __aligned(XILINX_DPDMA_ALIGN_BYTES);

/**
 * struct xilinx_dpdma_sw_desc - DPDMA software descriptor
 * @hw: DPDMA hardware descriptor
 * @node: list node for software descriptors
 * @phys: physical address of the software descriptor
 */
struct xilinx_dpdma_sw_desc {
	struct xilinx_dpdma_hw_desc hw;
	struct list_head node;
	dma_addr_t phys;
};

/**
 * enum xilinx_dpdma_tx_desc_status - DPDMA tx descriptor status
 * @PREPARED: descriptor is prepared for transaction
 * @ACTIVE: transaction is (being) done successfully
 * @ERRORED: descriptor generates some errors
 */
enum xilinx_dpdma_tx_desc_status {
	PREPARED,
	ACTIVE,
	ERRORED
};

/**
 * struct xilinx_dpdma_tx_desc - DPDMA transaction descriptor
 * @async_tx: DMA async transaction descriptor
 * @descriptors: list of software descriptors
 * @node: list node for transaction descriptors
 * @status: tx descriptor status
 * @done_cnt: number of complete notification to deliver
 */
struct xilinx_dpdma_tx_desc {
	struct dma_async_tx_descriptor async_tx;
	struct list_head descriptors;
	struct list_head node;
	enum xilinx_dpdma_tx_desc_status status;
	unsigned int done_cnt;
};

/**
 * enum xilinx_dpdma_chan_id - DPDMA channel ID
 * @VIDEO0: video 1st channel
 * @VIDEO1: video 2nd channel for multi plane yuv formats
 * @VIDEO2: video 3rd channel for multi plane yuv formats
 * @GRAPHICS: graphics channel
 * @AUDIO0: 1st audio channel
 * @AUDIO1: 2nd audio channel
 */
enum xilinx_dpdma_chan_id {
	VIDEO0,
	VIDEO1,
	VIDEO2,
	GRAPHICS,
	AUDIO0,
	AUDIO1
};

/**
 * enum xilinx_dpdma_chan_status - DPDMA channel status
 * @IDLE: idle state
 * @STREAMING: actively streaming state
 */
enum xilinx_dpdma_chan_status {
	IDLE,
	STREAMING
};

/*
 * DPDMA descriptor placement
 * --------------------------
 * DPDMA descritpor life time is described with following placements:
 *
 * allocated_desc -> submitted_desc -> pending_desc -> active_desc -> done_list
 *
 * Transition is triggered as following:
 *
 * -> allocated_desc : a descriptor allocation
 * allocated_desc -> submitted_desc: a descriptorsubmission
 * submitted_desc -> pending_desc: request to issue pending a descriptor
 * pending_desc -> active_desc: VSYNC intr when a desc is scheduled to DPDMA
 * active_desc -> done_list: VSYNC intr when DPDMA switches to a new desc
 */

/**
 * struct xilinx_dpdma_chan - DPDMA channel
 * @common: generic dma channel structure
 * @reg: register base address
 * @id: channel ID
 * @wait_to_stop: queue to wait for outstanding transacitons before stopping
 * @status: channel status
 * @first_frame: flag for the first frame of stream
 * @video_group: flag if multi-channel operation is needed for video channels
 * @lock: lock to access struct xilinx_dpdma_chan
 * @desc_pool: descriptor allocation pool
 * @done_task: done IRQ bottom half handler
 * @err_task: error IRQ bottom half handler
 * @allocated_desc: allocated descriptor
 * @submitted_desc: submitted descriptor
 * @pending_desc: pending descriptor to be scheduled in next period
 * @active_desc: descriptor that the DPDMA channel is active on
 * @done_list: done descriptor list
 * @xdev: DPDMA device
 */
struct xilinx_dpdma_chan {
	struct dma_chan common;
	void __iomem *reg;
	enum xilinx_dpdma_chan_id id;

	wait_queue_head_t wait_to_stop;
	enum xilinx_dpdma_chan_status status;
	bool first_frame;
	bool video_group;

	spinlock_t lock;
	struct dma_pool *desc_pool;
	struct tasklet_struct done_task;
	struct tasklet_struct err_task;

	struct xilinx_dpdma_tx_desc *allocated_desc;
	struct xilinx_dpdma_tx_desc *submitted_desc;
	struct xilinx_dpdma_tx_desc *pending_desc;
	struct xilinx_dpdma_tx_desc *active_desc;
	struct list_head done_list;

	struct xilinx_dpdma_device *xdev;
};

/**
 * struct xilinx_dpdma_device - DPDMA device
 * @common: generic dma device structure
 * @reg: register base address
 * @dev: generic device structure
 * @axi_clk: axi clock
 * @chan: DPDMA channels
 * @ext_addr: flag for 64 bit system (48 bit addressing)
 * @desc_addr: descriptor addressing callback (32 bit vs 64 bit)
 */
struct xilinx_dpdma_device {
	struct dma_device common;
	void __iomem *reg;
	struct device *dev;

	struct clk *axi_clk;
	struct xilinx_dpdma_chan *chan[XILINX_DPDMA_NUM_CHAN];

	bool ext_addr;
	void (*desc_addr)(struct xilinx_dpdma_sw_desc *sw_desc,
			  struct xilinx_dpdma_sw_desc *prev,
			  dma_addr_t dma_addr[], unsigned int num_src_addr);
};

#ifdef CONFIG_XILINX_DPDMA_DEBUG_FS
#define XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE	32
#define XILINX_DPDMA_DEBUGFS_UINT16_MAX_STR	"65535"
#define IN_RANGE(x, min, max) ((x) >= (min) && (x) <= (max))

/* Match xilinx_dpdma_testcases vs dpdma_debugfs_reqs[] entry */
enum xilinx_dpdma_testcases {
	DPDMA_TC_INTR_DONE,
	DPDMA_TC_NONE
};

struct xilinx_dpdma_debugfs {
	enum xilinx_dpdma_testcases testcase;
	u16 xilinx_dpdma_intr_done_count;
	enum xilinx_dpdma_chan_id chan_id;
};

static struct xilinx_dpdma_debugfs dpdma_debugfs;
struct xilinx_dpdma_debugfs_request {
	const char *req;
	enum xilinx_dpdma_testcases tc;
	ssize_t (*read_handler)(char **kern_buff);
	ssize_t (*write_handler)(char **cmd);
};

static void xilinx_dpdma_debugfs_intr_done_count_incr(int chan_id)
{
	if (chan_id == dpdma_debugfs.chan_id)
		dpdma_debugfs.xilinx_dpdma_intr_done_count++;
}

static s64 xilinx_dpdma_debugfs_argument_value(char *arg)
{
	s64 value;

	if (!arg)
		return -1;

	if (!kstrtos64(arg, 0, &value))
		return value;

	return -1;
}

static ssize_t
xilinx_dpdma_debugfs_desc_done_intr_write(char **dpdma_test_arg)
{
	char *arg;
	char *arg_chan_id;
	s64 id;

	arg = strsep(dpdma_test_arg, " ");
	if (strncasecmp(arg, "start", 5) != 0)
		return -EINVAL;

	arg_chan_id = strsep(dpdma_test_arg, " ");
	id = xilinx_dpdma_debugfs_argument_value(arg_chan_id);

	if (id < 0 || !IN_RANGE(id, VIDEO0, AUDIO1))
		return -EINVAL;

	dpdma_debugfs.testcase = DPDMA_TC_INTR_DONE;
	dpdma_debugfs.xilinx_dpdma_intr_done_count = 0;
	dpdma_debugfs.chan_id = id;

	return 0;
}

static ssize_t xilinx_dpdma_debugfs_desc_done_intr_read(char **kern_buff)
{
	size_t out_str_len;

	dpdma_debugfs.testcase = DPDMA_TC_NONE;

	out_str_len = strlen(XILINX_DPDMA_DEBUGFS_UINT16_MAX_STR);
	out_str_len = min_t(size_t, XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE,
			    out_str_len);
	snprintf(*kern_buff, out_str_len, "%d",
		 dpdma_debugfs.xilinx_dpdma_intr_done_count);

	return 0;
}

/* Match xilinx_dpdma_testcases vs dpdma_debugfs_reqs[] entry */
struct xilinx_dpdma_debugfs_request dpdma_debugfs_reqs[] = {
	{"DESCRIPTOR_DONE_INTR", DPDMA_TC_INTR_DONE,
			xilinx_dpdma_debugfs_desc_done_intr_read,
			xilinx_dpdma_debugfs_desc_done_intr_write},
};

static ssize_t xilinx_dpdma_debugfs_write(struct file *f, const char __user
					       *buf, size_t size, loff_t *pos)
{
	char *kern_buff;
	char *dpdma_test_req;
	int ret;
	int i;

	if (*pos != 0 || size <= 0)
		return -EINVAL;

	/* Supporting single instance of test as of now*/
	if (dpdma_debugfs.testcase != DPDMA_TC_NONE)
		return -EBUSY;

	kern_buff = kzalloc(size, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	ret = strncpy_from_user(kern_buff, buf, size);
	if (ret < 0) {
		kfree(kern_buff);
		return ret;
	}

	/* Read the testcase name from an user request */
	dpdma_test_req = strsep(&kern_buff, " ");

	for (i = 0; i < ARRAY_SIZE(dpdma_debugfs_reqs); i++) {
		if (!strcasecmp(dpdma_test_req, dpdma_debugfs_reqs[i].req)) {
			if (!dpdma_debugfs_reqs[i].write_handler(&kern_buff)) {
				kfree(kern_buff);
				return size;
			}
			break;
		}
	}
	kfree(kern_buff);
	return -EINVAL;
}

static ssize_t xilinx_dpdma_debugfs_read(struct file *f, char __user *buf,
					 size_t size, loff_t *pos)
{
	char *kern_buff = NULL;
	size_t kern_buff_len, out_str_len;
	int ret;

	if (size <= 0)
		return -EINVAL;

	if (*pos != 0)
		return 0;

	kern_buff = kzalloc(XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE, GFP_KERNEL);
	if (!kern_buff) {
		dpdma_debugfs.testcase = DPDMA_TC_NONE;
		return -ENOMEM;
	}

	if (dpdma_debugfs.testcase == DPDMA_TC_NONE) {
		out_str_len = strlen("No testcase executed");
		out_str_len = min_t(size_t, XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE,
				    out_str_len);
		snprintf(kern_buff, out_str_len, "%s", "No testcase executed");
	} else {
		ret = dpdma_debugfs_reqs[dpdma_debugfs.testcase].read_handler(
				&kern_buff);
		if (ret) {
			kfree(kern_buff);
			return ret;
		}
	}

	kern_buff_len = strlen(kern_buff);
	size = min(size, kern_buff_len);

	ret = copy_to_user(buf, kern_buff, size);

	kfree(kern_buff);
	if (ret)
		return ret;

	*pos = size + 1;
	return size;
}

static const struct file_operations fops_xilinx_dpdma_dbgfs = {
	.owner = THIS_MODULE,
	.read = xilinx_dpdma_debugfs_read,
	.write = xilinx_dpdma_debugfs_write,
};

static int xilinx_dpdma_debugfs_init(struct device *dev)
{
	int err;
	struct dentry *xilinx_dpdma_debugfs_dir, *xilinx_dpdma_debugfs_file;

	dpdma_debugfs.testcase = DPDMA_TC_NONE;

	xilinx_dpdma_debugfs_dir = debugfs_create_dir("dpdma", NULL);
	if (!xilinx_dpdma_debugfs_dir) {
		dev_err(dev, "debugfs_create_dir failed\n");
		return -ENODEV;
	}

	xilinx_dpdma_debugfs_file =
		debugfs_create_file("testcase", 0444,
				    xilinx_dpdma_debugfs_dir, NULL,
				    &fops_xilinx_dpdma_dbgfs);
	if (!xilinx_dpdma_debugfs_file) {
		dev_err(dev, "debugfs_create_file testcase failed\n");
		err = -ENODEV;
		goto err_dbgfs;
	}
	return 0;

err_dbgfs:
	debugfs_remove_recursive(xilinx_dpdma_debugfs_dir);
	xilinx_dpdma_debugfs_dir = NULL;
	return err;
}

#else
static int xilinx_dpdma_debugfs_init(struct device *dev)
{
	return 0;
}

static void xilinx_dpdma_debugfs_intr_done_count_incr(int chan_id)
{
}
#endif /* CONFIG_XILINX_DPDMA_DEBUG_FS */

#define to_dpdma_tx_desc(tx) \
	container_of(tx, struct xilinx_dpdma_tx_desc, async_tx)

#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_dpdma_chan, common)

/* IO operations */

static inline u32 dpdma_read(void __iomem *base, u32 offset)
{
	return ioread32(base + offset);
}

static inline void dpdma_write(void __iomem *base, u32 offset, u32 val)
{
	iowrite32(val, base + offset);
}

static inline void dpdma_clr(void __iomem *base, u32 offset, u32 clr)
{
	dpdma_write(base, offset, dpdma_read(base, offset) & ~clr);
}

static inline void dpdma_set(void __iomem *base, u32 offset, u32 set)
{
	dpdma_write(base, offset, dpdma_read(base, offset) | set);
}

/* Xilinx DPDMA descriptor operations */

/**
 * xilinx_dpdma_sw_desc_next_32 - Set 32 bit address of a next sw descriptor
 * @sw_desc: current software descriptor
 * @next: next descriptor
 *
 * Update the current sw descriptor @sw_desc with 32 bit address of the next
 * descriptor @next.
 */
static inline void
xilinx_dpdma_sw_desc_next_32(struct xilinx_dpdma_sw_desc *sw_desc,
			     struct xilinx_dpdma_sw_desc *next)
{
	sw_desc->hw.next_desc = next->phys;
}

/**
 * xilinx_dpdma_sw_desc_addr_32 - Update the sw descriptor with 32 bit address
 * @sw_desc: software descriptor
 * @prev: previous descriptor
 * @dma_addr: array of dma addresses
 * @num_src_addr: number of addresses in @dma_addr
 *
 * Update the descriptor @sw_desc with 32 bit address.
 */
static void xilinx_dpdma_sw_desc_addr_32(struct xilinx_dpdma_sw_desc *sw_desc,
					 struct xilinx_dpdma_sw_desc *prev,
					 dma_addr_t dma_addr[],
					 unsigned int num_src_addr)
{
	struct xilinx_dpdma_hw_desc *hw_desc = &sw_desc->hw;
	unsigned int i;

	hw_desc->src_addr = dma_addr[0];

	if (prev)
		xilinx_dpdma_sw_desc_next_32(prev, sw_desc);

	for (i = 1; i < num_src_addr; i++) {
		u32 *addr = &hw_desc->src_addr2;
		u32 frag_addr;

		frag_addr = dma_addr[i];
		addr[i - 1] = frag_addr;
	}
}

/**
 * xilinx_dpdma_sw_desc_next_64 - Set 64 bit address of a next sw descriptor
 * @sw_desc: current software descriptor
 * @next: next descriptor
 *
 * Update the current sw descriptor @sw_desc with 64 bit address of the next
 * descriptor @next.
 */
static inline void
xilinx_dpdma_sw_desc_next_64(struct xilinx_dpdma_sw_desc *sw_desc,
			     struct xilinx_dpdma_sw_desc *next)
{
	sw_desc->hw.next_desc = (u32)next->phys;
	sw_desc->hw.addr_ext |= ((u64)next->phys >> 32) &
				XILINX_DPDMA_DESC_ADDR_EXT_ADDR_MASK;
}

/**
 * xilinx_dpdma_sw_desc_addr_64 - Update the sw descriptor with 64 bit address
 * @sw_desc: software descriptor
 * @prev: previous descriptor
 * @dma_addr: array of dma addresses
 * @num_src_addr: number of addresses in @dma_addr
 *
 * Update the descriptor @sw_desc with 64 bit address.
 */
static void xilinx_dpdma_sw_desc_addr_64(struct xilinx_dpdma_sw_desc *sw_desc,
					 struct xilinx_dpdma_sw_desc *prev,
					 dma_addr_t dma_addr[],
					 unsigned int num_src_addr)
{
	struct xilinx_dpdma_hw_desc *hw_desc = &sw_desc->hw;
	unsigned int i;

	hw_desc->src_addr = (u32)dma_addr[0];
	hw_desc->addr_ext |=
		((u64)dma_addr[0] >> 32) & XILINX_DPDMA_DESC_ADDR_EXT_ADDR_MASK;

	if (prev)
		xilinx_dpdma_sw_desc_next_64(prev, sw_desc);

	for (i = 1; i < num_src_addr; i++) {
		u32 *addr = &hw_desc->src_addr2;
		u32 *addr_ext = &hw_desc->addr_ext_23;
		u64 frag_addr;

		frag_addr = dma_addr[i];
		addr[i] = (u32)frag_addr;

		frag_addr >>= 32;
		frag_addr &= XILINX_DPDMA_DESC_ADDR_EXT_ADDR_MASK;
		frag_addr <<= XILINX_DPDMA_DESC_ADDR_EXT_ADDR_SHIFT * (i % 2);
		addr_ext[i / 2] = frag_addr;
	}
}

/* Xilinx DPDMA channel descriptor operations */

/**
 * xilinx_dpdma_chan_alloc_sw_desc - Allocate a software descriptor
 * @chan: DPDMA channel
 *
 * Allocate a software descriptor from the channel's descriptor pool.
 *
 * Return: a software descriptor or NULL.
 */
static struct xilinx_dpdma_sw_desc *
xilinx_dpdma_chan_alloc_sw_desc(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_sw_desc *sw_desc;
	dma_addr_t phys;

	sw_desc = dma_pool_alloc(chan->desc_pool, GFP_ATOMIC, &phys);
	if (!sw_desc)
		return NULL;

	memset(sw_desc, 0, sizeof(*sw_desc));
	sw_desc->phys = phys;

	return sw_desc;
}

/**
 * xilinx_dpdma_chan_free_sw_desc - Free a software descriptor
 * @chan: DPDMA channel
 * @sw_desc: software descriptor to free
 *
 * Free a software descriptor from the channel's descriptor pool.
 */
static void
xilinx_dpdma_chan_free_sw_desc(struct xilinx_dpdma_chan *chan,
			       struct xilinx_dpdma_sw_desc *sw_desc)
{
	dma_pool_free(chan->desc_pool, sw_desc, sw_desc->phys);
}

/**
 * xilinx_dpdma_chan_dump_tx_desc - Dump a tx descriptor
 * @chan: DPDMA channel
 * @tx_desc: tx descriptor to dump
 *
 * Dump contents of a tx descriptor
 */
static void xilinx_dpdma_chan_dump_tx_desc(struct xilinx_dpdma_chan *chan,
					   struct xilinx_dpdma_tx_desc *tx_desc)
{
	struct xilinx_dpdma_sw_desc *sw_desc;
	struct device *dev = chan->xdev->dev;
	unsigned int i = 0;

	dev_dbg(dev, "------- TX descriptor dump start -------\n");
	dev_dbg(dev, "------- channel ID = %d -------\n", chan->id);

	list_for_each_entry(sw_desc, &tx_desc->descriptors, node) {
		struct xilinx_dpdma_hw_desc *hw_desc = &sw_desc->hw;

		dev_dbg(dev, "------- HW descriptor %d -------\n", i++);
		dev_dbg(dev, "descriptor phys: %pad\n", &sw_desc->phys);
		dev_dbg(dev, "control: 0x%08x\n", hw_desc->control);
		dev_dbg(dev, "desc_id: 0x%08x\n", hw_desc->desc_id);
		dev_dbg(dev, "xfer_size: 0x%08x\n", hw_desc->xfer_size);
		dev_dbg(dev, "hsize_stride: 0x%08x\n", hw_desc->hsize_stride);
		dev_dbg(dev, "timestamp_lsb: 0x%08x\n", hw_desc->timestamp_lsb);
		dev_dbg(dev, "timestamp_msb: 0x%08x\n", hw_desc->timestamp_msb);
		dev_dbg(dev, "addr_ext: 0x%08x\n", hw_desc->addr_ext);
		dev_dbg(dev, "next_desc: 0x%08x\n", hw_desc->next_desc);
		dev_dbg(dev, "src_addr: 0x%08x\n", hw_desc->src_addr);
		dev_dbg(dev, "addr_ext_23: 0x%08x\n", hw_desc->addr_ext_23);
		dev_dbg(dev, "addr_ext_45: 0x%08x\n", hw_desc->addr_ext_45);
		dev_dbg(dev, "src_addr2: 0x%08x\n", hw_desc->src_addr2);
		dev_dbg(dev, "src_addr3: 0x%08x\n", hw_desc->src_addr3);
		dev_dbg(dev, "src_addr4: 0x%08x\n", hw_desc->src_addr4);
		dev_dbg(dev, "src_addr5: 0x%08x\n", hw_desc->src_addr5);
		dev_dbg(dev, "crc: 0x%08x\n", hw_desc->crc);
	}

	dev_dbg(dev, "------- TX descriptor dump end -------\n");
}

/**
 * xilinx_dpdma_chan_alloc_tx_desc - Allocate a transaction descriptor
 * @chan: DPDMA channel
 *
 * Allocate a tx descriptor.
 *
 * Return: a tx descriptor or NULL.
 */
static struct xilinx_dpdma_tx_desc *
xilinx_dpdma_chan_alloc_tx_desc(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_tx_desc *tx_desc;

	tx_desc = kzalloc(sizeof(*tx_desc), GFP_KERNEL);
	if (!tx_desc)
		return NULL;

	INIT_LIST_HEAD(&tx_desc->descriptors);
	tx_desc->status = PREPARED;

	return tx_desc;
}

/**
 * xilinx_dpdma_chan_free_tx_desc - Free a transaction descriptor
 * @chan: DPDMA channel
 * @tx_desc: tx descriptor
 *
 * Free the tx descriptor @tx_desc including its software descriptors.
 */
static void
xilinx_dpdma_chan_free_tx_desc(struct xilinx_dpdma_chan *chan,
			       struct xilinx_dpdma_tx_desc *tx_desc)
{
	struct xilinx_dpdma_sw_desc *sw_desc, *next;

	if (!tx_desc)
		return;

	list_for_each_entry_safe(sw_desc, next, &tx_desc->descriptors, node) {
		list_del(&sw_desc->node);
		xilinx_dpdma_chan_free_sw_desc(chan, sw_desc);
	}

	kfree(tx_desc);
}

/**
 * xilinx_dpdma_chan_submit_tx_desc - Submit a transaction descriptor
 * @chan: DPDMA channel
 * @tx_desc: tx descriptor
 *
 * Submit the tx descriptor @tx_desc to the channel @chan.
 *
 * Return: a cookie assigned to the tx descriptor
 */
static dma_cookie_t
xilinx_dpdma_chan_submit_tx_desc(struct xilinx_dpdma_chan *chan,
				 struct xilinx_dpdma_tx_desc *tx_desc)
{
	struct xilinx_dpdma_sw_desc *sw_desc;
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	if (chan->submitted_desc) {
		cookie = chan->submitted_desc->async_tx.cookie;
		goto out_unlock;
	}

	cookie = dma_cookie_assign(&tx_desc->async_tx);

	/* Assign the cookie to descriptors in this transaction */
	/* Only 16 bit will be used, but it should be enough */
	list_for_each_entry(sw_desc, &tx_desc->descriptors, node)
		sw_desc->hw.desc_id = cookie;

	if (tx_desc != chan->allocated_desc)
		dev_err(chan->xdev->dev, "desc != allocated_desc\n");
	else
		chan->allocated_desc = NULL;
	chan->submitted_desc = tx_desc;

	if (chan->id == VIDEO1 || chan->id == VIDEO2) {
		chan->video_group = true;
		chan->xdev->chan[VIDEO0]->video_group = true;
	}

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * xilinx_dpdma_chan_free_desc_list - Free a descriptor list
 * @chan: DPDMA channel
 * @list: tx descriptor list
 *
 * Free tx descriptors in the list @list.
 */
static void xilinx_dpdma_chan_free_desc_list(struct xilinx_dpdma_chan *chan,
					     struct list_head *list)
{
	struct xilinx_dpdma_tx_desc *tx_desc, *next;

	list_for_each_entry_safe(tx_desc, next, list, node) {
		list_del(&tx_desc->node);
		xilinx_dpdma_chan_free_tx_desc(chan, tx_desc);
	}
}

/**
 * xilinx_dpdma_chan_free_all_desc - Free all descriptors of the channel
 * @chan: DPDMA channel
 *
 * Free all descriptors associated with the channel. The channel should be
 * disabled before this function is called, otherwise, this function may
 * result in misbehavior of the system due to remaining outstanding
 * transactions.
 */
static void xilinx_dpdma_chan_free_all_desc(struct xilinx_dpdma_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	dev_dbg(chan->xdev->dev, "chan->status = %s\n",
		chan->status == STREAMING ? "STREAMING" : "IDLE");

	xilinx_dpdma_chan_free_tx_desc(chan, chan->allocated_desc);
	chan->allocated_desc = NULL;
	xilinx_dpdma_chan_free_tx_desc(chan, chan->submitted_desc);
	chan->submitted_desc = NULL;
	xilinx_dpdma_chan_free_tx_desc(chan, chan->pending_desc);
	chan->pending_desc = NULL;
	xilinx_dpdma_chan_free_tx_desc(chan, chan->active_desc);
	chan->active_desc = NULL;
	xilinx_dpdma_chan_free_desc_list(chan, &chan->done_list);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dpdma_chan_cleanup_desc - Clean up descriptors
 * @chan: DPDMA channel
 *
 * Trigger the complete callbacks of descriptors with finished transactions.
 * Free descriptors which are no longer in use.
 */
static void xilinx_dpdma_chan_cleanup_desc(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_tx_desc *desc;
	dma_async_tx_callback callback;
	void *callback_param;
	unsigned long flags;
	unsigned int cnt, i;

	spin_lock_irqsave(&chan->lock, flags);

	while (!list_empty(&chan->done_list)) {
		desc = list_first_entry(&chan->done_list,
					struct xilinx_dpdma_tx_desc, node);
		list_del(&desc->node);

		cnt = desc->done_cnt;
		desc->done_cnt = 0;
		callback = desc->async_tx.callback;
		callback_param = desc->async_tx.callback_param;
		if (callback) {
			spin_unlock_irqrestore(&chan->lock, flags);
			for (i = 0; i < cnt; i++)
				callback(callback_param);
			spin_lock_irqsave(&chan->lock, flags);
		}

		xilinx_dpdma_chan_free_tx_desc(chan, desc);
	}

	if (chan->active_desc) {
		cnt = chan->active_desc->done_cnt;
		chan->active_desc->done_cnt = 0;
		callback = chan->active_desc->async_tx.callback;
		callback_param = chan->active_desc->async_tx.callback_param;
		if (callback) {
			spin_unlock_irqrestore(&chan->lock, flags);
			for (i = 0; i < cnt; i++)
				callback(callback_param);
			spin_lock_irqsave(&chan->lock, flags);
		}
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dpdma_chan_desc_active - Set the descriptor as active
 * @chan: DPDMA channel
 *
 * Make the pending descriptor @chan->pending_desc as active. This function
 * should be called when the channel starts operating on the pending descriptor.
 */
static void xilinx_dpdma_chan_desc_active(struct xilinx_dpdma_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	if (!chan->pending_desc)
		goto out_unlock;

	if (chan->active_desc)
		list_add_tail(&chan->active_desc->node, &chan->done_list);

	chan->active_desc = chan->pending_desc;
	chan->pending_desc = NULL;

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dpdma_chan_desc_done_intr - Mark the current descriptor as 'done'
 * @chan: DPDMA channel
 *
 * Mark the current active descriptor @chan->active_desc as 'done'. This
 * function should be called to mark completion of the currently active
 * descriptor.
 */
static void xilinx_dpdma_chan_desc_done_intr(struct xilinx_dpdma_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	xilinx_dpdma_debugfs_intr_done_count_incr(chan->id);

	if (!chan->active_desc) {
		dev_dbg(chan->xdev->dev, "done intr with no active desc\n");
		goto out_unlock;
	}

	chan->active_desc->done_cnt++;
	if (chan->active_desc->status ==  PREPARED) {
		dma_cookie_complete(&chan->active_desc->async_tx);
		chan->active_desc->status = ACTIVE;
	}

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
	tasklet_schedule(&chan->done_task);
}

/**
 * xilinx_dpdma_chan_prep_slave_sg - Prepare a scatter-gather dma descriptor
 * @chan: DPDMA channel
 * @sgl: scatter-gather list
 *
 * Prepare a tx descriptor incudling internal software/hardware descriptors
 * for the given scatter-gather transaction.
 *
 * Return: A dma async tx descriptor on success, or NULL.
 */
static struct dma_async_tx_descriptor *
xilinx_dpdma_chan_prep_slave_sg(struct xilinx_dpdma_chan *chan,
				struct scatterlist *sgl)
{
	struct xilinx_dpdma_tx_desc *tx_desc;
	struct xilinx_dpdma_sw_desc *sw_desc, *last = NULL;
	struct scatterlist *iter = sgl;
	u32 line_size = 0;

	if (chan->allocated_desc)
		return &chan->allocated_desc->async_tx;

	tx_desc = xilinx_dpdma_chan_alloc_tx_desc(chan);
	if (!tx_desc)
		return NULL;

	while (!sg_is_chain(iter))
		line_size += sg_dma_len(iter++);

	while (sgl) {
		struct xilinx_dpdma_hw_desc *hw_desc;
		dma_addr_t dma_addr[4];
		unsigned int num_pages = 0;

		sw_desc = xilinx_dpdma_chan_alloc_sw_desc(chan);
		if (!sw_desc)
			goto error;

		while (!sg_is_chain(sgl) && !sg_is_last(sgl)) {
			dma_addr[num_pages] = sg_dma_address(sgl++);
			if (!IS_ALIGNED(dma_addr[num_pages++],
					XILINX_DPDMA_ALIGN_BYTES)) {
				dev_err(chan->xdev->dev,
					"buffer should be aligned at %d B\n",
					XILINX_DPDMA_ALIGN_BYTES);
				goto error;
			}
		}

		chan->xdev->desc_addr(sw_desc, last, dma_addr, num_pages);
		hw_desc = &sw_desc->hw;
		hw_desc->xfer_size = line_size;
		hw_desc->hsize_stride =
			line_size << XILINX_DPDMA_DESC_HSIZE_STRIDE_HSIZE_SHIFT;
		hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_PREEMBLE;
		hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_FRAG_MODE;
		hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_IGNORE_DONE;

		list_add_tail(&sw_desc->node, &tx_desc->descriptors);
		last = sw_desc;
		if (sg_is_last(sgl))
			break;
		sgl = sg_chain_ptr(sgl);
	}

	sw_desc = list_first_entry(&tx_desc->descriptors,
				   struct xilinx_dpdma_sw_desc, node);
	if (chan->xdev->ext_addr)
		xilinx_dpdma_sw_desc_next_64(last, sw_desc);
	else
		xilinx_dpdma_sw_desc_next_32(last, sw_desc);
	last->hw.control |= XILINX_DPDMA_DESC_CONTROL_COMPLETE_INTR;
	last->hw.control |= XILINX_DPDMA_DESC_CONTROL_LAST_OF_FRAME;

	chan->allocated_desc = tx_desc;

	return &tx_desc->async_tx;

error:
	xilinx_dpdma_chan_free_tx_desc(chan, tx_desc);

	return NULL;
}

/**
 * xilinx_dpdma_chan_prep_cyclic - Prepare a cyclic dma descriptor
 * @chan: DPDMA channel
 * @buf_addr: buffer address
 * @buf_len: buffer length
 * @period_len: number of periods
 *
 * Prepare a tx descriptor incudling internal software/hardware descriptors
 * for the given cyclic transaction.
 *
 * Return: A dma async tx descriptor on success, or NULL.
 */
static struct dma_async_tx_descriptor *
xilinx_dpdma_chan_prep_cyclic(struct xilinx_dpdma_chan *chan,
			      dma_addr_t buf_addr, size_t buf_len,
			      size_t period_len)
{
	struct xilinx_dpdma_tx_desc *tx_desc;
	struct xilinx_dpdma_sw_desc *sw_desc, *last = NULL;
	unsigned int periods = buf_len / period_len;
	unsigned int i;

	if (chan->allocated_desc)
		return &chan->allocated_desc->async_tx;

	tx_desc = xilinx_dpdma_chan_alloc_tx_desc(chan);
	if (!tx_desc)
		return NULL;

	for (i = 0; i < periods; i++) {
		struct xilinx_dpdma_hw_desc *hw_desc;

		if (!IS_ALIGNED(buf_addr, XILINX_DPDMA_ALIGN_BYTES)) {
			dev_err(chan->xdev->dev,
				"buffer should be aligned at %d B\n",
				XILINX_DPDMA_ALIGN_BYTES);
			goto error;
		}

		sw_desc = xilinx_dpdma_chan_alloc_sw_desc(chan);
		if (!sw_desc)
			goto error;

		chan->xdev->desc_addr(sw_desc, last, &buf_addr, 1);
		hw_desc = &sw_desc->hw;
		hw_desc->xfer_size = period_len;
		hw_desc->hsize_stride =
			period_len <<
			XILINX_DPDMA_DESC_HSIZE_STRIDE_HSIZE_SHIFT;
		hw_desc->hsize_stride |=
			period_len <<
			XILINX_DPDMA_DESC_HSIZE_STRIDE_STRIDE_SHIFT;
		hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_PREEMBLE;
		hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_IGNORE_DONE;
		hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_COMPLETE_INTR;

		list_add_tail(&sw_desc->node, &tx_desc->descriptors);

		buf_addr += period_len;
		last = sw_desc;
	}

	sw_desc = list_first_entry(&tx_desc->descriptors,
				   struct xilinx_dpdma_sw_desc, node);
	if (chan->xdev->ext_addr)
		xilinx_dpdma_sw_desc_next_64(last, sw_desc);
	else
		xilinx_dpdma_sw_desc_next_32(last, sw_desc);
	last->hw.control |= XILINX_DPDMA_DESC_CONTROL_LAST_OF_FRAME;

	chan->allocated_desc = tx_desc;

	return &tx_desc->async_tx;

error:
	xilinx_dpdma_chan_free_tx_desc(chan, tx_desc);

	return NULL;
}

/**
 * xilinx_dpdma_chan_prep_interleaved - Prepare a interleaved dma descriptor
 * @chan: DPDMA channel
 * @xt: dma interleaved template
 *
 * Prepare a tx descriptor incudling internal software/hardware descriptors
 * based on @xt.
 *
 * Return: A dma async tx descriptor on success, or NULL.
 */
static struct dma_async_tx_descriptor *
xilinx_dpdma_chan_prep_interleaved(struct xilinx_dpdma_chan *chan,
				   struct dma_interleaved_template *xt)
{
	struct xilinx_dpdma_tx_desc *tx_desc;
	struct xilinx_dpdma_sw_desc *sw_desc;
	struct xilinx_dpdma_hw_desc *hw_desc;
	size_t hsize = xt->sgl[0].size;
	size_t stride = hsize + xt->sgl[0].icg;

	if (!IS_ALIGNED(xt->src_start, XILINX_DPDMA_ALIGN_BYTES)) {
		dev_err(chan->xdev->dev, "buffer should be aligned at %d B\n",
			XILINX_DPDMA_ALIGN_BYTES);
		return NULL;
	}

	if (chan->allocated_desc)
		return &chan->allocated_desc->async_tx;

	tx_desc = xilinx_dpdma_chan_alloc_tx_desc(chan);
	if (!tx_desc)
		return NULL;

	sw_desc = xilinx_dpdma_chan_alloc_sw_desc(chan);
	if (!sw_desc)
		goto error;

	chan->xdev->desc_addr(sw_desc, sw_desc, &xt->src_start, 1);
	hw_desc = &sw_desc->hw;
	hw_desc->xfer_size = hsize * xt->numf;
	hw_desc->hsize_stride = hsize <<
				XILINX_DPDMA_DESC_HSIZE_STRIDE_HSIZE_SHIFT;
	hw_desc->hsize_stride |= (stride / 16) <<
				 XILINX_DPDMA_DESC_HSIZE_STRIDE_STRIDE_SHIFT;
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_PREEMBLE;
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_COMPLETE_INTR;
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_IGNORE_DONE;
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_LAST_OF_FRAME;

	list_add_tail(&sw_desc->node, &tx_desc->descriptors);
	chan->allocated_desc = tx_desc;

	return &tx_desc->async_tx;

error:
	xilinx_dpdma_chan_free_tx_desc(chan, tx_desc);

	return NULL;
}

/* Xilinx DPDMA channel operations */

/**
 * xilinx_dpdma_chan_enable - Enable the channel
 * @chan: DPDMA channel
 *
 * Enable the channel and its interrupts. Set the QoS values for video class.
 */
static inline void xilinx_dpdma_chan_enable(struct xilinx_dpdma_chan *chan)
{
	u32 reg;

	reg = XILINX_DPDMA_INTR_CHAN_MASK << chan->id;
	reg |= XILINX_DPDMA_INTR_GLOBAL_MASK;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN, reg);
	reg = XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id;
	reg |= XILINX_DPDMA_INTR_GLOBAL_ERR;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_EIEN, reg);

	reg = XILINX_DPDMA_CH_CNTL_ENABLE;
	reg |= XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS <<
	       XILINX_DPDMA_CH_CNTL_QOS_DSCR_WR_SHIFT;
	reg |= XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS <<
	       XILINX_DPDMA_CH_CNTL_QOS_DSCR_RD_SHIFT;
	reg |= XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS <<
	       XILINX_DPDMA_CH_CNTL_QOS_DATA_RD_SHIFT;
	dpdma_set(chan->reg, XILINX_DPDMA_CH_CNTL, reg);
}

/**
 * xilinx_dpdma_chan_disable - Disable the channel
 * @chan: DPDMA channel
 *
 * Disable the channel and its interrupts.
 */
static inline void xilinx_dpdma_chan_disable(struct xilinx_dpdma_chan *chan)
{
	u32 reg;

	reg = XILINX_DPDMA_INTR_CHAN_MASK << chan->id;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN, reg);
	reg = XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_EIEN, reg);

	dpdma_clr(chan->reg, XILINX_DPDMA_CH_CNTL, XILINX_DPDMA_CH_CNTL_ENABLE);
}

/**
 * xilinx_dpdma_chan_pause - Pause the channel
 * @chan: DPDMA channel
 *
 * Pause the channel.
 */
static inline void xilinx_dpdma_chan_pause(struct xilinx_dpdma_chan *chan)
{
	dpdma_set(chan->reg, XILINX_DPDMA_CH_CNTL, XILINX_DPDMA_CH_CNTL_PAUSE);
}

/**
 * xilinx_dpdma_chan_unpause - Unpause the channel
 * @chan: DPDMA channel
 *
 * Unpause the channel.
 */
static inline void xilinx_dpdma_chan_unpause(struct xilinx_dpdma_chan *chan)
{
	dpdma_clr(chan->reg, XILINX_DPDMA_CH_CNTL, XILINX_DPDMA_CH_CNTL_PAUSE);
}

static u32
xilinx_dpdma_chan_video_group_ready(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_device *xdev = chan->xdev;
	u32 i = 0, ret = 0;

	for (i = VIDEO0; i < GRAPHICS; i++) {
		if (xdev->chan[i]->video_group &&
		    xdev->chan[i]->status != STREAMING)
			return 0;

		if (xdev->chan[i]->video_group)
			ret |= BIT(i);
	}

	return ret;
}

/**
 * xilinx_dpdma_chan_issue_pending - Issue the pending descriptor
 * @chan: DPDMA channel
 *
 * Issue the first pending descriptor from @chan->submitted_desc. If the channel
 * is already streaming, the channel is re-triggered with the pending
 * descriptor.
 */
static void xilinx_dpdma_chan_issue_pending(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_device *xdev = chan->xdev;
	struct xilinx_dpdma_sw_desc *sw_desc;
	unsigned long flags;
	u32 reg, channels;

	spin_lock_irqsave(&chan->lock, flags);

	if (!chan->submitted_desc || chan->pending_desc)
		goto out_unlock;

	chan->pending_desc = chan->submitted_desc;
	chan->submitted_desc = NULL;

	sw_desc = list_first_entry(&chan->pending_desc->descriptors,
				   struct xilinx_dpdma_sw_desc, node);
	dpdma_write(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDR,
		    (u32)sw_desc->phys);
	if (xdev->ext_addr)
		dpdma_write(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDRE,
			    ((u64)sw_desc->phys >> 32) &
			    XILINX_DPDMA_DESC_ADDR_EXT_ADDR_MASK);

	if (chan->first_frame) {
		chan->first_frame = false;
		if (chan->video_group) {
			channels = xilinx_dpdma_chan_video_group_ready(chan);
			if (!channels)
				goto out_unlock;
			reg = channels << XILINX_DPDMA_GBL_TRIG_SHIFT;
		} else {
			reg = 1 << (XILINX_DPDMA_GBL_TRIG_SHIFT + chan->id);
		}
	} else {
		if (chan->video_group) {
			channels = xilinx_dpdma_chan_video_group_ready(chan);
			if (!channels)
				goto out_unlock;
			reg = channels << XILINX_DPDMA_GBL_RETRIG_SHIFT;
		} else {
			reg = 1 << (XILINX_DPDMA_GBL_RETRIG_SHIFT + chan->id);
		}
	}

	dpdma_write(xdev->reg, XILINX_DPDMA_GBL, reg);

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dpdma_chan_start - Start the channel
 * @chan: DPDMA channel
 *
 * Start the channel by enabling interrupts and triggering the channel.
 * If the channel is enabled already or there's no pending descriptor, this
 * function won't do anything on the channel.
 */
static void xilinx_dpdma_chan_start(struct xilinx_dpdma_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	if (!chan->submitted_desc || chan->status == STREAMING)
		goto out_unlock;

	xilinx_dpdma_chan_unpause(chan);
	xilinx_dpdma_chan_enable(chan);
	chan->first_frame = true;
	chan->status = STREAMING;

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dpdma_chan_ostand - Number of outstanding transactions
 * @chan: DPDMA channel
 *
 * Read and return the number of outstanding transactions from register.
 *
 * Return: Number of outstanding transactions from the status register.
 */
static inline u32 xilinx_dpdma_chan_ostand(struct xilinx_dpdma_chan *chan)
{
	return dpdma_read(chan->reg, XILINX_DPDMA_CH_STATUS) >>
	       XILINX_DPDMA_CH_STATUS_OTRAN_CNT_SHIFT &
	       XILINX_DPDMA_CH_STATUS_OTRAN_CNT_MASK;
}

/**
 * xilinx_dpdma_chan_no_ostand - Notify no outstanding transaction event
 * @chan: DPDMA channel
 *
 * Notify waiters for no outstanding event, so waiters can stop the channel
 * safely. This function is supposed to be called when 'no oustanding' interrupt
 * is generated. The 'no outstanding' interrupt is disabled and should be
 * re-enabled when this event is handled. If the channel status register still
 * shows some number of outstanding transactions, the interrupt remains enabled.
 *
 * Return: 0 on success. On failure, -EWOULDBLOCK if there's still outstanding
 * transaction(s).
 */
static int xilinx_dpdma_chan_notify_no_ostand(struct xilinx_dpdma_chan *chan)
{
	u32 cnt;

	cnt = xilinx_dpdma_chan_ostand(chan);
	if (cnt) {
		dev_dbg(chan->xdev->dev, "%d outstanding transactions\n", cnt);
		return -EWOULDBLOCK;
	}

	/* Disable 'no oustanding' interrupt */
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_IDS,
		    1 << (XILINX_DPDMA_INTR_NO_OSTAND_SHIFT + chan->id));
	wake_up(&chan->wait_to_stop);

	return 0;
}

/**
 * xilinx_dpdma_chan_wait_no_ostand - Wait for the oustanding transaction intr
 * @chan: DPDMA channel
 *
 * Wait for the no outstanding transaction interrupt. This functions can sleep
 * for 50ms.
 *
 * Return: 0 on success. On failure, -ETIMEOUT for time out, or the error code
 * from wait_event_interruptible_timeout().
 */
static int xilinx_dpdma_chan_wait_no_ostand(struct xilinx_dpdma_chan *chan)
{
	int ret;

	/* Wait for a no outstanding transaction interrupt upto 50msec */
	ret = wait_event_interruptible_timeout(chan->wait_to_stop,
					       !xilinx_dpdma_chan_ostand(chan),
					       msecs_to_jiffies(50));
	if (ret > 0) {
		dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN,
			    1 <<
			    (XILINX_DPDMA_INTR_NO_OSTAND_SHIFT + chan->id));
		return 0;
	}

	dev_err(chan->xdev->dev, "not ready to stop: %d trans\n",
		xilinx_dpdma_chan_ostand(chan));

	if (ret == 0)
		return -ETIMEDOUT;

	return ret;
}

/**
 * xilinx_dpdma_chan_poll_no_ostand - Poll the oustanding transaction status reg
 * @chan: DPDMA channel
 *
 * Poll the outstanding transaction status, and return when there's no
 * outstanding transaction. This functions can be used in the interrupt context
 * or where the atomicity is required. Calling thread may wait more than 50ms.
 *
 * Return: 0 on success, or -ETIMEDOUT.
 */
static int xilinx_dpdma_chan_poll_no_ostand(struct xilinx_dpdma_chan *chan)
{
	u32 cnt, loop = 50000;

	/* Poll at least for 50ms (20 fps). */
	do {
		cnt = xilinx_dpdma_chan_ostand(chan);
		udelay(1);
	} while (loop-- > 0 && cnt);

	if (loop) {
		dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN,
			    1 <<
			    (XILINX_DPDMA_INTR_NO_OSTAND_SHIFT + chan->id));
		return 0;
	}

	dev_err(chan->xdev->dev, "not ready to stop: %d trans\n",
		xilinx_dpdma_chan_ostand(chan));

	return -ETIMEDOUT;
}

/**
 * xilinx_dpdma_chan_stop - Stop the channel
 * @chan: DPDMA channel
 *
 * Stop the channel with the following sequence: 1. Pause, 2. Wait (sleep) for
 * no outstanding transaction interrupt, 3. Disable the channel.
 *
 * Return: 0 on success, or error code from xilinx_dpdma_chan_wait_no_ostand().
 */
static int xilinx_dpdma_chan_stop(struct xilinx_dpdma_chan *chan)
{
	unsigned long flags;
	bool ret;

	xilinx_dpdma_chan_pause(chan);
	ret = xilinx_dpdma_chan_wait_no_ostand(chan);
	if (ret)
		return ret;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_dpdma_chan_disable(chan);
	chan->status = IDLE;
	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

/**
 * xilinx_dpdma_chan_alloc_resources - Allocate resources for the channel
 * @chan: DPDMA channel
 *
 * Allocate a descriptor pool for the channel.
 *
 * Return: 0 on success, or -ENOMEM if failed to allocate a pool.
 */
static int xilinx_dpdma_chan_alloc_resources(struct xilinx_dpdma_chan *chan)
{
	chan->desc_pool = dma_pool_create(dev_name(chan->xdev->dev),
				chan->xdev->dev,
				sizeof(struct xilinx_dpdma_sw_desc),
				__alignof__(struct xilinx_dpdma_sw_desc), 0);
	if (!chan->desc_pool) {
		dev_err(chan->xdev->dev,
			"failed to allocate a descriptor pool\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * xilinx_dpdma_chan_free_resources - Free all resources for the channel
 * @chan: DPDMA channel
 *
 * Free all descriptors and the descriptor pool for the channel.
 */
static void xilinx_dpdma_chan_free_resources(struct xilinx_dpdma_chan *chan)
{
	xilinx_dpdma_chan_free_all_desc(chan);
	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

/**
 * xilinx_dpdma_chan_terminate_all - Terminate the channel and descriptors
 * @chan: DPDMA channel
 *
 * Stop the channel and free all associated descriptors.
 *
 * Return: 0 on success, or the error code from xilinx_dpdma_chan_stop().
 */
static int xilinx_dpdma_chan_terminate_all(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_device *xdev = chan->xdev;
	int ret;
	unsigned int i;

	if (chan->video_group) {
		for (i = VIDEO0; i < GRAPHICS; i++) {
			if (xdev->chan[i]->video_group &&
			    xdev->chan[i]->status == STREAMING) {
				xilinx_dpdma_chan_pause(xdev->chan[i]);
				xdev->chan[i]->video_group = false;
			}
		}
	}

	ret = xilinx_dpdma_chan_stop(chan);
	if (ret)
		return ret;

	xilinx_dpdma_chan_free_all_desc(chan);

	return 0;
}

/**
 * xilinx_dpdma_chan_err - Detect any channel error
 * @chan: DPDMA channel
 * @isr: masked Interrupt Status Register
 * @eisr: Error Interrupt Status Register
 *
 * Return: true if any channel error occurs, or false otherwise.
 */
static bool
xilinx_dpdma_chan_err(struct xilinx_dpdma_chan *chan, u32 isr, u32 eisr)
{
	if (!chan)
		return false;

	if (chan->status == STREAMING &&
	    ((isr & (XILINX_DPDMA_INTR_CHAN_ERR_MASK << chan->id)) ||
	    (eisr & (XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id))))
		return true;

	return false;
}

/**
 * xilinx_dpdma_chan_handle_err - DPDMA channel error handling
 * @chan: DPDMA channel
 *
 * This function is called when any channel error or any global error occurs.
 * The function disables the paused channel by errors and determines
 * if the current active descriptor can be rescheduled depending on
 * the descriptor status.
 */
static void xilinx_dpdma_chan_handle_err(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_device *xdev = chan->xdev;
	struct device *dev = xdev->dev;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	dev_dbg(dev, "cur desc addr = 0x%04x%08x\n",
		dpdma_read(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDRE),
		dpdma_read(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDR));
	dev_dbg(dev, "cur payload addr = 0x%04x%08x\n",
		dpdma_read(chan->reg, XILINX_DPDMA_CH_PYLD_CUR_ADDRE),
		dpdma_read(chan->reg, XILINX_DPDMA_CH_PYLD_CUR_ADDR));

	xilinx_dpdma_chan_disable(chan);
	chan->status = IDLE;

	/* Decide if the current descriptor can be rescheduled */
	if (chan->active_desc) {
		switch (chan->active_desc->status) {
		case ACTIVE:
		case PREPARED:
			xilinx_dpdma_chan_free_tx_desc(chan,
						       chan->submitted_desc);
			chan->submitted_desc = NULL;
			xilinx_dpdma_chan_free_tx_desc(chan,
						       chan->pending_desc);
			chan->pending_desc = NULL;
			chan->active_desc->status = ERRORED;
			chan->submitted_desc = chan->active_desc;
			break;
		case ERRORED:
			dev_err(dev, "desc is dropped by unrecoverable err\n");
			xilinx_dpdma_chan_dump_tx_desc(chan, chan->active_desc);
			xilinx_dpdma_chan_free_tx_desc(chan, chan->active_desc);
			break;
		default:
			break;
		}
		chan->active_desc = NULL;
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

/* DMA tx descriptor */

static dma_cookie_t xilinx_dpdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(tx->chan);
	struct xilinx_dpdma_tx_desc *tx_desc = to_dpdma_tx_desc(tx);

	return xilinx_dpdma_chan_submit_tx_desc(chan, tx_desc);
}

/* DMA channel operations */

static struct dma_async_tx_descriptor *
xilinx_dpdma_prep_slave_sg(struct dma_chan *dchan, struct scatterlist *sgl,
			   unsigned int sg_len,
			   enum dma_transfer_direction direction,
			   unsigned long flags, void *context)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	struct dma_async_tx_descriptor *async_tx;

	if (direction != DMA_MEM_TO_DEV)
		return NULL;

	if (!sgl || sg_len < 2)
		return NULL;

	async_tx = xilinx_dpdma_chan_prep_slave_sg(chan, sgl);
	if (!async_tx)
		return NULL;

	dma_async_tx_descriptor_init(async_tx, dchan);
	async_tx->tx_submit = xilinx_dpdma_tx_submit;
	async_tx->flags = flags;
	async_tx_ack(async_tx);

	return async_tx;
}

static struct dma_async_tx_descriptor *
xilinx_dpdma_prep_dma_cyclic(struct dma_chan *dchan, dma_addr_t buf_addr,
			     size_t buf_len, size_t period_len,
			     enum dma_transfer_direction direction,
			     unsigned long flags)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	struct dma_async_tx_descriptor *async_tx;

	if (direction != DMA_MEM_TO_DEV)
		return NULL;

	if (buf_len % period_len)
		return NULL;

	async_tx = xilinx_dpdma_chan_prep_cyclic(chan, buf_addr, buf_len,
						 period_len);
	if (!async_tx)
		return NULL;

	dma_async_tx_descriptor_init(async_tx, dchan);
	async_tx->tx_submit = xilinx_dpdma_tx_submit;
	async_tx->flags = flags;
	async_tx_ack(async_tx);

	return async_tx;
}

static struct dma_async_tx_descriptor *
xilinx_dpdma_prep_interleaved_dma(struct dma_chan *dchan,
				  struct dma_interleaved_template *xt,
				  unsigned long flags)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	struct dma_async_tx_descriptor *async_tx;

	if (xt->dir != DMA_MEM_TO_DEV)
		return NULL;

	if (!xt->numf || !xt->sgl[0].size)
		return NULL;

	async_tx = xilinx_dpdma_chan_prep_interleaved(chan, xt);
	if (!async_tx)
		return NULL;

	dma_async_tx_descriptor_init(async_tx, dchan);
	async_tx->tx_submit = xilinx_dpdma_tx_submit;
	async_tx->flags = flags;
	async_tx_ack(async_tx);

	return async_tx;
}

static int xilinx_dpdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);

	dma_cookie_init(dchan);

	return xilinx_dpdma_chan_alloc_resources(chan);
}

static void xilinx_dpdma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);

	xilinx_dpdma_chan_free_resources(chan);
}

static enum dma_status xilinx_dpdma_tx_status(struct dma_chan *dchan,
					      dma_cookie_t cookie,
					      struct dma_tx_state *txstate)
{
	return dma_cookie_status(dchan, cookie, txstate);
}

static void xilinx_dpdma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);

	xilinx_dpdma_chan_start(chan);
	xilinx_dpdma_chan_issue_pending(chan);
}

static int xilinx_dpdma_config(struct dma_chan *dchan,
			       struct dma_slave_config *config)
{
	if (config->direction != DMA_MEM_TO_DEV)
		return -EINVAL;

	return 0;
}

static int xilinx_dpdma_pause(struct dma_chan *dchan)
{
	xilinx_dpdma_chan_pause(to_xilinx_chan(dchan));

	return 0;
}

static int xilinx_dpdma_resume(struct dma_chan *dchan)
{
	xilinx_dpdma_chan_unpause(to_xilinx_chan(dchan));

	return 0;
}

static int xilinx_dpdma_terminate_all(struct dma_chan *dchan)
{
	return xilinx_dpdma_chan_terminate_all(to_xilinx_chan(dchan));
}

/* Xilinx DPDMA device operations */

/**
 * xilinx_dpdma_err - Detect any global error
 * @isr: Interrupt Status Register
 * @eisr: Error Interrupt Status Register
 *
 * Return: True if any global error occurs, or false otherwise.
 */
static bool xilinx_dpdma_err(u32 isr, u32 eisr)
{
	if ((isr & XILINX_DPDMA_INTR_GLOBAL_ERR ||
	    eisr & XILINX_DPDMA_EINTR_GLOBAL_ERR))
		return true;

	return false;
}

/**
 * xilinx_dpdma_handle_err_intr - Handle DPDMA error interrupt
 * @xdev: DPDMA device
 * @isr: masked Interrupt Status Register
 * @eisr: Error Interrupt Status Register
 *
 * Handle if any error occurs based on @isr and @eisr. This function disables
 * corresponding error interrupts, and those should be re-enabled once handling
 * is done.
 */
static void xilinx_dpdma_handle_err_intr(struct xilinx_dpdma_device *xdev,
					 u32 isr, u32 eisr)
{
	bool err = xilinx_dpdma_err(isr, eisr);
	unsigned int i;

	dev_err(xdev->dev, "error intr: isr = 0x%08x, eisr = 0x%08x\n",
		isr, eisr);

	/* Disable channel error interrupts until errors are handled. */
	dpdma_write(xdev->reg, XILINX_DPDMA_IDS,
		    isr & ~XILINX_DPDMA_INTR_GLOBAL_ERR);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIDS,
		    eisr & ~XILINX_DPDMA_EINTR_GLOBAL_ERR);

	for (i = 0; i < XILINX_DPDMA_NUM_CHAN; i++)
		if (err || xilinx_dpdma_chan_err(xdev->chan[i], isr, eisr))
			tasklet_schedule(&xdev->chan[i]->err_task);
}

/**
 * xilinx_dpdma_handle_vsync_intr - Handle the VSYNC interrupt
 * @xdev: DPDMA device
 *
 * Handle the VSYNC event. At this point, the current frame becomes active,
 * which means the DPDMA actually starts fetching, and the next frame can be
 * scheduled.
 */
static void xilinx_dpdma_handle_vsync_intr(struct xilinx_dpdma_device *xdev)
{
	unsigned int i;

	for (i = 0; i < XILINX_DPDMA_NUM_CHAN; i++) {
		if (xdev->chan[i] &&
		    xdev->chan[i]->status == STREAMING) {
			xilinx_dpdma_chan_desc_active(xdev->chan[i]);
			xilinx_dpdma_chan_issue_pending(xdev->chan[i]);
		}
	}
}

/**
 * xilinx_dpdma_enable_intr - Enable interrupts
 * @xdev: DPDMA device
 *
 * Enable interrupts.
 */
static void xilinx_dpdma_enable_intr(struct xilinx_dpdma_device *xdev)
{
	dpdma_write(xdev->reg, XILINX_DPDMA_IEN, XILINX_DPDMA_INTR_ALL);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIEN, XILINX_DPDMA_EINTR_ALL);
}

/**
 * xilinx_dpdma_disable_intr - Disable interrupts
 * @xdev: DPDMA device
 *
 * Disable interrupts.
 */
static void xilinx_dpdma_disable_intr(struct xilinx_dpdma_device *xdev)
{
	dpdma_write(xdev->reg, XILINX_DPDMA_IDS, XILINX_DPDMA_INTR_ERR_ALL);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIDS, XILINX_DPDMA_EINTR_ALL);
}

/* Interrupt handling operations*/

/**
 * xilinx_dpdma_chan_err_task - Per channel tasklet for error handling
 * @data: tasklet data to be casted to DPDMA channel structure
 *
 * Per channel error handling tasklet. This function waits for the outstanding
 * transaction to complete and triggers error handling. After error handling,
 * re-enable channel error interrupts, and restart the channel if needed.
 */
static void xilinx_dpdma_chan_err_task(unsigned long data)
{
	struct xilinx_dpdma_chan *chan = (struct xilinx_dpdma_chan *)data;
	struct xilinx_dpdma_device *xdev = chan->xdev;

	/* Proceed error handling even when polling fails. */
	xilinx_dpdma_chan_poll_no_ostand(chan);

	xilinx_dpdma_chan_handle_err(chan);

	dpdma_write(xdev->reg, XILINX_DPDMA_IEN,
		    XILINX_DPDMA_INTR_CHAN_ERR_MASK << chan->id);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIEN,
		    XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id);

	xilinx_dpdma_chan_start(chan);
	xilinx_dpdma_chan_issue_pending(chan);
}

/**
 * xilinx_dpdma_chan_done_task - Per channel tasklet for done interrupt handling
 * @data: tasklet data to be casted to DPDMA channel structure
 *
 * Per channel done interrupt handling tasklet.
 */
static void xilinx_dpdma_chan_done_task(unsigned long data)
{
	struct xilinx_dpdma_chan *chan = (struct xilinx_dpdma_chan *)data;

	xilinx_dpdma_chan_cleanup_desc(chan);
}

static irqreturn_t xilinx_dpdma_irq_handler(int irq, void *data)
{
	struct xilinx_dpdma_device *xdev = data;
	u32 status, error, i;
	unsigned long masked;

	status = dpdma_read(xdev->reg, XILINX_DPDMA_ISR);
	error = dpdma_read(xdev->reg, XILINX_DPDMA_EISR);
	if (!status && !error)
		return IRQ_NONE;

	dpdma_write(xdev->reg, XILINX_DPDMA_ISR, status);
	dpdma_write(xdev->reg, XILINX_DPDMA_EISR, error);

	if (status & XILINX_DPDMA_INTR_VSYNC)
		xilinx_dpdma_handle_vsync_intr(xdev);

	masked = (status & XILINX_DPDMA_INTR_DESC_DONE_MASK) >>
		 XILINX_DPDMA_INTR_DESC_DONE_SHIFT;
	if (masked)
		for_each_set_bit(i, &masked, XILINX_DPDMA_NUM_CHAN)
			xilinx_dpdma_chan_desc_done_intr(xdev->chan[i]);

	masked = (status & XILINX_DPDMA_INTR_NO_OSTAND_MASK) >>
		 XILINX_DPDMA_INTR_NO_OSTAND_SHIFT;
	if (masked)
		for_each_set_bit(i, &masked, XILINX_DPDMA_NUM_CHAN)
			xilinx_dpdma_chan_notify_no_ostand(xdev->chan[i]);

	masked = status & XILINX_DPDMA_INTR_ERR_ALL;
	if (masked || error)
		xilinx_dpdma_handle_err_intr(xdev, masked, error);

	return IRQ_HANDLED;
}

/* Initialization operations */

static struct xilinx_dpdma_chan *
xilinx_dpdma_chan_probe(struct device_node *node,
			struct xilinx_dpdma_device *xdev)
{
	struct xilinx_dpdma_chan *chan;

	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return ERR_PTR(-ENOMEM);

	if (of_device_is_compatible(node, "xlnx,video0")) {
		chan->id = VIDEO0;
	} else if (of_device_is_compatible(node, "xlnx,video1")) {
		chan->id = VIDEO1;
	} else if (of_device_is_compatible(node, "xlnx,video2")) {
		chan->id = VIDEO2;
	} else if (of_device_is_compatible(node, "xlnx,graphics")) {
		chan->id = GRAPHICS;
	} else if (of_device_is_compatible(node, "xlnx,audio0")) {
		chan->id = AUDIO0;
	} else if (of_device_is_compatible(node, "xlnx,audio1")) {
		chan->id = AUDIO1;
	} else {
		dev_err(xdev->dev, "invalid channel compatible string in DT\n");
		return ERR_PTR(-EINVAL);
	}

	chan->reg = xdev->reg + XILINX_DPDMA_CH_BASE + XILINX_DPDMA_CH_OFFSET *
		    chan->id;
	chan->status = IDLE;

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->done_list);
	init_waitqueue_head(&chan->wait_to_stop);

	tasklet_init(&chan->done_task, xilinx_dpdma_chan_done_task,
		     (unsigned long)chan);
	tasklet_init(&chan->err_task, xilinx_dpdma_chan_err_task,
		     (unsigned long)chan);

	chan->common.device = &xdev->common;
	chan->xdev = xdev;

	list_add_tail(&chan->common.device_node, &xdev->common.channels);
	xdev->chan[chan->id] = chan;

	return chan;
}

static void xilinx_dpdma_chan_remove(struct xilinx_dpdma_chan *chan)
{
	tasklet_kill(&chan->err_task);
	tasklet_kill(&chan->done_task);
	list_del(&chan->common.device_node);
}

static struct dma_chan *of_dma_xilinx_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct xilinx_dpdma_device *xdev = ofdma->of_dma_data;
	uint32_t chan_id = dma_spec->args[0];

	if (chan_id >= XILINX_DPDMA_NUM_CHAN)
		return NULL;

	if (!xdev->chan[chan_id])
		return NULL;

	return dma_get_slave_channel(&xdev->chan[chan_id]->common);
}

static int xilinx_dpdma_probe(struct platform_device *pdev)
{
	struct xilinx_dpdma_device *xdev;
	struct xilinx_dpdma_chan *chan;
	struct dma_device *ddev;
	struct resource *res;
	struct device_node *node, *child;
	u32 i;
	int irq, ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;
	ddev = &xdev->common;
	ddev->dev = &pdev->dev;
	node = xdev->dev->of_node;

	xdev->axi_clk = devm_clk_get(xdev->dev, "axi_clk");
	if (IS_ERR(xdev->axi_clk))
		return PTR_ERR(xdev->axi_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xdev->reg))
		return PTR_ERR(xdev->reg);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(xdev->dev, "failed to get platform irq\n");
		return irq;
	}

	ret = devm_request_irq(xdev->dev, irq, xilinx_dpdma_irq_handler,
			       IRQF_SHARED, dev_name(xdev->dev), xdev);
	if (ret) {
		dev_err(xdev->dev, "failed to request IRQ\n");
		return ret;
	}

	INIT_LIST_HEAD(&xdev->common.channels);
	dma_cap_set(DMA_SLAVE, ddev->cap_mask);
	dma_cap_set(DMA_PRIVATE, ddev->cap_mask);
	dma_cap_set(DMA_CYCLIC, ddev->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, ddev->cap_mask);
	ddev->copy_align = fls(XILINX_DPDMA_ALIGN_BYTES - 1);

	ddev->device_alloc_chan_resources = xilinx_dpdma_alloc_chan_resources;
	ddev->device_free_chan_resources = xilinx_dpdma_free_chan_resources;
	ddev->device_prep_slave_sg = xilinx_dpdma_prep_slave_sg;
	ddev->device_prep_dma_cyclic = xilinx_dpdma_prep_dma_cyclic;
	ddev->device_prep_interleaved_dma = xilinx_dpdma_prep_interleaved_dma;
	ddev->device_tx_status = xilinx_dpdma_tx_status;
	ddev->device_issue_pending = xilinx_dpdma_issue_pending;
	ddev->device_config = xilinx_dpdma_config;
	ddev->device_pause = xilinx_dpdma_pause;
	ddev->device_resume = xilinx_dpdma_resume;
	ddev->device_terminate_all = xilinx_dpdma_terminate_all;
	ddev->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED);
	ddev->directions = BIT(DMA_MEM_TO_DEV);
	ddev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	for_each_child_of_node(node, child) {
		chan = xilinx_dpdma_chan_probe(child, xdev);
		if (IS_ERR(chan)) {
			dev_err(xdev->dev, "failed to probe a channel\n");
			ret = PTR_ERR(chan);
			goto error;
		}
	}

	xdev->ext_addr = sizeof(dma_addr_t) > 4;
	if (xdev->ext_addr)
		xdev->desc_addr = xilinx_dpdma_sw_desc_addr_64;
	else
		xdev->desc_addr = xilinx_dpdma_sw_desc_addr_32;

	ret = clk_prepare_enable(xdev->axi_clk);
	if (ret) {
		dev_err(xdev->dev, "failed to enable the axi clock\n");
		goto error;
	}

	ret = dma_async_device_register(ddev);
	if (ret) {
		dev_err(xdev->dev, "failed to enable the axi clock\n");
		goto error_dma_async;
	}

	ret = of_dma_controller_register(xdev->dev->of_node,
					 of_dma_xilinx_xlate, ddev);
	if (ret) {
		dev_err(xdev->dev, "failed to register DMA to DT DMA helper\n");
		goto error_of_dma;
	}

	xilinx_dpdma_enable_intr(xdev);

	xilinx_dpdma_debugfs_init(&pdev->dev);

	dev_info(&pdev->dev, "Xilinx DPDMA engine is probed\n");

	return 0;

error_of_dma:
	dma_async_device_unregister(ddev);
error_dma_async:
	clk_disable_unprepare(xdev->axi_clk);
error:
	for (i = 0; i < XILINX_DPDMA_NUM_CHAN; i++)
		if (xdev->chan[i])
			xilinx_dpdma_chan_remove(xdev->chan[i]);

	return ret;
}

static int xilinx_dpdma_remove(struct platform_device *pdev)
{
	struct xilinx_dpdma_device *xdev;
	unsigned int i;

	xdev = platform_get_drvdata(pdev);

	xilinx_dpdma_disable_intr(xdev);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&xdev->common);
	clk_disable_unprepare(xdev->axi_clk);

	for (i = 0; i < XILINX_DPDMA_NUM_CHAN; i++)
		if (xdev->chan[i])
			xilinx_dpdma_chan_remove(xdev->chan[i]);

	return 0;
}

static const struct of_device_id xilinx_dpdma_of_match[] = {
	{ .compatible = "xlnx,dpdma",},
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_dpdma_of_match);

static struct platform_driver xilinx_dpdma_driver = {
	.probe			= xilinx_dpdma_probe,
	.remove			= xilinx_dpdma_remove,
	.driver			= {
		.name		= "xilinx-dpdma",
		.of_match_table	= xilinx_dpdma_of_match,
	},
};

module_platform_driver(xilinx_dpdma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx DPDMA driver");
MODULE_LICENSE("GPL v2");

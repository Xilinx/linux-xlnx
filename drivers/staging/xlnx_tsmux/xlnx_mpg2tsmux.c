// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx TS mux driver
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Author: Venkateshwar Rao G <venkateshwar.rao.gannavarapu@xilinx.com>
 */
#include <linux/bitops.h>
#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmapool.h>
#include <linux/dma-buf.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <uapi/linux/xlnx_mpg2tsmux_interface.h>

#define DRIVER_NAME "mpegtsmux-1.0"
#define DRIVER_CLASS "mpg2mux_ts_cls"
#define DRIVER_MAX_DEV (10)

/* Register offsets and bit masks */
#define XTSMUX_RST_CTRL			0x00
#define XTSMUX_GLBL_IER			0x04
#define XTSMUX_IER_STAT			0x08
#define XTSMUX_ISR_STAT			0x0c
#define XTSMUX_ERR_STAT			0x10
#define XTSMUX_LAST_NODE_PROCESSED	0x14
#define XTSMUX_MUXCONTEXT_ADDR		0x20
#define XTSMUX_STREAMCONTEXT_ADDR	0x30
#define XTSMUX_NUM_STREAM_IDTBL		0x48
#define XTSMUX_NUM_DESC			0x70
#define XTSMUX_STREAM_IDTBL_ADDR	0x78
#define XTSMUX_CONTEXT_DATA_SIZE	64

#define XTSMUX_RST_CTRL_START_MASK	BIT(0)
#define XTSMUX_GLBL_IER_ENABLE_MASK	BIT(0)
#define XTSMUX_IER_ENABLE_MASK		BIT(0)

/* Number of input/output streams supported */
#define XTSMUX_MAXIN_STRM		112
#define XTSMUX_MAXIN_PLSTRM		16
#define XTSMUX_MAXIN_TLSTRM	(XTSMUX_MAXIN_STRM + XTSMUX_MAXIN_PLSTRM)
#define XTSMUX_MAXOUT_STRM		112
#define XTSMUX_MAXOUT_PLSTRM		16
#define XTSMUX_MAXOUT_TLSTRM	(XTSMUX_MAXOUT_STRM + XTSMUX_MAXOUT_PLSTRM)
#define XTSMUX_POOL_SIZE		128
/* Initial version is tested with 256 align only */
#define XTSMUX_POOL_ALIGN		256
#define XTSMUX_STRMBL_FREE		0
#define XTSMUX_STRMBL_BUSY		1

/**
 * struct stream_context - struct to enqueue a stream context descriptor
 * @command: stream context type
 * @is_pcr_stream: flag for pcr(programmable clock recovery) stream
 * @stream_id: stream identification number
 * @extended_stream_id: extended stream id
 * @reserved1: reserved for hardware alignment
 * @pid: packet id number
 * @dmabuf_id: 0 for buf allocated by driver, nonzero for external buf
 * @size_data_in: size in bytes of input buffer
 * @pts: presentation time stamp
 * @dts: display time stamp
 * @in_buf_pointer: physical address of src buf address
 * @reserved2: reserved for hardware alignment
 * @insert_pcr: inserting pcr in stream context
 * @reserved3: reserved for hardware alignment
 * @pcr_extension: pcr extension number
 * @pcr_base: pcr base number
 */
struct stream_context {
	enum ts_mux_command command;
	bool is_pcr_stream;
	u8 stream_id;
	u8 extended_stream_id;
	u8 reserved1;
	u16 pid;
	u16 dmabuf_id;
	u32 size_data_in;
	u64 pts;
	u64 dts;
	u64 in_buf_pointer;
	u32 reserved2;
	bool insert_pcr;
	bool reserved3;
	u16 pcr_extension;
	u64 pcr_base;
};

/**
 * enum node_status_info - status of stream context
 * @NOT_FILLED: node not filled
 * @UPDATED_BY_DRIVER: updated by driver
 * @READ_BY_IP: read by IP
 * @USED_BY_IP: used by IP
 * @NODE_INVALID: invalid node
 */
enum node_status_info {
	NOT_FILLED = 0,
	UPDATED_BY_DRIVER,
	READ_BY_IP,
	USED_BY_IP,
	NODE_INVALID
};

/**
 * enum stream_errors - stream context error type
 * @NO_ERROR: no error
 * @PARTIAL_FRAME_WRITTEN: partial frame written
 * @DESCRIPTOR_NOT_READABLE: descriptor not readable
 */
enum stream_errors {
	NO_ERROR = 0,
	PARTIAL_FRAME_WRITTEN,
	DESCRIPTOR_NOT_READABLE
};

/**
 * struct strm_node - struct to describe stream node in linked list
 * @node_number: node number to handle streams
 * @node_status: status of stream node
 * @element: stream context info
 * @error_code: error codes
 * @reserved1: reserved bits for hardware align
 * @tail_pointer: physical address of next stream node in linked list
 * @strm_phy_addr: physical address of stream context
 * @node: struct of linked list head
 * @reserved2: reserved for hardware align
 */
struct stream_context_node {
	u32 node_number;
	enum node_status_info node_status;
	struct stream_context element;
	enum stream_errors error_code;
	u32 reserved1;
	u64 tail_pointer;
	u64 strm_phy_addr;
	struct list_head node;
	u64 reserved2;
};

/**
 * struct strm_info - struct to describe streamid node in streamid table
 * @pid: identification number of stream
 * @continuity_counter: counter to maintain packet count for a stream
 * @usageflag: flag to know free or under use for allocating streamid node
 * @strmtbl_update: struct to know enqueue or dequeue streamid in table
 */
struct stream_info {
	u16 pid;
	u8 continuity_counter;
	bool usageflag;
	enum strmtbl_cnxt strmtbl_update;
};

/* Enum for error handling of mux context */
enum mux_op_errs {
	MUXER_NO_ERROR = 0,
	ERROR_OUTPUT_BUFFER_IS_NOT_ACCESIBLE,
	ERROR_PARTIAL_PACKET_WRITTEN
};

/**
 * struct muxer_context - struct to describe mux node in linked list
 * @node_status: status of mux node
 * @reserved: reserved for hardware align
 * @dst_buf_start_addr: physical address of dst buf
 * @dst_buf_size: size of the output buffer
 * @dst_buf_written: size of data written in dst buf
 * @num_of_pkts_written: number of packets in dst buf
 * @error_code: error status of mux node updated by IP
 * @mux_phy_addr: physical address of muxer
 * @node: struct of linked list head
 */
struct muxer_context {
	enum node_status_info node_status;
	u32 reserved;
	u64 dst_buf_start_addr;
	u32 dst_buf_size;
	u32 dst_buf_written;
	u32 num_of_pkts_written;
	enum mux_op_errs error_code;
	u64 mux_phy_addr;
	struct list_head node;
};

/**
 * struct xlnx_tsmux_dmabufintl - dma buf internal info
 * @dbuf: reference to a buffer's dmabuf struct
 * @attach: attachment to the buffer's dmabuf
 * @sgt: scatterlist info for the buffer's dmabuf
 * @dmabuf_addr: buffer physical address
 * @dmabuf_fd: dma buffer fd
 * @buf_id: dma buffer reference id
 */
struct xlnx_tsmux_dmabufintl {
	struct dma_buf *dbuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dmabuf_addr;
	s32 dmabuf_fd;
	u16 buf_id;
};

/**
 * struct xlnx_tsmux - xilinx mpeg2 TS muxer device
 * @dev: pointer to struct device instance used by the driver
 * @iomem: base address of the HW/IP
 * @chdev: char device handle
 * @user_count: count of users who have opened the device
 * @lock: spinlock to protect driver data structures
 * @waitq: wait queue used by the driver
 * @irq: irq number
 * @id: device instance ID
 * @num_inbuf: number of input buffers allocated uisng DMA
 * @num_outbuf: number of output buffers allocated uisng DMA
 * @srcbuf_size: size of each source buffer
 * @dstbuf_size: size of each destination buffer
 * @strm_node: list containing descriptors of stream context
 * @mux_node: list containing descriptors of mux context
 * @stcxt_node_cnt: stream number used for maintaing list
 * @num_strmnodes: number of stream nodes in the streamid table
 * @intn_stream_count: internal count of streams added to stream context
 * @outbuf_idx: index number to maintain output buffers
 * @srcbuf_addrs: physical address of source buffer
 * @dstbuf_addrs: physical address of destination buffer
 * @src_kaddrs: kernel VA for source buffer allocated by the driver
 * @dst_kaddrs: kernel VA for destination buffer allocated by the driver
 * @strm_ctx_pool: dma pool to allocate stream context buffers
 * @mux_ctx_pool: dma pool to allocate mux context buffers
 * @strmtbl_addrs: physical address of streamid table
 * @strmtbl_kaddrs: kernel VA for streamid table
 * @intn_strmtbl_addrs: physical address of streamid table for internal
 * @intn_strmtbl_kaddrs: kernel VA for streamid table for internal
 * @ap_clk: interface clock
 * @src_dmabufintl: array of src DMA buf allocated by user
 * @dst_dmabufintl: array of src DMA buf allocated by user
 * @outbuf_written: size in bytes written in output buffer
 * @stream_count: stream count
 */
struct xlnx_tsmux {
	struct device *dev;
	void __iomem *iomem;
	struct cdev chdev;
	atomic_t user_count;
	/* lock is used to protect access to sync_err and wdg_err */
	spinlock_t lock;
	wait_queue_head_t waitq;
	s32 irq;
	s32 id;
	u32 num_inbuf;
	u32 num_outbuf;
	size_t srcbuf_size;
	size_t dstbuf_size;
	struct list_head strm_node;
	struct list_head mux_node;
	u32 stcxt_node_cnt;
	u32 num_strmnodes;
	atomic_t intn_stream_count;
	atomic_t outbuf_idx;
	dma_addr_t srcbuf_addrs[XTSMUX_MAXIN_TLSTRM];
	dma_addr_t dstbuf_addrs[XTSMUX_MAXOUT_TLSTRM];
	void *src_kaddrs[XTSMUX_MAXIN_TLSTRM];
	void *dst_kaddrs[XTSMUX_MAXOUT_TLSTRM];
	struct dma_pool *strm_ctx_pool;
	struct dma_pool *mux_ctx_pool;
	dma_addr_t strmtbl_addrs;
	void *strmtbl_kaddrs;
	dma_addr_t intn_strmtbl_addrs;
	void *intn_strmtbl_kaddrs;
	struct clk *ap_clk;
	struct xlnx_tsmux_dmabufintl src_dmabufintl[XTSMUX_MAXIN_STRM];
	struct xlnx_tsmux_dmabufintl dst_dmabufintl[XTSMUX_MAXOUT_STRM];
	s32 outbuf_written;
	atomic_t stream_count;
};

static inline u32 xlnx_tsmux_read(const struct xlnx_tsmux *mpgmuxts,
				  const u32 reg)
{
	return ioread32(mpgmuxts->iomem + reg);
}

static inline void xlnx_tsmux_write(const struct xlnx_tsmux *mpgmuxts,
				    const u32 reg, const u32 val)
{
	iowrite32(val, (void __iomem *)(mpgmuxts->iomem + reg));
}

/* TODO: Optimize using iowrite64 call */
static inline void xlnx_tsmux_write64(const struct xlnx_tsmux *mpgmuxts,
				      const u32 reg, const u64 val)
{
	iowrite32(lower_32_bits(val), (void __iomem *)(mpgmuxts->iomem + reg));
	iowrite32(upper_32_bits(val), (void __iomem *)(mpgmuxts->iomem +
						       reg + 4));
}

static int xlnx_tsmux_start_muxer(struct xlnx_tsmux *mpgmuxts)
{
	struct stream_context_node *new_strm_node;
	struct muxer_context *new_mux_node;

	new_mux_node = list_first_entry_or_null(&mpgmuxts->mux_node,
						struct muxer_context, node);
	if (!new_mux_node)
		return -ENXIO;

	xlnx_tsmux_write64(mpgmuxts, XTSMUX_MUXCONTEXT_ADDR,
			   new_mux_node->mux_phy_addr);

	new_strm_node = list_first_entry_or_null(&mpgmuxts->strm_node,
						 struct stream_context_node,
						 node);
	if (!new_strm_node)
		return -ENXIO;

	xlnx_tsmux_write64(mpgmuxts, XTSMUX_STREAMCONTEXT_ADDR,
			   new_strm_node->strm_phy_addr);

	xlnx_tsmux_write(mpgmuxts, XTSMUX_NUM_DESC,
			 atomic_read(&mpgmuxts->intn_stream_count));

	xlnx_tsmux_write64(mpgmuxts, XTSMUX_STREAM_IDTBL_ADDR,
			   (u64)mpgmuxts->intn_strmtbl_addrs);
	xlnx_tsmux_write(mpgmuxts, XTSMUX_NUM_STREAM_IDTBL, 1);
	xlnx_tsmux_write(mpgmuxts, XTSMUX_GLBL_IER,
			 XTSMUX_GLBL_IER_ENABLE_MASK);
	xlnx_tsmux_write(mpgmuxts, XTSMUX_IER_STAT,
			 XTSMUX_IER_ENABLE_MASK);

	xlnx_tsmux_write(mpgmuxts, XTSMUX_RST_CTRL,
			 XTSMUX_RST_CTRL_START_MASK);

	return 0;
}

static void xlnx_tsmux_stop_muxer(const struct xlnx_tsmux *mpgmuxts)
{
	xlnx_tsmux_write(mpgmuxts, XTSMUX_GLBL_IER, 0);
	xlnx_tsmux_write(mpgmuxts, XTSMUX_IER_STAT, 0);
	xlnx_tsmux_write(mpgmuxts, XTSMUX_RST_CTRL, 0);
}

static enum xlnx_tsmux_status xlnx_tsmux_get_status(const struct
						    xlnx_tsmux * mpgmuxts)
{
	u32 status;

	status = xlnx_tsmux_read(mpgmuxts, XTSMUX_RST_CTRL);

	if (!status)
		return MPG2MUX_ERROR;

	if (status & XTSMUX_RST_CTRL_START_MASK)
		return MPG2MUX_BUSY;

	return MPG2MUX_READY;
}

static struct class *xlnx_tsmux_class;
static dev_t xlnx_tsmux_devt;
static atomic_t xlnx_tsmux_ndevs = ATOMIC_INIT(0);

static int xlnx_tsmux_open(struct inode *pin, struct file *fptr)
{
	struct xlnx_tsmux *mpgtsmux;

	mpgtsmux = container_of(pin->i_cdev, struct xlnx_tsmux, chdev);

	fptr->private_data = mpgtsmux;
	atomic_inc(&mpgtsmux->user_count);
	atomic_set(&mpgtsmux->outbuf_idx, 0);
	mpgtsmux->stcxt_node_cnt = 0;

	return 0;
}

static int xlnx_tsmux_release(struct inode *pin, struct file *fptr)
{
	struct xlnx_tsmux *mpgtsmux = (struct xlnx_tsmux *)fptr->private_data;

	if (!mpgtsmux)
		return -EIO;

	return 0;
}

/* TODO: Optimize buf alloc, dealloc API's to accommodate src, dst, strmtbl */
static int xlnx_tsmux_ioctl_srcbuf_dealloc(struct xlnx_tsmux *mpgmuxts)
{
	unsigned int i;

	for (i = 0; i < mpgmuxts->num_inbuf; i++) {
		if (!mpgmuxts->src_kaddrs[i] || !mpgmuxts->srcbuf_addrs[i])
			break;
		dma_free_coherent(mpgmuxts->dev, mpgmuxts->srcbuf_size,
				  mpgmuxts->src_kaddrs[i],
				  mpgmuxts->srcbuf_addrs[i]);
		mpgmuxts->src_kaddrs[i] = NULL;
	}

	return 0;
}

static int xlnx_tsmux_ioctl_srcbuf_alloc(struct xlnx_tsmux *mpgmuxts,
					 void __user *arg)
{
	int ret;
	unsigned int i;
	struct strc_bufs_info buf_data;

	ret = copy_from_user(&buf_data, arg, sizeof(struct strc_bufs_info));
	if (ret < 0) {
		dev_dbg(mpgmuxts->dev, "Failed to read input buffer info\n");
		return ret;
	}

	if (buf_data.num_buf > XTSMUX_MAXIN_PLSTRM) {
		dev_dbg(mpgmuxts->dev, "Excessive input payload. supported %d",
			XTSMUX_MAXIN_PLSTRM);
		return -EINVAL;
	}

	mpgmuxts->num_inbuf = buf_data.num_buf;
	mpgmuxts->srcbuf_size = buf_data.buf_size;
	/* buf_size & num_buf boundary conditions are handled in application
	 * and initial version of driver tested with 32-bit addressing only
	 */
	for (i = 0; i < mpgmuxts->num_inbuf; i++) {
		mpgmuxts->src_kaddrs[i] =
			dma_zalloc_coherent(mpgmuxts->dev,
					    mpgmuxts->srcbuf_size,
					    &mpgmuxts->srcbuf_addrs[i],
					    GFP_KERNEL | GFP_DMA32);
		if (!mpgmuxts->src_kaddrs[i]) {
			dev_dbg(mpgmuxts->dev, "dma alloc fail %d buffer", i);
			goto exit_free;
		}
	}

	return 0;

exit_free:
	xlnx_tsmux_ioctl_srcbuf_dealloc(mpgmuxts);

	return -ENOMEM;
}

static int xlnx_tsmux_ioctl_dstbuf_dealloc(struct xlnx_tsmux *mpgmuxts)
{
	unsigned int i;

	for (i = 0; i < mpgmuxts->num_outbuf; i++) {
		if (!mpgmuxts->dst_kaddrs[i] || !mpgmuxts->dstbuf_addrs[i])
			break;
		dma_free_coherent(mpgmuxts->dev, mpgmuxts->dstbuf_size,
				  mpgmuxts->dst_kaddrs[i],
				  mpgmuxts->dstbuf_addrs[i]);
		mpgmuxts->dst_kaddrs[i] = NULL;
	}

	return 0;
}

static int xlnx_tsmux_ioctl_dstbuf_alloc(struct xlnx_tsmux *mpgmuxts,
					 void __user *arg)
{
	int ret;
	unsigned int i;
	struct strc_bufs_info buf_data;

	ret = copy_from_user(&buf_data, arg, sizeof(struct strc_bufs_info));
	if (ret < 0) {
		dev_dbg(mpgmuxts->dev, "%s: Failed to read output buffer info",
			__func__);
		return ret;
	}

	if (buf_data.num_buf > XTSMUX_MAXOUT_PLSTRM) {
		dev_dbg(mpgmuxts->dev, "Excessive output payload supported %d",
			XTSMUX_MAXOUT_PLSTRM);
		return -EINVAL;
	}

	mpgmuxts->num_outbuf = buf_data.num_buf;
	mpgmuxts->dstbuf_size = buf_data.buf_size;
	/* buf_size & num_buf boundary conditions are handled in application*/
	for (i = 0; i < mpgmuxts->num_outbuf; i++) {
		mpgmuxts->dst_kaddrs[i] =
			dma_zalloc_coherent(mpgmuxts->dev,
					    mpgmuxts->dstbuf_size,
					    &mpgmuxts->dstbuf_addrs[i],
					    GFP_KERNEL | GFP_DMA32);
		if (!mpgmuxts->dst_kaddrs[i]) {
			dev_dbg(mpgmuxts->dev, "dmamem alloc fail for %d", i);
			goto exit_free;
		}
	}

	return 0;

exit_free:
	xlnx_tsmux_ioctl_dstbuf_dealloc(mpgmuxts);

	return -ENOMEM;
}

static int xlnx_tsmux_ioctl_strmtbl_dealloc(struct xlnx_tsmux *mpgmuxts)
{
	u32 buf_size;

	buf_size = sizeof(struct stream_info) * mpgmuxts->num_strmnodes;
	if (!mpgmuxts->strmtbl_kaddrs || !mpgmuxts->strmtbl_addrs)
		return 0;

	dma_free_coherent(mpgmuxts->dev, buf_size, mpgmuxts->strmtbl_kaddrs,
			  mpgmuxts->strmtbl_addrs);
	mpgmuxts->strmtbl_kaddrs = NULL;

	if (!mpgmuxts->intn_strmtbl_kaddrs || !mpgmuxts->intn_strmtbl_addrs)
		return 0;
	dma_free_coherent(mpgmuxts->dev, buf_size,
			  mpgmuxts->intn_strmtbl_kaddrs,
			  mpgmuxts->intn_strmtbl_addrs);
	mpgmuxts->intn_strmtbl_kaddrs = NULL;

	return 0;
}

static int xlnx_tsmux_ioctl_strmtbl_alloc(struct xlnx_tsmux *mpgmuxts,
					  void __user *arg)
{
	int ret, buf_size;
	u16 num_nodes;

	ret = copy_from_user(&num_nodes, arg, sizeof(u16));
	if (ret < 0) {
		dev_dbg(mpgmuxts->dev, "Failed to read streamid table info");
		return ret;
	}
	mpgmuxts->num_strmnodes = num_nodes;
	buf_size = sizeof(struct stream_info) * mpgmuxts->num_strmnodes;

	mpgmuxts->strmtbl_kaddrs =
		dma_zalloc_coherent(mpgmuxts->dev,
				    buf_size, &mpgmuxts->strmtbl_addrs,
				    GFP_KERNEL | GFP_DMA32);
	if (!mpgmuxts->strmtbl_kaddrs) {
		dev_dbg(mpgmuxts->dev, "dmamem alloc fail for strm table");
		return -ENOMEM;
	}

	/* Allocating memory for internal streamid table */
	mpgmuxts->intn_strmtbl_kaddrs =
		dma_zalloc_coherent(mpgmuxts->dev,
				    buf_size, &mpgmuxts->intn_strmtbl_addrs,
				    GFP_KERNEL | GFP_DMA32);

	if (!mpgmuxts->intn_strmtbl_kaddrs) {
		dev_dbg(mpgmuxts->dev, "dmamem alloc fail for intr strm table");
		goto exist_free;
	}

	return 0;
exist_free:
	xlnx_tsmux_ioctl_strmtbl_dealloc(mpgmuxts);

	return -ENOMEM;
}

/**
 * xlnx_tsmux_update_intstrm_tbl - updates stream id table
 * @mpgmuxts: pointer to the device structure
 *
 * This function updates the stream id table
 *
 * Return: 0 on success and error value on failure.
 *
 */
static int xlnx_tsmux_update_intstrm_tbl(struct xlnx_tsmux *mpgmuxts)
{
	struct stream_info *cptr, *intn_cptr;
	int i, j;

	cptr = (struct stream_info *)mpgmuxts->strmtbl_kaddrs;

	if (!cptr->usageflag)
		return 0;

	for (i = 0; i < mpgmuxts->num_strmnodes && cptr->usageflag;
	     i++, cptr++) {
		intn_cptr = (struct stream_info *)mpgmuxts->intn_strmtbl_kaddrs;
		/* Adding to table */
		if (cptr->strmtbl_update == ADD_TO_TBL) {
			for (j = 0; j < mpgmuxts->num_strmnodes;
			     j++, intn_cptr++) {
				if (!intn_cptr->usageflag) {
					intn_cptr->pid = cptr->pid;
					intn_cptr->continuity_counter = 0;
					intn_cptr->usageflag = 1;
					cptr->usageflag = 0;
					break;
				}
			}
			if (j == mpgmuxts->num_strmnodes)
				return -EIO;
		} else if (cptr->strmtbl_update == DEL_FR_TBL) {
			/* deleting from table */
			for (j = 0; j < mpgmuxts->num_strmnodes; j++,
			     intn_cptr++) {
				if (intn_cptr->usageflag) {
					if (intn_cptr->pid == cptr->pid) {
						intn_cptr->pid = 0;
						intn_cptr->continuity_counter = 0;
						intn_cptr->usageflag = 0;
						cptr->usageflag = 0;
						break;
					}
				}
			}
			if (j == mpgmuxts->num_strmnodes)
				return -EIO;
		} else {
			return -EIO;
		}
	}

	return 0;
}

static int xlnx_tsmux_update_strminfo_table(struct xlnx_tsmux *mpgmuxts,
					    struct strc_strminfo new_strm_info)
{
	u32 i = 0;
	struct stream_info *cptr;

	cptr = (struct stream_info *)mpgmuxts->strmtbl_kaddrs;

	/* Finding free memory block and writing input data into the block*/
	for (i = 0; i < mpgmuxts->num_strmnodes; i++, cptr++) {
		if (!cptr->usageflag) {
			cptr->pid = new_strm_info.pid;
			cptr->continuity_counter = 0;
			cptr->usageflag = XTSMUX_STRMBL_BUSY;
			cptr->strmtbl_update = new_strm_info.strmtbl_ctxt;
			break;
		}
	}
	if (i == mpgmuxts->num_strmnodes)
		return -EIO;

	return 0;
}

static int xlnx_tsmux_ioctl_update_strmtbl(struct xlnx_tsmux *mpgmuxts,
					   void __user *arg)
{
	int ret;
	struct strc_strminfo new_strm_info;

	ret = copy_from_user(&new_strm_info, arg, sizeof(struct strc_strminfo));
	if (ret < 0) {
		dev_dbg(mpgmuxts->dev, "Reading strmInfo failed");
		return ret;
	}

	return xlnx_tsmux_update_strminfo_table(mpgmuxts, new_strm_info);
}

static int xlnx_tsmux_enqueue_stream_context(struct xlnx_tsmux *mpgmuxts,
					     struct
					     stream_context_in * stream_data)
{
	struct stream_context_node *new_strm_node, *prev_strm_node;
	void *kaddr_strm_node;
	dma_addr_t strm_phy_addr;
	unsigned long flags;
	u32 i;

	kaddr_strm_node = dma_pool_alloc(mpgmuxts->strm_ctx_pool,
					 GFP_KERNEL | GFP_DMA32,
					 &strm_phy_addr);

	new_strm_node = (struct stream_context_node *)kaddr_strm_node;
	if (!new_strm_node)
		return -ENOMEM;

	/* update the stream context node */
	wmb();
	new_strm_node->element.command = stream_data->command;
	new_strm_node->element.is_pcr_stream = stream_data->is_pcr_stream;
	new_strm_node->element.stream_id = stream_data->stream_id;
	new_strm_node->element.extended_stream_id =
				stream_data->extended_stream_id;
	new_strm_node->element.pid = stream_data->pid;
	new_strm_node->element.size_data_in = stream_data->size_data_in;
	new_strm_node->element.pts = stream_data->pts;
	new_strm_node->element.dts = stream_data->dts;
	new_strm_node->element.insert_pcr = stream_data->insert_pcr;
	new_strm_node->element.pcr_base = stream_data->pcr_base;
	new_strm_node->element.pcr_extension = stream_data->pcr_extension;

	/* Check for external dma buffer */
	if (!stream_data->is_dmabuf) {
		new_strm_node->element.in_buf_pointer =
			mpgmuxts->srcbuf_addrs[stream_data->srcbuf_id];
		new_strm_node->element.dmabuf_id = 0;
	} else {
		for (i = 0; i < XTSMUX_MAXIN_STRM; i++) {
			/* Serching dma buf info based on srcbuf_id */
			if (stream_data->srcbuf_id ==
					mpgmuxts->src_dmabufintl[i].dmabuf_fd) {
				new_strm_node->element.in_buf_pointer =
					mpgmuxts->src_dmabufintl[i].dmabuf_addr;
				new_strm_node->element.dmabuf_id =
					mpgmuxts->src_dmabufintl[i].buf_id;
				break;
			}
		}

		/* No dma buf found with srcbuf_id*/
		if (i == XTSMUX_MAXIN_STRM) {
			dev_err(mpgmuxts->dev, "No DMA buffer with %d",
				stream_data->srcbuf_id);
			return -ENOMEM;
		}
	}

	new_strm_node->strm_phy_addr = (u64)strm_phy_addr;
	new_strm_node->node_number = mpgmuxts->stcxt_node_cnt + 1;
	mpgmuxts->stcxt_node_cnt++;
	new_strm_node->node_status = UPDATED_BY_DRIVER;
	new_strm_node->error_code = NO_ERROR;
	new_strm_node->tail_pointer = 0;

	spin_lock_irqsave(&mpgmuxts->lock, flags);
	/* If it is not first stream in stream node linked list find
	 * physical address of current node and add to last node in list
	 */
	if (!list_empty_careful(&mpgmuxts->strm_node)) {
		prev_strm_node = list_last_entry(&mpgmuxts->strm_node,
						 struct stream_context_node,
						 node);
		prev_strm_node->tail_pointer = new_strm_node->strm_phy_addr;
	}
	/* update the list and stream count */
	wmb();
	list_add_tail(&new_strm_node->node, &mpgmuxts->strm_node);
	atomic_inc(&mpgmuxts->stream_count);
	spin_unlock_irqrestore(&mpgmuxts->lock, flags);

	return 0;
}

static int xlnx_tsmux_set_stream_desc(struct xlnx_tsmux *mpgmuxts,
				      void __user *arg)
{
	struct stream_context_in *stream_data;
	int ret = 0;

	stream_data = kzalloc(sizeof(*stream_data), GFP_KERNEL);
	if (!stream_data)
		return -ENOMEM;

	ret = copy_from_user(stream_data, arg,
			     sizeof(struct stream_context_in));
	if (ret) {
		dev_err(mpgmuxts->dev, "Failed to copy stream data from user");
		goto error_free;
	}

	ret = xlnx_tsmux_enqueue_stream_context(mpgmuxts, stream_data);

error_free:
	kfree(stream_data);

	return ret;
}

static int xlnx_tsmux_ioctl_set_stream_context(struct xlnx_tsmux *mpgmuxts,
					       void __user *arg)
{
	int ret;

	ret = xlnx_tsmux_set_stream_desc(mpgmuxts, arg);
	if (ret < 0) {
		dev_err(mpgmuxts->dev, "Setting stream descripter failed");
		return ret;
	}

	return 0;
}

static enum xlnx_tsmux_status xlnx_tsmux_get_device_status(struct xlnx_tsmux *
							   mpgmuxts)
{
	enum xlnx_tsmux_status ip_status;

	ip_status = xlnx_tsmux_get_status(mpgmuxts);

	if (ip_status == MPG2MUX_ERROR) {
		dev_err(mpgmuxts->dev, "Failed to get device status");
		return -EACCES;
	}

	if (ip_status == MPG2MUX_BUSY)
		return -EBUSY;

	return MPG2MUX_READY;
}

static int xlnx_tsmux_ioctl_start(struct xlnx_tsmux *mpgmuxts)
{
	enum xlnx_tsmux_status ip_stat;
	int cnt, ret;

	/* get IP status */
	ip_stat = xlnx_tsmux_get_device_status(mpgmuxts);
	if (ip_stat != MPG2MUX_READY) {
		dev_err(mpgmuxts->dev, "device is busy");
		return ip_stat;
	}

	if (list_empty(&mpgmuxts->mux_node) ||
	    list_empty(&mpgmuxts->strm_node)) {
		dev_err(mpgmuxts->dev, "No stream or mux to start device");
		return -EIO;
	}

	cnt = atomic_read(&mpgmuxts->stream_count);
	atomic_set(&mpgmuxts->intn_stream_count, cnt);

	/* update streamid table */
	ret = xlnx_tsmux_update_intstrm_tbl(mpgmuxts);

	if (ret < 0) {
		dev_err(mpgmuxts->dev, "Update streamid intn table failed\n");
		return ret;
	}
	return xlnx_tsmux_start_muxer(mpgmuxts);
}

static void xlnx_tsmux_free_dmalloc(struct xlnx_tsmux *mpgmuxts)
{
	dma_pool_destroy(mpgmuxts->strm_ctx_pool);
	dma_pool_destroy(mpgmuxts->mux_ctx_pool);
}

static int xlnx_tsmux_ioctl_stop(struct xlnx_tsmux *mpgmuxts)
{
	enum xlnx_tsmux_status ip_stat;
	unsigned long flags;

	ip_stat = xlnx_tsmux_get_device_status(mpgmuxts);
	if (ip_stat != MPG2MUX_READY) {
		dev_err(mpgmuxts->dev, "device is busy");
		return ip_stat;
	}

	/* Free all driver allocated memory and reset linked list
	 * Reset IP registers
	 */
	xlnx_tsmux_free_dmalloc(mpgmuxts);
	spin_lock_irqsave(&mpgmuxts->lock, flags);
	INIT_LIST_HEAD(&mpgmuxts->strm_node);
	INIT_LIST_HEAD(&mpgmuxts->mux_node);
	spin_unlock_irqrestore(&mpgmuxts->lock, flags);
	xlnx_tsmux_stop_muxer(mpgmuxts);

	return 0;
}

static int xlnx_tsmux_ioctl_get_status(struct xlnx_tsmux *mpgmuxts,
				       void __user *arg)
{
	int ret;
	enum xlnx_tsmux_status ip_stat;

	ip_stat = xlnx_tsmux_get_device_status(mpgmuxts);

	ret = copy_to_user(arg, (void *)&ip_stat,
			   (unsigned long)(sizeof(enum xlnx_tsmux_status)));
	if (ret) {
		dev_err(mpgmuxts->dev, "Unable to copy device status to user");
		return -EACCES;
	}

	return 0;
}

static int xlnx_tsmux_ioctl_get_outbufinfo(struct xlnx_tsmux *mpgmuxts,
					   void __user *arg)
{
	int ret;
	int out_index;
	struct out_buffer out_info;

	out_info.buf_write = mpgmuxts->outbuf_written;
	mpgmuxts->outbuf_written = 0;
	out_index = atomic_read(&mpgmuxts->outbuf_idx);
	if (out_index)
		out_info.buf_id = 0;
	else
		out_info.buf_id = 1;

	ret = copy_to_user(arg, (void *)&out_info,
			   (unsigned long)(sizeof(struct out_buffer)));
	if (ret) {
		dev_err(mpgmuxts->dev, "Unable to copy outbuf info");
		return -EACCES;
	}

	return 0;
}

static int xlnx_tsmux_enqueue_mux_context(struct xlnx_tsmux *mpgmuxts,
					  struct muxer_context_in *mux_data)
{
	struct muxer_context *new_mux_node;
	u32 out_index;
	void *kaddr_mux_node;
	dma_addr_t mux_phy_addr;
	unsigned long flags;
	s32 i;

	kaddr_mux_node = dma_pool_alloc(mpgmuxts->mux_ctx_pool,
					GFP_KERNEL | GFP_DMA32,
					&mux_phy_addr);

	new_mux_node = (struct muxer_context *)kaddr_mux_node;
	if (!new_mux_node)
		return -EAGAIN;

	new_mux_node->node_status = UPDATED_BY_DRIVER;
	new_mux_node->mux_phy_addr = (u64)mux_phy_addr;

	/* Check for external dma buffer */
	if (!mux_data->is_dmabuf) {
		out_index = 0;
		new_mux_node->dst_buf_start_addr =
			(u64)mpgmuxts->dstbuf_addrs[out_index];
		new_mux_node->dst_buf_size = mpgmuxts->dstbuf_size;
		if (out_index)
			atomic_set(&mpgmuxts->outbuf_idx, 0);
		else
			atomic_set(&mpgmuxts->outbuf_idx, 1);
	} else {
		for (i = 0; i < XTSMUX_MAXOUT_STRM; i++) {
			if (mux_data->dstbuf_id ==
			   mpgmuxts->dst_dmabufintl[i].dmabuf_fd) {
				new_mux_node->dst_buf_start_addr =
					mpgmuxts->dst_dmabufintl[i].dmabuf_addr;
				break;
			}
		}
		if (i == XTSMUX_MAXOUT_STRM) {
			dev_err(mpgmuxts->dev, "No DMA buffer with %d",
				mux_data->dstbuf_id);
			return -ENOMEM;
		}
		new_mux_node->dst_buf_size = mux_data->dmabuf_size;
	}
	new_mux_node->error_code = MUXER_NO_ERROR;

	spin_lock_irqsave(&mpgmuxts->lock, flags);
	list_add_tail(&new_mux_node->node, &mpgmuxts->mux_node);
	spin_unlock_irqrestore(&mpgmuxts->lock, flags);

	return 0;
}

static int xlnx_tsmux_set_mux_desc(struct xlnx_tsmux *mpgmuxts,
				   void __user *arg)
{
	struct muxer_context_in *mux_data;
	int ret = 0;

	mux_data = kzalloc(sizeof(*mux_data), GFP_KERNEL);
	if (!mux_data)
		return -ENOMEM;

	ret = copy_from_user(mux_data, arg,
			     sizeof(struct muxer_context_in));
	if (ret) {
		dev_err(mpgmuxts->dev, "failed to copy muxer data from user");
		goto kmem_free;
	}

	return xlnx_tsmux_enqueue_mux_context(mpgmuxts, mux_data);

kmem_free:
	kfree(mux_data);

	return ret;
}

static int xlnx_tsmux_ioctl_set_mux_context(struct xlnx_tsmux *mpgmuxts,
					    void __user *arg)
{
	int ret;

	ret = xlnx_tsmux_set_mux_desc(mpgmuxts, arg);
	if (ret < 0)
		dev_dbg(mpgmuxts->dev, "Setting mux context failed");

	return ret;
}

static int xlnx_tsmux_ioctl_verify_dmabuf(struct xlnx_tsmux *mpgmuxts,
					  void __user *arg)
{
	struct dma_buf *dbuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct xlnx_tsmux_dmabuf_info *dbuf_info;
	s32 i;
	int ret = 0;

	dbuf_info = kzalloc(sizeof(*dbuf_info), GFP_KERNEL);
	if (!dbuf_info)
		return -ENOMEM;

	ret = copy_from_user(dbuf_info, arg,
			     sizeof(struct xlnx_tsmux_dmabuf_info));
	if (ret) {
		dev_err(mpgmuxts->dev, "Failed to copy from user");
		goto dmak_free;
	}
	if (dbuf_info->dir != DMA_TO_MPG2MUX &&
	    dbuf_info->dir != DMA_FROM_MPG2MUX) {
		dev_err(mpgmuxts->dev, "Incorrect DMABUF direction %d",
			dbuf_info->dir);
		ret = -EINVAL;
		goto dmak_free;
	}
	dbuf = dma_buf_get(dbuf_info->buf_fd);
	if (IS_ERR(dbuf)) {
		dev_err(mpgmuxts->dev, "dma_buf_get fail fd %d direction %d",
			dbuf_info->buf_fd, dbuf_info->dir);
		ret = PTR_ERR(dbuf);
		goto dmak_free;
	}
	attach = dma_buf_attach(dbuf, mpgmuxts->dev);
	if (IS_ERR(attach)) {
		dev_err(mpgmuxts->dev, "dma_buf_attach fail fd %d dir %d",
			dbuf_info->buf_fd, dbuf_info->dir);
		ret = PTR_ERR(attach);
		goto err_dmabuf_put;
	}
	sgt = dma_buf_map_attachment(attach,
				     (enum dma_data_direction)(dbuf_info->dir));
	if (IS_ERR(sgt)) {
		dev_err(mpgmuxts->dev, "dma_buf_map_attach fail fd %d dir %d",
			dbuf_info->buf_fd, dbuf_info->dir);
		ret = PTR_ERR(sgt);
		goto err_dmabuf_detach;
	}

	if (sgt->nents > 1) {
		ret = -EIO;
		dev_dbg(mpgmuxts->dev, "Not contig nents %d fd %d direction %d",
			sgt->nents, dbuf_info->buf_fd, dbuf_info->dir);
		goto err_dmabuf_unmap_attachment;
	}
	dev_dbg(mpgmuxts->dev, "dmabuf %s is physically contiguous",
		(dbuf_info->dir ==
		 DMA_TO_MPG2MUX ? "Source" : "Destination"));

	if (dbuf_info->dir == DMA_TO_MPG2MUX) {
		for (i = 0; i < XTSMUX_MAXIN_STRM; i++) {
			if (!mpgmuxts->src_dmabufintl[i].buf_id) {
				mpgmuxts->src_dmabufintl[i].dbuf = dbuf;
				mpgmuxts->src_dmabufintl[i].attach = attach;
				mpgmuxts->src_dmabufintl[i].sgt = sgt;
				mpgmuxts->src_dmabufintl[i].dmabuf_addr =
						sg_dma_address(sgt->sgl);
				mpgmuxts->src_dmabufintl[i].dmabuf_fd =
							dbuf_info->buf_fd;
				mpgmuxts->src_dmabufintl[i].buf_id = i + 1;
				dev_dbg(mpgmuxts->dev,
					"%s: phy-addr=0x%llx for src dmabuf=%d",
					__func__,
					mpgmuxts->src_dmabufintl[i].dmabuf_addr,
					mpgmuxts->src_dmabufintl[i].dmabuf_fd);
				break;
			}
		}
		/* External src streams more than XTSMUX_MAXIN_STRM
		 * can not be handled
		 */
		if (i == XTSMUX_MAXIN_STRM) {
			ret = -EIO;
			dev_dbg(mpgmuxts->dev, "src DMA bufs more than %d",
				XTSMUX_MAXIN_STRM);
			goto err_dmabuf_unmap_attachment;
		}
	} else {
		for (i = 0; i < XTSMUX_MAXOUT_STRM; i++) {
			if (!mpgmuxts->dst_dmabufintl[i].buf_id) {
				mpgmuxts->dst_dmabufintl[i].dbuf = dbuf;
				mpgmuxts->dst_dmabufintl[i].attach = attach;
				mpgmuxts->dst_dmabufintl[i].sgt = sgt;
				mpgmuxts->dst_dmabufintl[i].dmabuf_addr =
						sg_dma_address(sgt->sgl);
				mpgmuxts->dst_dmabufintl[i].dmabuf_fd =
						dbuf_info->buf_fd;
				mpgmuxts->dst_dmabufintl[i].buf_id = i + 1;
				dev_dbg(mpgmuxts->dev,
					"phy-addr=0x%llx for src dmabuf=%d",
					mpgmuxts->dst_dmabufintl[i].dmabuf_addr,
					mpgmuxts->dst_dmabufintl[i].dmabuf_fd);
				break;
			}
		}
		/* External dst streams more than XTSMUX_MAXOUT_STRM
		 * can not be handled
		 */
		if (i == XTSMUX_MAXOUT_STRM) {
			ret = -EIO;
			dev_dbg(mpgmuxts->dev, "dst DMA bufs more than %d",
				XTSMUX_MAXOUT_STRM);
			goto err_dmabuf_unmap_attachment;
		}
	}

	return 0;

err_dmabuf_unmap_attachment:
	dma_buf_unmap_attachment(attach, sgt,
				 (enum dma_data_direction)dbuf_info->dir);
err_dmabuf_detach:
	dma_buf_detach(dbuf, attach);
err_dmabuf_put:
	dma_buf_put(dbuf);
dmak_free:
	kfree(dbuf_info);

	return ret;
}

static long xlnx_tsmux_ioctl(struct file *fptr,
			     unsigned int cmd, unsigned long data)
{
	struct xlnx_tsmux *mpgmuxts;
	void __user *arg;
	int ret;

	mpgmuxts = fptr->private_data;
	if (!mpgmuxts)
		return -EINVAL;

	arg = (void __user *)data;
	switch (cmd) {
	case MPG2MUX_INBUFALLOC:
		ret = xlnx_tsmux_ioctl_srcbuf_alloc(mpgmuxts, arg);
		break;
	case MPG2MUX_INBUFDEALLOC:
		ret = xlnx_tsmux_ioctl_srcbuf_dealloc(mpgmuxts);
		break;
	case MPG2MUX_OUTBUFALLOC:
		ret = xlnx_tsmux_ioctl_dstbuf_alloc(mpgmuxts, arg);
		break;
	case MPG2MUX_OUTBUFDEALLOC:
		ret = xlnx_tsmux_ioctl_dstbuf_dealloc(mpgmuxts);
		break;
	case MPG2MUX_STBLALLOC:
		ret = xlnx_tsmux_ioctl_strmtbl_alloc(mpgmuxts, arg);
		break;
	case MPG2MUX_STBLDEALLOC:
		ret = xlnx_tsmux_ioctl_strmtbl_dealloc(mpgmuxts);
		break;
	case MPG2MUX_TBLUPDATE:
		ret = xlnx_tsmux_ioctl_update_strmtbl(mpgmuxts, arg);
		break;
	case MPG2MUX_SETSTRM:
		ret = xlnx_tsmux_ioctl_set_stream_context(mpgmuxts, arg);
		break;
	case MPG2MUX_START:
		ret = xlnx_tsmux_ioctl_start(mpgmuxts);
		break;
	case MPG2MUX_STOP:
		ret = xlnx_tsmux_ioctl_stop(mpgmuxts);
		break;
	case MPG2MUX_STATUS:
		ret = xlnx_tsmux_ioctl_get_status(mpgmuxts, arg);
		break;
	case MPG2MUX_GETOUTBUF:
		ret = xlnx_tsmux_ioctl_get_outbufinfo(mpgmuxts, arg);
		break;
	case MPG2MUX_SETMUX:
		ret = xlnx_tsmux_ioctl_set_mux_context(mpgmuxts, arg);
		break;
	case MPG2MUX_VDBUF:
		ret = xlnx_tsmux_ioctl_verify_dmabuf(mpgmuxts, arg);
		break;
	default:
		return -EINVAL;
	}
	if (ret < 0)
		dev_err(mpgmuxts->dev, "ioctl %d failed\n", cmd);

	return ret;
}

static int xlnx_tsmux_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct xlnx_tsmux *mpgmuxts = fp->private_data;
	int ret, buf_id;

	if (!mpgmuxts)
		return -ENODEV;

	buf_id = vma->vm_pgoff;

	if (buf_id < mpgmuxts->num_inbuf) {
		if (!mpgmuxts->srcbuf_addrs[buf_id]) {
			dev_err(mpgmuxts->dev, "Mem not allocated for src %d",
				buf_id);
			return -EINVAL;
		}
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		ret = remap_pfn_range(vma, vma->vm_start,
				      mpgmuxts->srcbuf_addrs[buf_id] >>
				      PAGE_SHIFT, vma->vm_end - vma->vm_start,
				      vma->vm_page_prot);
		if (ret) {
			dev_err(mpgmuxts->dev, "mmap fail bufid = %d", buf_id);
			return -EINVAL;
		}
	} else if (buf_id < (mpgmuxts->num_inbuf + mpgmuxts->num_outbuf)) {
		buf_id -= mpgmuxts->num_inbuf;
		if (!mpgmuxts->dstbuf_addrs[buf_id]) {
			dev_err(mpgmuxts->dev, "Mem not allocated fordst %d",
				buf_id);
			return -EINVAL;
		}
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		ret =
		remap_pfn_range(vma, vma->vm_start,
				mpgmuxts->dstbuf_addrs[buf_id] >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start, vma->vm_page_prot);
		if (ret) {
			dev_err(mpgmuxts->dev, "mmap fail buf_id = %d", buf_id);
			ret = -EINVAL;
		}
	} else {
		dev_err(mpgmuxts->dev, "Wrong buffer id -> %d buf", buf_id);
		return -EINVAL;
	}
	fp->private_data = mpgmuxts;
	return 0;
}

static __poll_t xlnx_tsmux_poll(struct file *fptr, poll_table *wait)
{
	struct xlnx_tsmux *mpgmuxts = fptr->private_data;

	poll_wait(fptr, &mpgmuxts->waitq, wait);

	if (xlnx_tsmux_read(mpgmuxts, XTSMUX_LAST_NODE_PROCESSED))
		return POLLIN | POLLPRI;

	return 0;
}

static const struct file_operations mpg2mux_fops = {
	.open = xlnx_tsmux_open,
	.release = xlnx_tsmux_release,
	.unlocked_ioctl = xlnx_tsmux_ioctl,
	.mmap = xlnx_tsmux_mmap,
	.poll = xlnx_tsmux_poll,
};

static void xlnx_tsmux_free_dmabufintl(struct xlnx_tsmux_dmabufintl
				       *intl_dmabuf, u16 dmabuf_id,
				       enum xlnx_tsmux_dma_dir dir)
{
	unsigned int i = dmabuf_id - 1;

	if (intl_dmabuf[i].dmabuf_fd) {
		dma_buf_unmap_attachment(intl_dmabuf[i].attach,
					 intl_dmabuf[i].sgt,
					 (enum dma_data_direction)dir);
		dma_buf_detach(intl_dmabuf[i].dbuf, intl_dmabuf[i].attach);
		dma_buf_put(intl_dmabuf[i].dbuf);
		intl_dmabuf[i].dmabuf_fd = 0;
		intl_dmabuf[i].buf_id = 0;
	}
}

static int xlnx_tsmux_update_complete(struct xlnx_tsmux *mpgmuxts)
{
	struct stream_context_node *tstrm_node;
	struct muxer_context *temp_mux;
	u32 num_strm_node, i;
	u32 num_strms;
	unsigned long flags;

	num_strm_node = xlnx_tsmux_read(mpgmuxts, XTSMUX_LAST_NODE_PROCESSED);
	if (num_strm_node == 0)
		return -1;

	/* Removing completed stream nodes from the list  */
	spin_lock_irqsave(&mpgmuxts->lock, flags);
	num_strms = atomic_read(&mpgmuxts->intn_stream_count);
	for (i = 0; i < num_strms; i++) {
		tstrm_node =
			list_first_entry(&mpgmuxts->strm_node,
					 struct stream_context_node, node);
		list_del(&tstrm_node->node);
		atomic_dec(&mpgmuxts->stream_count);
		if (tstrm_node->element.dmabuf_id)
			xlnx_tsmux_free_dmabufintl
				(mpgmuxts->src_dmabufintl,
				 tstrm_node->element.dmabuf_id,
				 DMA_TO_MPG2MUX);
		if (tstrm_node->node_number == num_strm_node) {
			dma_pool_free(mpgmuxts->strm_ctx_pool, tstrm_node,
				      tstrm_node->strm_phy_addr);
			break;
		}
	}

	/* Removing completed mux nodes from the list  */
	temp_mux = list_first_entry(&mpgmuxts->mux_node, struct muxer_context,
				    node);
	mpgmuxts->outbuf_written = temp_mux->dst_buf_written;

	list_del(&temp_mux->node);
	spin_unlock_irqrestore(&mpgmuxts->lock, flags);

	return 0;
}

static irqreturn_t xlnx_tsmux_intr_handler(int irq, void *ctx)
{
	u32 status;
	struct xlnx_tsmux *mpgmuxts = (struct xlnx_tsmux *)ctx;

	status = xlnx_tsmux_read(mpgmuxts, XTSMUX_ISR_STAT);
	status &= XTSMUX_IER_ENABLE_MASK;

	if (status) {
		xlnx_tsmux_write(mpgmuxts, XTSMUX_ISR_STAT, status);
		xlnx_tsmux_update_complete(mpgmuxts);
		if (mpgmuxts->outbuf_written)
			wake_up_interruptible(&mpgmuxts->waitq);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int xlnx_tsmux_probe(struct platform_device *pdev)
{
	struct xlnx_tsmux *mpgmuxts;
	struct device *dev = &pdev->dev;
	struct device *dev_crt;
	struct resource *dev_resrc;
	int ret = -1;
	unsigned long flags;

	/* DRIVER_MAX_DEV is to limit the number of instances, but
	 * Initial version is tested with single instance only.
	 * TODO: replace atomic_read with ida_simple_get
	 */
	if (atomic_read(&xlnx_tsmux_ndevs) >= DRIVER_MAX_DEV) {
		dev_err(&pdev->dev, "Limit of %d number of device is reached",
			DRIVER_MAX_DEV);
		return -EIO;
	}

	mpgmuxts = devm_kzalloc(&pdev->dev, sizeof(struct xlnx_tsmux),
				GFP_KERNEL);
	if (!mpgmuxts)
		return -ENOMEM;
	mpgmuxts->dev = &pdev->dev;
	dev_resrc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mpgmuxts->iomem = devm_ioremap_resource(mpgmuxts->dev, dev_resrc);
	if (IS_ERR(mpgmuxts->iomem))
		return PTR_ERR(mpgmuxts->iomem);

	mpgmuxts->irq = irq_of_parse_and_map(mpgmuxts->dev->of_node, 0);
	if (!mpgmuxts->irq) {
		dev_err(mpgmuxts->dev, "Unable to get IRQ");
		return -EINVAL;
	}

	mpgmuxts->ap_clk = devm_clk_get(dev, "ap_clk");
	if (IS_ERR(mpgmuxts->ap_clk)) {
		ret = PTR_ERR(mpgmuxts->ap_clk);
		dev_err(dev, "failed to get ap clk %d\n", ret);
		goto cdev_err;
	}
	ret = clk_prepare_enable(mpgmuxts->ap_clk);
	if (ret) {
		dev_err(dev, "failed to enable ap clk %d\n", ret);
		goto err_disable_ap_clk;
	}

	/* Initializing variables used in Muxer */
	spin_lock_irqsave(&mpgmuxts->lock, flags);
	INIT_LIST_HEAD(&mpgmuxts->strm_node);
	INIT_LIST_HEAD(&mpgmuxts->mux_node);
	spin_unlock_irqrestore(&mpgmuxts->lock, flags);
	mpgmuxts->strm_ctx_pool = dma_pool_create("strcxt_pool", mpgmuxts->dev,
						  XTSMUX_POOL_SIZE,
						  XTSMUX_POOL_ALIGN,
						  XTSMUX_POOL_SIZE *
						  XTSMUX_MAXIN_TLSTRM);
	if (!mpgmuxts->strm_ctx_pool) {
		dev_err(mpgmuxts->dev, "Allocation fail for strm ctx pool");
		return -ENOMEM;
	}

	mpgmuxts->mux_ctx_pool = dma_pool_create("muxcxt_pool", mpgmuxts->dev,
						 XTSMUX_POOL_SIZE,
						 XTSMUX_POOL_SIZE,
						 XTSMUX_POOL_SIZE *
						 XTSMUX_MAXIN_TLSTRM);

	if (!mpgmuxts->mux_ctx_pool) {
		dev_err(mpgmuxts->dev, "Allocation fail for mux ctx pool");
		goto mux_err;
	}

	init_waitqueue_head(&mpgmuxts->waitq);

	ret = devm_request_irq(mpgmuxts->dev, mpgmuxts->irq,
			       xlnx_tsmux_intr_handler, IRQF_SHARED,
			       DRIVER_NAME, mpgmuxts);

	if (ret < 0) {
		dev_err(mpgmuxts->dev, "Unable to register IRQ");
		goto mux_err;
	}

	cdev_init(&mpgmuxts->chdev, &mpg2mux_fops);
	mpgmuxts->chdev.owner = THIS_MODULE;
	mpgmuxts->id = atomic_read(&xlnx_tsmux_ndevs);
	ret = cdev_add(&mpgmuxts->chdev, MKDEV(MAJOR(xlnx_tsmux_devt),
					       mpgmuxts->id), 1);

	if (ret < 0) {
		dev_err(mpgmuxts->dev, "cdev_add failed");
		goto cadd_err;
	}

	dev_crt = device_create(xlnx_tsmux_class, mpgmuxts->dev,
				MKDEV(MAJOR(xlnx_tsmux_devt), mpgmuxts->id),
				mpgmuxts, "mpgmuxts%d", mpgmuxts->id);

	if (IS_ERR(dev_crt)) {
		ret = PTR_ERR(dev_crt);
		dev_err(mpgmuxts->dev, "Unable to create device");
		goto cdev_err;
	}

	dev_info(mpgmuxts->dev,
		 "Xilinx mpeg2 TS muxer device probe completed");

	atomic_inc(&xlnx_tsmux_ndevs);

	return 0;

err_disable_ap_clk:
	clk_disable_unprepare(mpgmuxts->ap_clk);
cdev_err:
	cdev_del(&mpgmuxts->chdev);
	device_destroy(xlnx_tsmux_class, MKDEV(MAJOR(xlnx_tsmux_devt),
					       mpgmuxts->id));
cadd_err:
	dma_pool_destroy(mpgmuxts->mux_ctx_pool);
mux_err:
	dma_pool_destroy(mpgmuxts->strm_ctx_pool);

	return ret;
}

static int xlnx_tsmux_remove(struct platform_device *pdev)
{
	struct xlnx_tsmux *mpgmuxts;

	mpgmuxts = platform_get_drvdata(pdev);
	if (!mpgmuxts || !xlnx_tsmux_class)
		return -EIO;
	dma_pool_destroy(mpgmuxts->mux_ctx_pool);
	dma_pool_destroy(mpgmuxts->strm_ctx_pool);

	device_destroy(xlnx_tsmux_class, MKDEV(MAJOR(xlnx_tsmux_devt),
					       mpgmuxts->id));
	cdev_del(&mpgmuxts->chdev);
	atomic_dec(&xlnx_tsmux_ndevs);
	clk_disable_unprepare(mpgmuxts->ap_clk);

	return 0;
}

static const struct of_device_id xlnx_tsmux_of_match[] = {
	{ .compatible = "xlnx,tsmux-1.0", },
	{ }
};

static struct platform_driver xlnx_tsmux_driver = {
	.probe = xlnx_tsmux_probe,
	.remove = xlnx_tsmux_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = xlnx_tsmux_of_match,
	},
};

static int __init xlnx_tsmux_mod_init(void)
{
	int err;

	xlnx_tsmux_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(xlnx_tsmux_class)) {
		pr_err("%s : Unable to create driver class", __func__);
		return PTR_ERR(xlnx_tsmux_class);
	}

	err = alloc_chrdev_region(&xlnx_tsmux_devt, 0, DRIVER_MAX_DEV,
				  DRIVER_NAME);
	if (err < 0) {
		pr_err("%s : Unable to get major number", __func__);
		goto err_class;
	}

	err = platform_driver_register(&xlnx_tsmux_driver);
	if (err < 0) {
		pr_err("%s : Unable to register %s driver", __func__,
		       DRIVER_NAME);
		goto err_driver;
	}

	return 0;

err_driver:
	unregister_chrdev_region(xlnx_tsmux_devt, DRIVER_MAX_DEV);
err_class:
	class_destroy(xlnx_tsmux_class);

	return err;
}

static void __exit xlnx_tsmux_mod_exit(void)
{
	platform_driver_unregister(&xlnx_tsmux_driver);
	unregister_chrdev_region(xlnx_tsmux_devt, DRIVER_MAX_DEV);
	class_destroy(xlnx_tsmux_class);
	xlnx_tsmux_class = NULL;
}

module_init(xlnx_tsmux_mod_init);
module_exit(xlnx_tsmux_mod_exit);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx mpeg2 transport stream muxer IP driver");
MODULE_LICENSE("GPL v2");

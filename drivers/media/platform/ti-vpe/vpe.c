/*
 * TI VPE mem2mem driver, based on the virtual v4l2-mem2mem example driver
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * Based on the virtual v4l2-mem2mem example device
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "vpdma.h"
#include "vpe_regs.h"

#define VPE_MODULE_NAME "vpe"

/* minimum and maximum frame sizes */
#define MIN_W		128
#define MIN_H		128
#define MAX_W		1920
#define MAX_H		1080

/* required alignments */
#define S_ALIGN		0	/* multiple of 1 */
#define H_ALIGN		1	/* multiple of 2 */
#define W_ALIGN		1	/* multiple of 2 */

/* multiple of 128 bits, line stride, 16 bytes */
#define L_ALIGN		4

/* flags that indicate a format can be used for capture/output */
#define VPE_FMT_TYPE_CAPTURE	(1 << 0)
#define VPE_FMT_TYPE_OUTPUT	(1 << 1)

/* used as plane indices */
#define VPE_MAX_PLANES	2
#define VPE_LUMA	0
#define VPE_CHROMA	1

/* per m2m context info */
#define VPE_MAX_SRC_BUFS	3	/* need 3 src fields to de-interlace */

#define VPE_DEF_BUFS_PER_JOB	1	/* default one buffer per batch job */

/*
 * each VPE context can need up to 3 config desciptors, 7 input descriptors,
 * 3 output descriptors, and 10 control descriptors
 */
#define VPE_DESC_LIST_SIZE	(10 * VPDMA_DTD_DESC_SIZE +	\
					13 * VPDMA_CFD_CTD_DESC_SIZE)

#define vpe_dbg(vpedev, fmt, arg...)	\
		dev_dbg((vpedev)->v4l2_dev.dev, fmt, ##arg)
#define vpe_err(vpedev, fmt, arg...)	\
		dev_err((vpedev)->v4l2_dev.dev, fmt, ##arg)

struct vpe_us_coeffs {
	unsigned short	anchor_fid0_c0;
	unsigned short	anchor_fid0_c1;
	unsigned short	anchor_fid0_c2;
	unsigned short	anchor_fid0_c3;
	unsigned short	interp_fid0_c0;
	unsigned short	interp_fid0_c1;
	unsigned short	interp_fid0_c2;
	unsigned short	interp_fid0_c3;
	unsigned short	anchor_fid1_c0;
	unsigned short	anchor_fid1_c1;
	unsigned short	anchor_fid1_c2;
	unsigned short	anchor_fid1_c3;
	unsigned short	interp_fid1_c0;
	unsigned short	interp_fid1_c1;
	unsigned short	interp_fid1_c2;
	unsigned short	interp_fid1_c3;
};

/*
 * Default upsampler coefficients
 */
static const struct vpe_us_coeffs us_coeffs[] = {
	{
		/* Coefficients for progressive input */
		0x00C8, 0x0348, 0x0018, 0x3FD8, 0x3FB8, 0x0378, 0x00E8, 0x3FE8,
		0x00C8, 0x0348, 0x0018, 0x3FD8, 0x3FB8, 0x0378, 0x00E8, 0x3FE8,
	},
	{
		/* Coefficients for Top Field Interlaced input */
		0x0051, 0x03D5, 0x3FE3, 0x3FF7, 0x3FB5, 0x02E9, 0x018F, 0x3FD3,
		/* Coefficients for Bottom Field Interlaced input */
		0x016B, 0x0247, 0x00B1, 0x3F9D, 0x3FCF, 0x03DB, 0x005D, 0x3FF9,
	},
};

/*
 * the following registers are for configuring some of the parameters of the
 * motion and edge detection blocks inside DEI, these generally remain the same,
 * these could be passed later via userspace if some one needs to tweak these.
 */
struct vpe_dei_regs {
	unsigned long mdt_spacial_freq_thr_reg;		/* VPE_DEI_REG2 */
	unsigned long edi_config_reg;			/* VPE_DEI_REG3 */
	unsigned long edi_lut_reg0;			/* VPE_DEI_REG4 */
	unsigned long edi_lut_reg1;			/* VPE_DEI_REG5 */
	unsigned long edi_lut_reg2;			/* VPE_DEI_REG6 */
	unsigned long edi_lut_reg3;			/* VPE_DEI_REG7 */
};

/*
 * default expert DEI register values, unlikely to be modified.
 */
static const struct vpe_dei_regs dei_regs = {
	0x020C0804u,
	0x0118100Fu,
	0x08040200u,
	0x1010100Cu,
	0x10101010u,
	0x10101010u,
};

/*
 * The port_data structure contains per-port data.
 */
struct vpe_port_data {
	enum vpdma_channel channel;	/* VPDMA channel */
	u8	vb_index;		/* input frame f, f-1, f-2 index */
	u8	vb_part;		/* plane index for co-panar formats */
};

/*
 * Define indices into the port_data tables
 */
#define VPE_PORT_LUMA1_IN	0
#define VPE_PORT_CHROMA1_IN	1
#define VPE_PORT_LUMA2_IN	2
#define VPE_PORT_CHROMA2_IN	3
#define VPE_PORT_LUMA3_IN	4
#define VPE_PORT_CHROMA3_IN	5
#define VPE_PORT_MV_IN		6
#define VPE_PORT_MV_OUT		7
#define VPE_PORT_LUMA_OUT	8
#define VPE_PORT_CHROMA_OUT	9
#define VPE_PORT_RGB_OUT	10

static const struct vpe_port_data port_data[11] = {
	[VPE_PORT_LUMA1_IN] = {
		.channel	= VPE_CHAN_LUMA1_IN,
		.vb_index	= 0,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA1_IN] = {
		.channel	= VPE_CHAN_CHROMA1_IN,
		.vb_index	= 0,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_LUMA2_IN] = {
		.channel	= VPE_CHAN_LUMA2_IN,
		.vb_index	= 1,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA2_IN] = {
		.channel	= VPE_CHAN_CHROMA2_IN,
		.vb_index	= 1,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_LUMA3_IN] = {
		.channel	= VPE_CHAN_LUMA3_IN,
		.vb_index	= 2,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA3_IN] = {
		.channel	= VPE_CHAN_CHROMA3_IN,
		.vb_index	= 2,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_MV_IN] = {
		.channel	= VPE_CHAN_MV_IN,
	},
	[VPE_PORT_MV_OUT] = {
		.channel	= VPE_CHAN_MV_OUT,
	},
	[VPE_PORT_LUMA_OUT] = {
		.channel	= VPE_CHAN_LUMA_OUT,
		.vb_part	= VPE_LUMA,
	},
	[VPE_PORT_CHROMA_OUT] = {
		.channel	= VPE_CHAN_CHROMA_OUT,
		.vb_part	= VPE_CHROMA,
	},
	[VPE_PORT_RGB_OUT] = {
		.channel	= VPE_CHAN_RGB_OUT,
		.vb_part	= VPE_LUMA,
	},
};


/* driver info for each of the supported video formats */
struct vpe_fmt {
	char	*name;			/* human-readable name */
	u32	fourcc;			/* standard format identifier */
	u8	types;			/* CAPTURE and/or OUTPUT */
	u8	coplanar;		/* set for unpacked Luma and Chroma */
	/* vpdma format info for each plane */
	struct vpdma_data_format const *vpdma_fmt[VPE_MAX_PLANES];
};

static struct vpe_fmt vpe_formats[] = {
	{
		.name		= "YUV 422 co-planar",
		.fourcc		= V4L2_PIX_FMT_NV16,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 1,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_Y444],
				    &vpdma_yuv_fmts[VPDMA_DATA_FMT_C444],
				  },
	},
	{
		.name		= "YUV 420 co-planar",
		.fourcc		= V4L2_PIX_FMT_NV12,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 1,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_Y420],
				    &vpdma_yuv_fmts[VPDMA_DATA_FMT_C420],
				  },
	},
	{
		.name		= "YUYV 422 packed",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_YC422],
				  },
	},
	{
		.name		= "UYVY 422 packed",
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.types		= VPE_FMT_TYPE_CAPTURE | VPE_FMT_TYPE_OUTPUT,
		.coplanar	= 0,
		.vpdma_fmt	= { &vpdma_yuv_fmts[VPDMA_DATA_FMT_CY422],
				  },
	},
};

/*
 * per-queue, driver-specific private data.
 * there is one source queue and one destination queue for each m2m context.
 */
struct vpe_q_data {
	unsigned int		width;				/* frame width */
	unsigned int		height;				/* frame height */
	unsigned int		bytesperline[VPE_MAX_PLANES];	/* bytes per line in memory */
	enum v4l2_colorspace	colorspace;
	enum v4l2_field		field;				/* supported field value */
	unsigned int		flags;
	unsigned int		sizeimage[VPE_MAX_PLANES];	/* image size in memory */
	struct v4l2_rect	c_rect;				/* crop/compose rectangle */
	struct vpe_fmt		*fmt;				/* format info */
};

/* vpe_q_data flag bits */
#define	Q_DATA_FRAME_1D		(1 << 0)
#define	Q_DATA_MODE_TILED	(1 << 1)
#define	Q_DATA_INTERLACED	(1 << 2)

enum {
	Q_DATA_SRC = 0,
	Q_DATA_DST = 1,
};

/* find our format description corresponding to the passed v4l2_format */
static struct vpe_fmt *find_format(struct v4l2_format *f)
{
	struct vpe_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(vpe_formats); k++) {
		fmt = &vpe_formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			return fmt;
	}

	return NULL;
}

/*
 * there is one vpe_dev structure in the driver, it is shared by
 * all instances.
 */
struct vpe_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;
	struct v4l2_m2m_dev	*m2m_dev;

	atomic_t		num_instances;	/* count of driver instances */
	dma_addr_t		loaded_mmrs;	/* shadow mmrs in device */
	struct mutex		dev_mutex;
	spinlock_t		lock;

	int			irq;
	void __iomem		*base;

	struct vb2_alloc_ctx	*alloc_ctx;
	struct vpdma_data	*vpdma;		/* vpdma data handle */
};

/*
 * There is one vpe_ctx structure for each m2m context.
 */
struct vpe_ctx {
	struct v4l2_fh		fh;
	struct vpe_dev		*dev;
	struct v4l2_m2m_ctx	*m2m_ctx;
	struct v4l2_ctrl_handler hdl;

	unsigned int		field;			/* current field */
	unsigned int		sequence;		/* current frame/field seq */
	unsigned int		aborting;		/* abort after next irq */

	unsigned int		bufs_per_job;		/* input buffers per batch */
	unsigned int		bufs_completed;		/* bufs done in this batch */

	struct vpe_q_data	q_data[2];		/* src & dst queue data */
	struct vb2_buffer	*src_vbs[VPE_MAX_SRC_BUFS];
	struct vb2_buffer	*dst_vb;

	dma_addr_t		mv_buf_dma[2];		/* dma addrs of motion vector in/out bufs */
	void			*mv_buf[2];		/* virtual addrs of motion vector bufs */
	size_t			mv_buf_size;		/* current motion vector buffer size */
	struct vpdma_buf	mmr_adb;		/* shadow reg addr/data block */
	struct vpdma_desc_list	desc_list;		/* DMA descriptor list */

	bool			deinterlacing;		/* using de-interlacer */
	bool			load_mmrs;		/* have new shadow reg values */

	unsigned int		src_mv_buf_selector;
};


/*
 * M2M devices get 2 queues.
 * Return the queue given the type.
 */
static struct vpe_q_data *get_q_data(struct vpe_ctx *ctx,
				     enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return &ctx->q_data[Q_DATA_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return &ctx->q_data[Q_DATA_DST];
	default:
		BUG();
	}
	return NULL;
}

static u32 read_reg(struct vpe_dev *dev, int offset)
{
	return ioread32(dev->base + offset);
}

static void write_reg(struct vpe_dev *dev, int offset, u32 value)
{
	iowrite32(value, dev->base + offset);
}

/* register field read/write helpers */
static int get_field(u32 value, u32 mask, int shift)
{
	return (value & (mask << shift)) >> shift;
}

static int read_field_reg(struct vpe_dev *dev, int offset, u32 mask, int shift)
{
	return get_field(read_reg(dev, offset), mask, shift);
}

static void write_field(u32 *valp, u32 field, u32 mask, int shift)
{
	u32 val = *valp;

	val &= ~(mask << shift);
	val |= (field & mask) << shift;
	*valp = val;
}

static void write_field_reg(struct vpe_dev *dev, int offset, u32 field,
		u32 mask, int shift)
{
	u32 val = read_reg(dev, offset);

	write_field(&val, field, mask, shift);

	write_reg(dev, offset, val);
}

/*
 * DMA address/data block for the shadow registers
 */
struct vpe_mmr_adb {
	struct vpdma_adb_hdr	out_fmt_hdr;
	u32			out_fmt_reg[1];
	u32			out_fmt_pad[3];
	struct vpdma_adb_hdr	us1_hdr;
	u32			us1_regs[8];
	struct vpdma_adb_hdr	us2_hdr;
	u32			us2_regs[8];
	struct vpdma_adb_hdr	us3_hdr;
	u32			us3_regs[8];
	struct vpdma_adb_hdr	dei_hdr;
	u32			dei_regs[8];
	struct vpdma_adb_hdr	sc_hdr;
	u32			sc_regs[1];
	u32			sc_pad[3];
	struct vpdma_adb_hdr	csc_hdr;
	u32			csc_regs[6];
	u32			csc_pad[2];
};

#define VPE_SET_MMR_ADB_HDR(ctx, hdr, regs, offset_a)	\
	VPDMA_SET_MMR_ADB_HDR(ctx->mmr_adb, vpe_mmr_adb, hdr, regs, offset_a)
/*
 * Set the headers for all of the address/data block structures.
 */
static void init_adb_hdrs(struct vpe_ctx *ctx)
{
	VPE_SET_MMR_ADB_HDR(ctx, out_fmt_hdr, out_fmt_reg, VPE_CLK_FORMAT_SELECT);
	VPE_SET_MMR_ADB_HDR(ctx, us1_hdr, us1_regs, VPE_US1_R0);
	VPE_SET_MMR_ADB_HDR(ctx, us2_hdr, us2_regs, VPE_US2_R0);
	VPE_SET_MMR_ADB_HDR(ctx, us3_hdr, us3_regs, VPE_US3_R0);
	VPE_SET_MMR_ADB_HDR(ctx, dei_hdr, dei_regs, VPE_DEI_FRAME_SIZE);
	VPE_SET_MMR_ADB_HDR(ctx, sc_hdr, sc_regs, VPE_SC_MP_SC0);
	VPE_SET_MMR_ADB_HDR(ctx, csc_hdr, csc_regs, VPE_CSC_CSC00);
};

/*
 * Allocate or re-allocate the motion vector DMA buffers
 * There are two buffers, one for input and one for output.
 * However, the roles are reversed after each field is processed.
 * In other words, after each field is processed, the previous
 * output (dst) MV buffer becomes the new input (src) MV buffer.
 */
static int realloc_mv_buffers(struct vpe_ctx *ctx, size_t size)
{
	struct device *dev = ctx->dev->v4l2_dev.dev;

	if (ctx->mv_buf_size == size)
		return 0;

	if (ctx->mv_buf[0])
		dma_free_coherent(dev, ctx->mv_buf_size, ctx->mv_buf[0],
			ctx->mv_buf_dma[0]);

	if (ctx->mv_buf[1])
		dma_free_coherent(dev, ctx->mv_buf_size, ctx->mv_buf[1],
			ctx->mv_buf_dma[1]);

	if (size == 0)
		return 0;

	ctx->mv_buf[0] = dma_alloc_coherent(dev, size, &ctx->mv_buf_dma[0],
				GFP_KERNEL);
	if (!ctx->mv_buf[0]) {
		vpe_err(ctx->dev, "failed to allocate motion vector buffer\n");
		return -ENOMEM;
	}

	ctx->mv_buf[1] = dma_alloc_coherent(dev, size, &ctx->mv_buf_dma[1],
				GFP_KERNEL);
	if (!ctx->mv_buf[1]) {
		vpe_err(ctx->dev, "failed to allocate motion vector buffer\n");
		dma_free_coherent(dev, size, ctx->mv_buf[0],
			ctx->mv_buf_dma[0]);

		return -ENOMEM;
	}

	ctx->mv_buf_size = size;
	ctx->src_mv_buf_selector = 0;

	return 0;
}

static void free_mv_buffers(struct vpe_ctx *ctx)
{
	realloc_mv_buffers(ctx, 0);
}

/*
 * While de-interlacing, we keep the two most recent input buffers
 * around.  This function frees those two buffers when we have
 * finished processing the current stream.
 */
static void free_vbs(struct vpe_ctx *ctx)
{
	struct vpe_dev *dev = ctx->dev;
	unsigned long flags;

	if (ctx->src_vbs[2] == NULL)
		return;

	spin_lock_irqsave(&dev->lock, flags);
	if (ctx->src_vbs[2]) {
		v4l2_m2m_buf_done(ctx->src_vbs[2], VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(ctx->src_vbs[1], VB2_BUF_STATE_DONE);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
}

/*
 * Enable or disable the VPE clocks
 */
static void vpe_set_clock_enable(struct vpe_dev *dev, bool on)
{
	u32 val = 0;

	if (on)
		val = VPE_DATA_PATH_CLK_ENABLE | VPE_VPEDMA_CLK_ENABLE;
	write_reg(dev, VPE_CLK_ENABLE, val);
}

static void vpe_top_reset(struct vpe_dev *dev)
{

	write_field_reg(dev, VPE_CLK_RESET, 1, VPE_DATA_PATH_CLK_RESET_MASK,
		VPE_DATA_PATH_CLK_RESET_SHIFT);

	usleep_range(100, 150);

	write_field_reg(dev, VPE_CLK_RESET, 0, VPE_DATA_PATH_CLK_RESET_MASK,
		VPE_DATA_PATH_CLK_RESET_SHIFT);
}

static void vpe_top_vpdma_reset(struct vpe_dev *dev)
{
	write_field_reg(dev, VPE_CLK_RESET, 1, VPE_VPDMA_CLK_RESET_MASK,
		VPE_VPDMA_CLK_RESET_SHIFT);

	usleep_range(100, 150);

	write_field_reg(dev, VPE_CLK_RESET, 0, VPE_VPDMA_CLK_RESET_MASK,
		VPE_VPDMA_CLK_RESET_SHIFT);
}

/*
 * Load the correct of upsampler coefficients into the shadow MMRs
 */
static void set_us_coefficients(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	struct vpe_q_data *s_q_data = &ctx->q_data[Q_DATA_SRC];
	u32 *us1_reg = &mmr_adb->us1_regs[0];
	u32 *us2_reg = &mmr_adb->us2_regs[0];
	u32 *us3_reg = &mmr_adb->us3_regs[0];
	const unsigned short *cp, *end_cp;

	cp = &us_coeffs[0].anchor_fid0_c0;

	if (s_q_data->flags & Q_DATA_INTERLACED)	/* interlaced */
		cp += sizeof(us_coeffs[0]) / sizeof(*cp);

	end_cp = cp + sizeof(us_coeffs[0]) / sizeof(*cp);

	while (cp < end_cp) {
		write_field(us1_reg, *cp++, VPE_US_C0_MASK, VPE_US_C0_SHIFT);
		write_field(us1_reg, *cp++, VPE_US_C1_MASK, VPE_US_C1_SHIFT);
		*us2_reg++ = *us1_reg;
		*us3_reg++ = *us1_reg++;
	}
	ctx->load_mmrs = true;
}

/*
 * Set the upsampler config mode and the VPDMA line mode in the shadow MMRs.
 */
static void set_cfg_and_line_modes(struct vpe_ctx *ctx)
{
	struct vpe_fmt *fmt = ctx->q_data[Q_DATA_SRC].fmt;
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	u32 *us1_reg0 = &mmr_adb->us1_regs[0];
	u32 *us2_reg0 = &mmr_adb->us2_regs[0];
	u32 *us3_reg0 = &mmr_adb->us3_regs[0];
	int line_mode = 1;
	int cfg_mode = 1;

	/*
	 * Cfg Mode 0: YUV420 source, enable upsampler, DEI is de-interlacing.
	 * Cfg Mode 1: YUV422 source, disable upsampler, DEI is de-interlacing.
	 */

	if (fmt->fourcc == V4L2_PIX_FMT_NV12) {
		cfg_mode = 0;
		line_mode = 0;		/* double lines to line buffer */
	}

	write_field(us1_reg0, cfg_mode, VPE_US_MODE_MASK, VPE_US_MODE_SHIFT);
	write_field(us2_reg0, cfg_mode, VPE_US_MODE_MASK, VPE_US_MODE_SHIFT);
	write_field(us3_reg0, cfg_mode, VPE_US_MODE_MASK, VPE_US_MODE_SHIFT);

	/* regs for now */
	vpdma_set_line_mode(ctx->dev->vpdma, line_mode, VPE_CHAN_CHROMA1_IN);
	vpdma_set_line_mode(ctx->dev->vpdma, line_mode, VPE_CHAN_CHROMA2_IN);
	vpdma_set_line_mode(ctx->dev->vpdma, line_mode, VPE_CHAN_CHROMA3_IN);

	/* frame start for input luma */
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_LUMA1_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_LUMA2_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_LUMA3_IN);

	/* frame start for input chroma */
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_CHROMA1_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_CHROMA2_IN);
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_CHROMA3_IN);

	/* frame start for MV in client */
	vpdma_set_frame_start_event(ctx->dev->vpdma, VPDMA_FSEVENT_CHANNEL_ACTIVE,
		VPE_CHAN_MV_IN);

	ctx->load_mmrs = true;
}

/*
 * Set the shadow registers that are modified when the source
 * format changes.
 */
static void set_src_registers(struct vpe_ctx *ctx)
{
	set_us_coefficients(ctx);
}

/*
 * Set the shadow registers that are modified when the destination
 * format changes.
 */
static void set_dst_registers(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	struct vpe_fmt *fmt = ctx->q_data[Q_DATA_DST].fmt;
	u32 val = 0;

	/* select RGB path when color space conversion is supported in future */
	if (fmt->fourcc == V4L2_PIX_FMT_RGB24)
		val |= VPE_RGB_OUT_SELECT | VPE_CSC_SRC_DEI_SCALER;
	else if (fmt->fourcc == V4L2_PIX_FMT_NV16)
		val |= VPE_COLOR_SEPARATE_422;

	/* The source of CHR_DS is always the scaler, whether it's used or not */
	val |= VPE_DS_SRC_DEI_SCALER;

	if (fmt->fourcc != V4L2_PIX_FMT_NV12)
		val |= VPE_DS_BYPASS;

	mmr_adb->out_fmt_reg[0] = val;

	ctx->load_mmrs = true;
}

/*
 * Set the de-interlacer shadow register values
 */
static void set_dei_regs(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	struct vpe_q_data *s_q_data = &ctx->q_data[Q_DATA_SRC];
	unsigned int src_h = s_q_data->c_rect.height;
	unsigned int src_w = s_q_data->c_rect.width;
	u32 *dei_mmr0 = &mmr_adb->dei_regs[0];
	bool deinterlace = true;
	u32 val = 0;

	/*
	 * according to TRM, we should set DEI in progressive bypass mode when
	 * the input content is progressive, however, DEI is bypassed correctly
	 * for both progressive and interlace content in interlace bypass mode.
	 * It has been recommended not to use progressive bypass mode.
	 */
	if ((!ctx->deinterlacing && (s_q_data->flags & Q_DATA_INTERLACED)) ||
			!(s_q_data->flags & Q_DATA_INTERLACED)) {
		deinterlace = false;
		val = VPE_DEI_INTERLACE_BYPASS;
	}

	src_h = deinterlace ? src_h * 2 : src_h;

	val |= (src_h << VPE_DEI_HEIGHT_SHIFT) |
		(src_w << VPE_DEI_WIDTH_SHIFT) |
		VPE_DEI_FIELD_FLUSH;

	*dei_mmr0 = val;

	ctx->load_mmrs = true;
}

static void set_dei_shadow_registers(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	u32 *dei_mmr = &mmr_adb->dei_regs[0];
	const struct vpe_dei_regs *cur = &dei_regs;

	dei_mmr[2]  = cur->mdt_spacial_freq_thr_reg;
	dei_mmr[3]  = cur->edi_config_reg;
	dei_mmr[4]  = cur->edi_lut_reg0;
	dei_mmr[5]  = cur->edi_lut_reg1;
	dei_mmr[6]  = cur->edi_lut_reg2;
	dei_mmr[7]  = cur->edi_lut_reg3;

	ctx->load_mmrs = true;
}

static void set_csc_coeff_bypass(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	u32 *shadow_csc_reg5 = &mmr_adb->csc_regs[5];

	*shadow_csc_reg5 |= VPE_CSC_BYPASS;

	ctx->load_mmrs = true;
}

static void set_sc_regs_bypass(struct vpe_ctx *ctx)
{
	struct vpe_mmr_adb *mmr_adb = ctx->mmr_adb.addr;
	u32 *sc_reg0 = &mmr_adb->sc_regs[0];
	u32 val = 0;

	val |= VPE_SC_BYPASS;
	*sc_reg0 = val;

	ctx->load_mmrs = true;
}

/*
 * Set the shadow registers whose values are modified when either the
 * source or destination format is changed.
 */
static int set_srcdst_params(struct vpe_ctx *ctx)
{
	struct vpe_q_data *s_q_data =  &ctx->q_data[Q_DATA_SRC];
	struct vpe_q_data *d_q_data =  &ctx->q_data[Q_DATA_DST];
	size_t mv_buf_size;
	int ret;

	ctx->sequence = 0;
	ctx->field = V4L2_FIELD_TOP;

	if ((s_q_data->flags & Q_DATA_INTERLACED) &&
			!(d_q_data->flags & Q_DATA_INTERLACED)) {
		const struct vpdma_data_format *mv =
			&vpdma_misc_fmts[VPDMA_DATA_FMT_MV];

		ctx->deinterlacing = 1;
		mv_buf_size =
			(s_q_data->width * s_q_data->height * mv->depth) >> 3;
	} else {
		ctx->deinterlacing = 0;
		mv_buf_size = 0;
	}

	free_vbs(ctx);

	ret = realloc_mv_buffers(ctx, mv_buf_size);
	if (ret)
		return ret;

	set_cfg_and_line_modes(ctx);
	set_dei_regs(ctx);
	set_csc_coeff_bypass(ctx);
	set_sc_regs_bypass(ctx);

	return 0;
}

/*
 * Return the vpe_ctx structure for a given struct file
 */
static struct vpe_ctx *file2ctx(struct file *file)
{
	return container_of(file->private_data, struct vpe_ctx, fh);
}

/*
 * mem2mem callbacks
 */

/**
 * job_ready() - check whether an instance is ready to be scheduled to run
 */
static int job_ready(void *priv)
{
	struct vpe_ctx *ctx = priv;
	int needed = ctx->bufs_per_job;

	if (ctx->deinterlacing && ctx->src_vbs[2] == NULL)
		needed += 2;	/* need additional two most recent fields */

	if (v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx) < needed)
		return 0;

	return 1;
}

static void job_abort(void *priv)
{
	struct vpe_ctx *ctx = priv;

	/* Will cancel the transaction in the next interrupt handler */
	ctx->aborting = 1;
}

/*
 * Lock access to the device
 */
static void vpe_lock(void *priv)
{
	struct vpe_ctx *ctx = priv;
	struct vpe_dev *dev = ctx->dev;
	mutex_lock(&dev->dev_mutex);
}

static void vpe_unlock(void *priv)
{
	struct vpe_ctx *ctx = priv;
	struct vpe_dev *dev = ctx->dev;
	mutex_unlock(&dev->dev_mutex);
}

static void vpe_dump_regs(struct vpe_dev *dev)
{
#define DUMPREG(r) vpe_dbg(dev, "%-35s %08x\n", #r, read_reg(dev, VPE_##r))

	vpe_dbg(dev, "VPE Registers:\n");

	DUMPREG(PID);
	DUMPREG(SYSCONFIG);
	DUMPREG(INT0_STATUS0_RAW);
	DUMPREG(INT0_STATUS0);
	DUMPREG(INT0_ENABLE0);
	DUMPREG(INT0_STATUS1_RAW);
	DUMPREG(INT0_STATUS1);
	DUMPREG(INT0_ENABLE1);
	DUMPREG(CLK_ENABLE);
	DUMPREG(CLK_RESET);
	DUMPREG(CLK_FORMAT_SELECT);
	DUMPREG(CLK_RANGE_MAP);
	DUMPREG(US1_R0);
	DUMPREG(US1_R1);
	DUMPREG(US1_R2);
	DUMPREG(US1_R3);
	DUMPREG(US1_R4);
	DUMPREG(US1_R5);
	DUMPREG(US1_R6);
	DUMPREG(US1_R7);
	DUMPREG(US2_R0);
	DUMPREG(US2_R1);
	DUMPREG(US2_R2);
	DUMPREG(US2_R3);
	DUMPREG(US2_R4);
	DUMPREG(US2_R5);
	DUMPREG(US2_R6);
	DUMPREG(US2_R7);
	DUMPREG(US3_R0);
	DUMPREG(US3_R1);
	DUMPREG(US3_R2);
	DUMPREG(US3_R3);
	DUMPREG(US3_R4);
	DUMPREG(US3_R5);
	DUMPREG(US3_R6);
	DUMPREG(US3_R7);
	DUMPREG(DEI_FRAME_SIZE);
	DUMPREG(MDT_BYPASS);
	DUMPREG(MDT_SF_THRESHOLD);
	DUMPREG(EDI_CONFIG);
	DUMPREG(DEI_EDI_LUT_R0);
	DUMPREG(DEI_EDI_LUT_R1);
	DUMPREG(DEI_EDI_LUT_R2);
	DUMPREG(DEI_EDI_LUT_R3);
	DUMPREG(DEI_FMD_WINDOW_R0);
	DUMPREG(DEI_FMD_WINDOW_R1);
	DUMPREG(DEI_FMD_CONTROL_R0);
	DUMPREG(DEI_FMD_CONTROL_R1);
	DUMPREG(DEI_FMD_STATUS_R0);
	DUMPREG(DEI_FMD_STATUS_R1);
	DUMPREG(DEI_FMD_STATUS_R2);
	DUMPREG(SC_MP_SC0);
	DUMPREG(SC_MP_SC1);
	DUMPREG(SC_MP_SC2);
	DUMPREG(SC_MP_SC3);
	DUMPREG(SC_MP_SC4);
	DUMPREG(SC_MP_SC5);
	DUMPREG(SC_MP_SC6);
	DUMPREG(SC_MP_SC8);
	DUMPREG(SC_MP_SC9);
	DUMPREG(SC_MP_SC10);
	DUMPREG(SC_MP_SC11);
	DUMPREG(SC_MP_SC12);
	DUMPREG(SC_MP_SC13);
	DUMPREG(SC_MP_SC17);
	DUMPREG(SC_MP_SC18);
	DUMPREG(SC_MP_SC19);
	DUMPREG(SC_MP_SC20);
	DUMPREG(SC_MP_SC21);
	DUMPREG(SC_MP_SC22);
	DUMPREG(SC_MP_SC23);
	DUMPREG(SC_MP_SC24);
	DUMPREG(SC_MP_SC25);
	DUMPREG(CSC_CSC00);
	DUMPREG(CSC_CSC01);
	DUMPREG(CSC_CSC02);
	DUMPREG(CSC_CSC03);
	DUMPREG(CSC_CSC04);
	DUMPREG(CSC_CSC05);
#undef DUMPREG
}

static void add_out_dtd(struct vpe_ctx *ctx, int port)
{
	struct vpe_q_data *q_data = &ctx->q_data[Q_DATA_DST];
	const struct vpe_port_data *p_data = &port_data[port];
	struct vb2_buffer *vb = ctx->dst_vb;
	struct v4l2_rect *c_rect = &q_data->c_rect;
	struct vpe_fmt *fmt = q_data->fmt;
	const struct vpdma_data_format *vpdma_fmt;
	int mv_buf_selector = !ctx->src_mv_buf_selector;
	dma_addr_t dma_addr;
	u32 flags = 0;

	if (port == VPE_PORT_MV_OUT) {
		vpdma_fmt = &vpdma_misc_fmts[VPDMA_DATA_FMT_MV];
		dma_addr = ctx->mv_buf_dma[mv_buf_selector];
	} else {
		/* to incorporate interleaved formats */
		int plane = fmt->coplanar ? p_data->vb_part : 0;

		vpdma_fmt = fmt->vpdma_fmt[plane];
		dma_addr = vb2_dma_contig_plane_dma_addr(vb, plane);
		if (!dma_addr) {
			vpe_err(ctx->dev,
				"acquiring output buffer(%d) dma_addr failed\n",
				port);
			return;
		}
	}

	if (q_data->flags & Q_DATA_FRAME_1D)
		flags |= VPDMA_DATA_FRAME_1D;
	if (q_data->flags & Q_DATA_MODE_TILED)
		flags |= VPDMA_DATA_MODE_TILED;

	vpdma_add_out_dtd(&ctx->desc_list, c_rect, vpdma_fmt, dma_addr,
		p_data->channel, flags);
}

static void add_in_dtd(struct vpe_ctx *ctx, int port)
{
	struct vpe_q_data *q_data = &ctx->q_data[Q_DATA_SRC];
	const struct vpe_port_data *p_data = &port_data[port];
	struct vb2_buffer *vb = ctx->src_vbs[p_data->vb_index];
	struct v4l2_rect *c_rect = &q_data->c_rect;
	struct vpe_fmt *fmt = q_data->fmt;
	const struct vpdma_data_format *vpdma_fmt;
	int mv_buf_selector = ctx->src_mv_buf_selector;
	int field = vb->v4l2_buf.field == V4L2_FIELD_BOTTOM;
	dma_addr_t dma_addr;
	u32 flags = 0;

	if (port == VPE_PORT_MV_IN) {
		vpdma_fmt = &vpdma_misc_fmts[VPDMA_DATA_FMT_MV];
		dma_addr = ctx->mv_buf_dma[mv_buf_selector];
	} else {
		/* to incorporate interleaved formats */
		int plane = fmt->coplanar ? p_data->vb_part : 0;

		vpdma_fmt = fmt->vpdma_fmt[plane];

		dma_addr = vb2_dma_contig_plane_dma_addr(vb, plane);
		if (!dma_addr) {
			vpe_err(ctx->dev,
				"acquiring input buffer(%d) dma_addr failed\n",
				port);
			return;
		}
	}

	if (q_data->flags & Q_DATA_FRAME_1D)
		flags |= VPDMA_DATA_FRAME_1D;
	if (q_data->flags & Q_DATA_MODE_TILED)
		flags |= VPDMA_DATA_MODE_TILED;

	vpdma_add_in_dtd(&ctx->desc_list, q_data->width, q_data->height,
		c_rect, vpdma_fmt, dma_addr, p_data->channel, field, flags);
}

/*
 * Enable the expected IRQ sources
 */
static void enable_irqs(struct vpe_ctx *ctx)
{
	write_reg(ctx->dev, VPE_INT0_ENABLE0_SET, VPE_INT0_LIST0_COMPLETE);
	write_reg(ctx->dev, VPE_INT0_ENABLE1_SET, VPE_DEI_ERROR_INT |
				VPE_DS1_UV_ERROR_INT);

	vpdma_enable_list_complete_irq(ctx->dev->vpdma, 0, true);
}

static void disable_irqs(struct vpe_ctx *ctx)
{
	write_reg(ctx->dev, VPE_INT0_ENABLE0_CLR, 0xffffffff);
	write_reg(ctx->dev, VPE_INT0_ENABLE1_CLR, 0xffffffff);

	vpdma_enable_list_complete_irq(ctx->dev->vpdma, 0, false);
}

/* device_run() - prepares and starts the device
 *
 * This function is only called when both the source and destination
 * buffers are in place.
 */
static void device_run(void *priv)
{
	struct vpe_ctx *ctx = priv;
	struct vpe_q_data *d_q_data = &ctx->q_data[Q_DATA_DST];

	if (ctx->deinterlacing && ctx->src_vbs[2] == NULL) {
		ctx->src_vbs[2] = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		WARN_ON(ctx->src_vbs[2] == NULL);
		ctx->src_vbs[1] = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		WARN_ON(ctx->src_vbs[1] == NULL);
	}

	ctx->src_vbs[0] = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	WARN_ON(ctx->src_vbs[0] == NULL);
	ctx->dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	WARN_ON(ctx->dst_vb == NULL);

	/* config descriptors */
	if (ctx->dev->loaded_mmrs != ctx->mmr_adb.dma_addr || ctx->load_mmrs) {
		vpdma_map_desc_buf(ctx->dev->vpdma, &ctx->mmr_adb);
		vpdma_add_cfd_adb(&ctx->desc_list, CFD_MMR_CLIENT, &ctx->mmr_adb);
		ctx->dev->loaded_mmrs = ctx->mmr_adb.dma_addr;
		ctx->load_mmrs = false;
	}

	/* output data descriptors */
	if (ctx->deinterlacing)
		add_out_dtd(ctx, VPE_PORT_MV_OUT);

	add_out_dtd(ctx, VPE_PORT_LUMA_OUT);
	if (d_q_data->fmt->coplanar)
		add_out_dtd(ctx, VPE_PORT_CHROMA_OUT);

	/* input data descriptors */
	if (ctx->deinterlacing) {
		add_in_dtd(ctx, VPE_PORT_LUMA3_IN);
		add_in_dtd(ctx, VPE_PORT_CHROMA3_IN);

		add_in_dtd(ctx, VPE_PORT_LUMA2_IN);
		add_in_dtd(ctx, VPE_PORT_CHROMA2_IN);
	}

	add_in_dtd(ctx, VPE_PORT_LUMA1_IN);
	add_in_dtd(ctx, VPE_PORT_CHROMA1_IN);

	if (ctx->deinterlacing)
		add_in_dtd(ctx, VPE_PORT_MV_IN);

	/* sync on channel control descriptors for input ports */
	vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_LUMA1_IN);
	vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_CHROMA1_IN);

	if (ctx->deinterlacing) {
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_LUMA2_IN);
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_CHROMA2_IN);

		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_LUMA3_IN);
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list,
			VPE_CHAN_CHROMA3_IN);

		vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_MV_IN);
	}

	/* sync on channel control descriptors for output ports */
	vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_LUMA_OUT);
	if (d_q_data->fmt->coplanar)
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_CHROMA_OUT);

	if (ctx->deinterlacing)
		vpdma_add_sync_on_channel_ctd(&ctx->desc_list, VPE_CHAN_MV_OUT);

	enable_irqs(ctx);

	vpdma_map_desc_buf(ctx->dev->vpdma, &ctx->desc_list.buf);
	vpdma_submit_descs(ctx->dev->vpdma, &ctx->desc_list);
}

static void dei_error(struct vpe_ctx *ctx)
{
	dev_warn(ctx->dev->v4l2_dev.dev,
		"received DEI error interrupt\n");
}

static void ds1_uv_error(struct vpe_ctx *ctx)
{
	dev_warn(ctx->dev->v4l2_dev.dev,
		"received downsampler error interrupt\n");
}

static irqreturn_t vpe_irq(int irq_vpe, void *data)
{
	struct vpe_dev *dev = (struct vpe_dev *)data;
	struct vpe_ctx *ctx;
	struct vpe_q_data *d_q_data;
	struct vb2_buffer *s_vb, *d_vb;
	struct v4l2_buffer *s_buf, *d_buf;
	unsigned long flags;
	u32 irqst0, irqst1;

	irqst0 = read_reg(dev, VPE_INT0_STATUS0);
	if (irqst0) {
		write_reg(dev, VPE_INT0_STATUS0_CLR, irqst0);
		vpe_dbg(dev, "INT0_STATUS0 = 0x%08x\n", irqst0);
	}

	irqst1 = read_reg(dev, VPE_INT0_STATUS1);
	if (irqst1) {
		write_reg(dev, VPE_INT0_STATUS1_CLR, irqst1);
		vpe_dbg(dev, "INT0_STATUS1 = 0x%08x\n", irqst1);
	}

	ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
	if (!ctx) {
		vpe_err(dev, "instance released before end of transaction\n");
		goto handled;
	}

	if (irqst1) {
		if (irqst1 & VPE_DEI_ERROR_INT) {
			irqst1 &= ~VPE_DEI_ERROR_INT;
			dei_error(ctx);
		}
		if (irqst1 & VPE_DS1_UV_ERROR_INT) {
			irqst1 &= ~VPE_DS1_UV_ERROR_INT;
			ds1_uv_error(ctx);
		}
	}

	if (irqst0) {
		if (irqst0 & VPE_INT0_LIST0_COMPLETE)
			vpdma_clear_list_stat(ctx->dev->vpdma);

		irqst0 &= ~(VPE_INT0_LIST0_COMPLETE);
	}

	if (irqst0 | irqst1) {
		dev_warn(dev->v4l2_dev.dev, "Unexpected interrupt: "
			"INT0_STATUS0 = 0x%08x, INT0_STATUS1 = 0x%08x\n",
			irqst0, irqst1);
	}

	disable_irqs(ctx);

	vpdma_unmap_desc_buf(dev->vpdma, &ctx->desc_list.buf);
	vpdma_unmap_desc_buf(dev->vpdma, &ctx->mmr_adb);

	vpdma_reset_desc_list(&ctx->desc_list);

	 /* the previous dst mv buffer becomes the next src mv buffer */
	ctx->src_mv_buf_selector = !ctx->src_mv_buf_selector;

	if (ctx->aborting)
		goto finished;

	s_vb = ctx->src_vbs[0];
	d_vb = ctx->dst_vb;
	s_buf = &s_vb->v4l2_buf;
	d_buf = &d_vb->v4l2_buf;

	d_buf->timestamp = s_buf->timestamp;
	if (s_buf->flags & V4L2_BUF_FLAG_TIMECODE) {
		d_buf->flags |= V4L2_BUF_FLAG_TIMECODE;
		d_buf->timecode = s_buf->timecode;
	}
	d_buf->sequence = ctx->sequence;
	d_buf->field = ctx->field;

	d_q_data = &ctx->q_data[Q_DATA_DST];
	if (d_q_data->flags & Q_DATA_INTERLACED) {
		if (ctx->field == V4L2_FIELD_BOTTOM) {
			ctx->sequence++;
			ctx->field = V4L2_FIELD_TOP;
		} else {
			WARN_ON(ctx->field != V4L2_FIELD_TOP);
			ctx->field = V4L2_FIELD_BOTTOM;
		}
	} else {
		ctx->sequence++;
	}

	if (ctx->deinterlacing)
		s_vb = ctx->src_vbs[2];

	spin_lock_irqsave(&dev->lock, flags);
	v4l2_m2m_buf_done(s_vb, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(d_vb, VB2_BUF_STATE_DONE);
	spin_unlock_irqrestore(&dev->lock, flags);

	if (ctx->deinterlacing) {
		ctx->src_vbs[2] = ctx->src_vbs[1];
		ctx->src_vbs[1] = ctx->src_vbs[0];
	}

	ctx->bufs_completed++;
	if (ctx->bufs_completed < ctx->bufs_per_job) {
		device_run(ctx);
		goto handled;
	}

finished:
	vpe_dbg(ctx->dev, "finishing transaction\n");
	ctx->bufs_completed = 0;
	v4l2_m2m_job_finish(dev->m2m_dev, ctx->m2m_ctx);
handled:
	return IRQ_HANDLED;
}

/*
 * video ioctls
 */
static int vpe_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	strncpy(cap->driver, VPE_MODULE_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, VPE_MODULE_NAME, sizeof(cap->card) - 1);
	strlcpy(cap->bus_info, VPE_MODULE_NAME, sizeof(cap->bus_info));
	cap->device_caps  = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int __enum_fmt(struct v4l2_fmtdesc *f, u32 type)
{
	int i, index;
	struct vpe_fmt *fmt = NULL;

	index = 0;
	for (i = 0; i < ARRAY_SIZE(vpe_formats); ++i) {
		if (vpe_formats[i].types & type) {
			if (index == f->index) {
				fmt = &vpe_formats[i];
				break;
			}
			index++;
		}
	}

	if (!fmt)
		return -EINVAL;

	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vpe_enum_fmt(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	if (V4L2_TYPE_IS_OUTPUT(f->type))
		return __enum_fmt(f, VPE_FMT_TYPE_OUTPUT);

	return __enum_fmt(f, VPE_FMT_TYPE_CAPTURE);
}

static int vpe_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct vpe_ctx *ctx = file2ctx(file);
	struct vb2_queue *vq;
	struct vpe_q_data *q_data;
	int i;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);

	pix->width = q_data->width;
	pix->height = q_data->height;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->field = q_data->field;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		pix->colorspace = q_data->colorspace;
	} else {
		struct vpe_q_data *s_q_data;

		/* get colorspace from the source queue */
		s_q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		pix->colorspace = s_q_data->colorspace;
	}

	pix->num_planes = q_data->fmt->coplanar ? 2 : 1;

	for (i = 0; i < pix->num_planes; i++) {
		pix->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		pix->plane_fmt[i].sizeimage = q_data->sizeimage[i];
	}

	return 0;
}

static int __vpe_try_fmt(struct vpe_ctx *ctx, struct v4l2_format *f,
		       struct vpe_fmt *fmt, int type)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt;
	int i;

	if (!fmt || !(fmt->types & type)) {
		vpe_err(ctx->dev, "Fourcc format (0x%08x) invalid.\n",
			pix->pixelformat);
		return -EINVAL;
	}

	if (pix->field != V4L2_FIELD_NONE && pix->field != V4L2_FIELD_ALTERNATE)
		pix->field = V4L2_FIELD_NONE;

	v4l_bound_align_image(&pix->width, MIN_W, MAX_W, W_ALIGN,
			      &pix->height, MIN_H, MAX_H, H_ALIGN,
			      S_ALIGN);

	pix->num_planes = fmt->coplanar ? 2 : 1;
	pix->pixelformat = fmt->fourcc;

	if (type == VPE_FMT_TYPE_CAPTURE) {
		struct vpe_q_data *s_q_data;

		/* get colorspace from the source queue */
		s_q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		pix->colorspace = s_q_data->colorspace;
	} else {
		if (!pix->colorspace)
			pix->colorspace = V4L2_COLORSPACE_SMPTE240M;
	}

	for (i = 0; i < pix->num_planes; i++) {
		int depth;

		plane_fmt = &pix->plane_fmt[i];
		depth = fmt->vpdma_fmt[i]->depth;

		if (i == VPE_LUMA)
			plane_fmt->bytesperline =
					round_up((pix->width * depth) >> 3,
						1 << L_ALIGN);
		else
			plane_fmt->bytesperline = pix->width;

		plane_fmt->sizeimage =
				(pix->height * pix->width * depth) >> 3;
	}

	return 0;
}

static int vpe_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vpe_ctx *ctx = file2ctx(file);
	struct vpe_fmt *fmt = find_format(f);

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		return __vpe_try_fmt(ctx, f, fmt, VPE_FMT_TYPE_OUTPUT);
	else
		return __vpe_try_fmt(ctx, f, fmt, VPE_FMT_TYPE_CAPTURE);
}

static int __vpe_s_fmt(struct vpe_ctx *ctx, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt;
	struct vpe_q_data *q_data;
	struct vb2_queue *vq;
	int i;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		vpe_err(ctx->dev, "queue busy\n");
		return -EBUSY;
	}

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	q_data->fmt		= find_format(f);
	q_data->width		= pix->width;
	q_data->height		= pix->height;
	q_data->colorspace	= pix->colorspace;
	q_data->field		= pix->field;

	for (i = 0; i < pix->num_planes; i++) {
		plane_fmt = &pix->plane_fmt[i];

		q_data->bytesperline[i]	= plane_fmt->bytesperline;
		q_data->sizeimage[i]	= plane_fmt->sizeimage;
	}

	q_data->c_rect.left	= 0;
	q_data->c_rect.top	= 0;
	q_data->c_rect.width	= q_data->width;
	q_data->c_rect.height	= q_data->height;

	if (q_data->field == V4L2_FIELD_ALTERNATE)
		q_data->flags |= Q_DATA_INTERLACED;
	else
		q_data->flags &= ~Q_DATA_INTERLACED;

	vpe_dbg(ctx->dev, "Setting format for type %d, wxh: %dx%d, fmt: %d bpl_y %d",
		f->type, q_data->width, q_data->height, q_data->fmt->fourcc,
		q_data->bytesperline[VPE_LUMA]);
	if (q_data->fmt->coplanar)
		vpe_dbg(ctx->dev, " bpl_uv %d\n",
			q_data->bytesperline[VPE_CHROMA]);

	return 0;
}

static int vpe_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	int ret;
	struct vpe_ctx *ctx = file2ctx(file);

	ret = vpe_try_fmt(file, priv, f);
	if (ret)
		return ret;

	ret = __vpe_s_fmt(ctx, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		set_src_registers(ctx);
	else
		set_dst_registers(ctx);

	return set_srcdst_params(ctx);
}

static int vpe_reqbufs(struct file *file, void *priv,
		       struct v4l2_requestbuffers *reqbufs)
{
	struct vpe_ctx *ctx = file2ctx(file);

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int vpe_querybuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct vpe_ctx *ctx = file2ctx(file);

	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int vpe_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct vpe_ctx *ctx = file2ctx(file);

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vpe_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct vpe_ctx *ctx = file2ctx(file);

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int vpe_streamon(struct file *file, void *priv, enum v4l2_buf_type type)
{
	struct vpe_ctx *ctx = file2ctx(file);

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int vpe_streamoff(struct file *file, void *priv, enum v4l2_buf_type type)
{
	struct vpe_ctx *ctx = file2ctx(file);

	vpe_dump_regs(ctx->dev);
	vpdma_dump_regs(ctx->dev->vpdma);

	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

/*
 * defines number of buffers/frames a context can process with VPE before
 * switching to a different context. default value is 1 buffer per context
 */
#define V4L2_CID_VPE_BUFS_PER_JOB		(V4L2_CID_USER_TI_VPE_BASE + 0)

static int vpe_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpe_ctx *ctx =
		container_of(ctrl->handler, struct vpe_ctx, hdl);

	switch (ctrl->id) {
	case V4L2_CID_VPE_BUFS_PER_JOB:
		ctx->bufs_per_job = ctrl->val;
		break;

	default:
		vpe_err(ctx->dev, "Invalid control\n");
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops vpe_ctrl_ops = {
	.s_ctrl = vpe_s_ctrl,
};

static const struct v4l2_ioctl_ops vpe_ioctl_ops = {
	.vidioc_querycap	= vpe_querycap,

	.vidioc_enum_fmt_vid_cap_mplane = vpe_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= vpe_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= vpe_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= vpe_s_fmt,

	.vidioc_enum_fmt_vid_out_mplane = vpe_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane	= vpe_g_fmt,
	.vidioc_try_fmt_vid_out_mplane	= vpe_try_fmt,
	.vidioc_s_fmt_vid_out_mplane	= vpe_s_fmt,

	.vidioc_reqbufs		= vpe_reqbufs,
	.vidioc_querybuf	= vpe_querybuf,

	.vidioc_qbuf		= vpe_qbuf,
	.vidioc_dqbuf		= vpe_dqbuf,

	.vidioc_streamon	= vpe_streamon,
	.vidioc_streamoff	= vpe_streamoff,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/*
 * Queue operations
 */
static int vpe_queue_setup(struct vb2_queue *vq,
			   const struct v4l2_format *fmt,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], void *alloc_ctxs[])
{
	int i;
	struct vpe_ctx *ctx = vb2_get_drv_priv(vq);
	struct vpe_q_data *q_data;

	q_data = get_q_data(ctx, vq->type);

	*nplanes = q_data->fmt->coplanar ? 2 : 1;

	for (i = 0; i < *nplanes; i++) {
		sizes[i] = q_data->sizeimage[i];
		alloc_ctxs[i] = ctx->dev->alloc_ctx;
	}

	vpe_dbg(ctx->dev, "get %d buffer(s) of size %d", *nbuffers,
		sizes[VPE_LUMA]);
	if (q_data->fmt->coplanar)
		vpe_dbg(ctx->dev, " and %d\n", sizes[VPE_CHROMA]);

	return 0;
}

static int vpe_buf_prepare(struct vb2_buffer *vb)
{
	struct vpe_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vpe_q_data *q_data;
	int i, num_planes;

	vpe_dbg(ctx->dev, "type: %d\n", vb->vb2_queue->type);

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	num_planes = q_data->fmt->coplanar ? 2 : 1;

	for (i = 0; i < num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			vpe_err(ctx->dev,
				"data will not fit into plane (%lu < %lu)\n",
				vb2_plane_size(vb, i),
				(long) q_data->sizeimage[i]);
			return -EINVAL;
		}
	}

	for (i = 0; i < num_planes; i++)
		vb2_set_plane_payload(vb, i, q_data->sizeimage[i]);

	return 0;
}

static void vpe_buf_queue(struct vb2_buffer *vb)
{
	struct vpe_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static void vpe_wait_prepare(struct vb2_queue *q)
{
	struct vpe_ctx *ctx = vb2_get_drv_priv(q);
	vpe_unlock(ctx);
}

static void vpe_wait_finish(struct vb2_queue *q)
{
	struct vpe_ctx *ctx = vb2_get_drv_priv(q);
	vpe_lock(ctx);
}

static struct vb2_ops vpe_qops = {
	.queue_setup	 = vpe_queue_setup,
	.buf_prepare	 = vpe_buf_prepare,
	.buf_queue	 = vpe_buf_queue,
	.wait_prepare	 = vpe_wait_prepare,
	.wait_finish	 = vpe_wait_finish,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct vpe_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &vpe_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &vpe_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	return vb2_queue_init(dst_vq);
}

static const struct v4l2_ctrl_config vpe_bufs_per_job = {
	.ops = &vpe_ctrl_ops,
	.id = V4L2_CID_VPE_BUFS_PER_JOB,
	.name = "Buffers Per Transaction",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = VPE_DEF_BUFS_PER_JOB,
	.min = 1,
	.max = VIDEO_MAX_FRAME,
	.step = 1,
};

/*
 * File operations
 */
static int vpe_open(struct file *file)
{
	struct vpe_dev *dev = video_drvdata(file);
	struct vpe_ctx *ctx = NULL;
	struct vpe_q_data *s_q_data;
	struct v4l2_ctrl_handler *hdl;
	int ret;

	vpe_dbg(dev, "vpe_open\n");

	if (!dev->vpdma->ready) {
		vpe_err(dev, "vpdma firmware not loaded\n");
		return -ENODEV;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	if (mutex_lock_interruptible(&dev->dev_mutex)) {
		ret = -ERESTARTSYS;
		goto free_ctx;
	}

	ret = vpdma_create_desc_list(&ctx->desc_list, VPE_DESC_LIST_SIZE,
			VPDMA_LIST_TYPE_NORMAL);
	if (ret != 0)
		goto unlock;

	ret = vpdma_alloc_desc_buf(&ctx->mmr_adb, sizeof(struct vpe_mmr_adb));
	if (ret != 0)
		goto free_desc_list;

	init_adb_hdrs(ctx);

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;

	hdl = &ctx->hdl;
	v4l2_ctrl_handler_init(hdl, 1);
	v4l2_ctrl_new_custom(hdl, &vpe_bufs_per_job, NULL);
	if (hdl->error) {
		ret = hdl->error;
		goto exit_fh;
	}
	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	s_q_data = &ctx->q_data[Q_DATA_SRC];
	s_q_data->fmt = &vpe_formats[2];
	s_q_data->width = 1920;
	s_q_data->height = 1080;
	s_q_data->sizeimage[VPE_LUMA] = (s_q_data->width * s_q_data->height *
			s_q_data->fmt->vpdma_fmt[VPE_LUMA]->depth) >> 3;
	s_q_data->colorspace = V4L2_COLORSPACE_SMPTE240M;
	s_q_data->field = V4L2_FIELD_NONE;
	s_q_data->c_rect.left = 0;
	s_q_data->c_rect.top = 0;
	s_q_data->c_rect.width = s_q_data->width;
	s_q_data->c_rect.height = s_q_data->height;
	s_q_data->flags = 0;

	ctx->q_data[Q_DATA_DST] = *s_q_data;

	set_dei_shadow_registers(ctx);
	set_src_registers(ctx);
	set_dst_registers(ctx);
	ret = set_srcdst_params(ctx);
	if (ret)
		goto exit_fh;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &queue_init);

	if (IS_ERR(ctx->m2m_ctx)) {
		ret = PTR_ERR(ctx->m2m_ctx);
		goto exit_fh;
	}

	v4l2_fh_add(&ctx->fh);

	/*
	 * for now, just report the creation of the first instance, we can later
	 * optimize the driver to enable or disable clocks when the first
	 * instance is created or the last instance released
	 */
	if (atomic_inc_return(&dev->num_instances) == 1)
		vpe_dbg(dev, "first instance created\n");

	ctx->bufs_per_job = VPE_DEF_BUFS_PER_JOB;

	ctx->load_mmrs = true;

	vpe_dbg(dev, "created instance %p, m2m_ctx: %p\n",
		ctx, ctx->m2m_ctx);

	mutex_unlock(&dev->dev_mutex);

	return 0;
exit_fh:
	v4l2_ctrl_handler_free(hdl);
	v4l2_fh_exit(&ctx->fh);
	vpdma_free_desc_buf(&ctx->mmr_adb);
free_desc_list:
	vpdma_free_desc_list(&ctx->desc_list);
unlock:
	mutex_unlock(&dev->dev_mutex);
free_ctx:
	kfree(ctx);
	return ret;
}

static int vpe_release(struct file *file)
{
	struct vpe_dev *dev = video_drvdata(file);
	struct vpe_ctx *ctx = file2ctx(file);

	vpe_dbg(dev, "releasing instance %p\n", ctx);

	mutex_lock(&dev->dev_mutex);
	free_vbs(ctx);
	free_mv_buffers(ctx);
	vpdma_free_desc_list(&ctx->desc_list);
	vpdma_free_desc_buf(&ctx->mmr_adb);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->hdl);
	v4l2_m2m_ctx_release(ctx->m2m_ctx);

	kfree(ctx);

	/*
	 * for now, just report the release of the last instance, we can later
	 * optimize the driver to enable or disable clocks when the first
	 * instance is created or the last instance released
	 */
	if (atomic_dec_return(&dev->num_instances) == 0)
		vpe_dbg(dev, "last instance released\n");

	mutex_unlock(&dev->dev_mutex);

	return 0;
}

static unsigned int vpe_poll(struct file *file,
			     struct poll_table_struct *wait)
{
	struct vpe_ctx *ctx = file2ctx(file);
	struct vpe_dev *dev = ctx->dev;
	int ret;

	mutex_lock(&dev->dev_mutex);
	ret = v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
	mutex_unlock(&dev->dev_mutex);
	return ret;
}

static int vpe_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vpe_ctx *ctx = file2ctx(file);
	struct vpe_dev *dev = ctx->dev;
	int ret;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;
	ret = v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
	mutex_unlock(&dev->dev_mutex);
	return ret;
}

static const struct v4l2_file_operations vpe_fops = {
	.owner		= THIS_MODULE,
	.open		= vpe_open,
	.release	= vpe_release,
	.poll		= vpe_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vpe_mmap,
};

static struct video_device vpe_videodev = {
	.name		= VPE_MODULE_NAME,
	.fops		= &vpe_fops,
	.ioctl_ops	= &vpe_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
	.vfl_dir	= VFL_DIR_M2M,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run	= device_run,
	.job_ready	= job_ready,
	.job_abort	= job_abort,
	.lock		= vpe_lock,
	.unlock		= vpe_unlock,
};

static int vpe_runtime_get(struct platform_device *pdev)
{
	int r;

	dev_dbg(&pdev->dev, "vpe_runtime_get\n");

	r = pm_runtime_get_sync(&pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

static void vpe_runtime_put(struct platform_device *pdev)
{

	int r;

	dev_dbg(&pdev->dev, "vpe_runtime_put\n");

	r = pm_runtime_put_sync(&pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS);
}

static int vpe_probe(struct platform_device *pdev)
{
	struct vpe_dev *dev;
	struct video_device *vfd;
	struct resource *res;
	int ret, irq, func;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	spin_lock_init(&dev->lock);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	atomic_set(&dev->num_instances, 0);
	mutex_init(&dev->dev_mutex);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vpe_top");
	/*
	 * HACK: we get resource info from device tree in the form of a list of
	 * VPE sub blocks, the driver currently uses only the base of vpe_top
	 * for register access, the driver should be changed later to access
	 * registers based on the sub block base addresses
	 */
	dev->base = devm_ioremap(&pdev->dev, res->start, SZ_32K);
	if (IS_ERR(dev->base)) {
		ret = PTR_ERR(dev->base);
		goto v4l2_dev_unreg;
	}

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, vpe_irq, 0, VPE_MODULE_NAME,
			dev);
	if (ret)
		goto v4l2_dev_unreg;

	platform_set_drvdata(pdev, dev);

	dev->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(dev->alloc_ctx)) {
		vpe_err(dev, "Failed to alloc vb2 context\n");
		ret = PTR_ERR(dev->alloc_ctx);
		goto v4l2_dev_unreg;
	}

	dev->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		vpe_err(dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		goto rel_ctx;
	}

	pm_runtime_enable(&pdev->dev);

	ret = vpe_runtime_get(pdev);
	if (ret)
		goto rel_m2m;

	/* Perform clk enable followed by reset */
	vpe_set_clock_enable(dev, 1);

	vpe_top_reset(dev);

	func = read_field_reg(dev, VPE_PID, VPE_PID_FUNC_MASK,
		VPE_PID_FUNC_SHIFT);
	vpe_dbg(dev, "VPE PID function %x\n", func);

	vpe_top_vpdma_reset(dev);

	dev->vpdma = vpdma_create(pdev);
	if (IS_ERR(dev->vpdma))
		goto runtime_put;

	vfd = &dev->vfd;
	*vfd = vpe_videodev;
	vfd->lock = &dev->dev_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		vpe_err(dev, "Failed to register video device\n");
		goto runtime_put;
	}

	video_set_drvdata(vfd, dev);
	snprintf(vfd->name, sizeof(vfd->name), "%s", vpe_videodev.name);
	dev_info(dev->v4l2_dev.dev, "Device registered as /dev/video%d\n",
		vfd->num);

	return 0;

runtime_put:
	vpe_runtime_put(pdev);
rel_m2m:
	pm_runtime_disable(&pdev->dev);
	v4l2_m2m_release(dev->m2m_dev);
rel_ctx:
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);
v4l2_dev_unreg:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static int vpe_remove(struct platform_device *pdev)
{
	struct vpe_dev *dev =
		(struct vpe_dev *) platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " VPE_MODULE_NAME);

	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(&dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);

	vpe_set_clock_enable(dev, 0);
	vpe_runtime_put(pdev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id vpe_of_match[] = {
	{
		.compatible = "ti,vpe",
	},
	{},
};
#else
#define vpe_of_match NULL
#endif

static struct platform_driver vpe_pdrv = {
	.probe		= vpe_probe,
	.remove		= vpe_remove,
	.driver		= {
		.name	= VPE_MODULE_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = vpe_of_match,
	},
};

static void __exit vpe_exit(void)
{
	platform_driver_unregister(&vpe_pdrv);
}

static int __init vpe_init(void)
{
	return platform_driver_register(&vpe_pdrv);
}

module_init(vpe_init);
module_exit(vpe_exit);

MODULE_DESCRIPTION("TI VPE driver");
MODULE_AUTHOR("Dale Farnsworth, <dale@farnsworth.org>");
MODULE_LICENSE("GPL");

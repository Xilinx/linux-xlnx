// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Memory-to-Memory Video Multi-Scaler IP
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Author: Suresh Gupta <suresh.gupta@xilinx.com>
 *
 * Based on the virtual v4l2-mem2mem example device
 *
 * This driver adds support to control the Xilinx Video Multi
 * Scaler Controller
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "xilinx-multi-scaler-coeff.h"

/* 0x0000 : Control signals */
#define XM2MSC_AP_CTRL			0x0000
#define XM2MSC_AP_CTRL_START		BIT(0)
#define XM2MSC_AP_CTRL_DONE		BIT(1)
#define XM2MSC_AP_CTRL_IDEL		BIT(2)
#define XM2MSC_AP_CTRL_READY		BIT(3)
#define XM2MSC_AP_CTRL_AUTO_RESTART	BIT(7)

/* 0x0004 : Global Interrupt Enable Register */
#define XM2MSC_GIE			0x0004
#define XM2MSC_GIE_EN			BIT(0)

/* 0x0008 : IP Interrupt Enable Register (Read/Write) */
#define XM2MSC_IER			0x0008
#define XM2MSC_ISR			0x000c
#define XM2MSC_ISR_DONE			BIT(0)
#define XM2MSC_ISR_READY		BIT(1)

#define XM2MSC_NUM_OUTS			0x0010

#define XM2MSC_WIDTHIN			0x000
#define XM2MSC_WIDTHOUT			0x008
#define XM2MSC_HEIGHTIN			0x010
#define XM2MSC_HEIGHTOUT		0x018
#define XM2MSC_LINERATE			0x020
#define XM2MSC_PIXELRATE		0x028
#define XM2MSC_INPIXELFMT		0x030
#define XM2MSC_OUTPIXELFMT		0x038
#define XM2MSC_INSTRIDE			0x050
#define XM2MSC_OUTSTRIDE		0x058
#define XM2MSC_SRCIMGBUF0		0x060
#define XM2MSC_SRCIMGBUF1		0x070
#define XM2MSC_DSTIMGBUF0		0x090
#define XM2MSC_DSTIMGBUF1		0x0100

#define XM2MVSC_VFLTCOEFF_L	0x2000
#define XM2MVSC_VFLTCOEFF(x)	(XM2MVSC_VFLTCOEFF_L + 0x2000 * (x))
#define XM2MVSC_HFLTCOEFF_L	0x2800
#define XM2MVSC_HFLTCOEFF(x)	(XM2MVSC_HFLTCOEFF_L + 0x2000 * (x))

#define XM2MSC_CHAN_REGS_START(x)	(0x100 + 0x200 * x)

/* GPIO RESET MACROS */
#define XM2MSC_RESET_ASSERT	(0x1)
#define XM2MSC_RESET_DEASSERT	(0x0)

#define XM2MSC_MIN_CHAN		1
#define XM2MSC_MAX_CHAN		8

#define XM2MSC_MAX_WIDTH	(3840)
#define XM2MSC_MAX_HEIGHT	(2160)
#define XM2MSC_MIN_WIDTH	(64)
#define XM2MSC_MIN_HEIGHT	(64)
#define XM2MSC_STEP_PRECISION	(65536)
/* Mask definitions for Low 16 bits in a 32 bit number */
#define XM2MSC_MASK_LOW_16BITS	GENMASK(15, 0)
#define XM2MSC_BITSHIFT_16	(16)

#define XM2MSC_DRIVER_NAME	"xm2msc"

#define CHAN_ATTACHED		BIT(0)
#define CHAN_OPENED		BIT(1)

#define XM2MSC_CHAN_OUT		0
#define XM2MSC_CHAN_CAP		1

#define NUM_STREAM(_x)			\
	({ typeof(_x) (x) = (_x);	\
	min(ffz(x->out_streamed_chan),	\
	    ffz(x->cap_streamed_chan)); })

/* Xilinx Video Specific Color/Pixel Formats */
enum xm2msc_pix_fmt {
	XILINX_M2MSC_FMT_RGBX8		= 10,
	XILINX_M2MSC_FMT_YUVX8		= 11,
	XILINX_M2MSC_FMT_YUYV8		= 12,
	XILINX_M2MSC_FMT_RGBX10		= 15,
	XILINX_M2MSC_FMT_YUVX10		= 16,
	XILINX_M2MSC_FMT_Y_UV8		= 18,
	XILINX_M2MSC_FMT_Y_UV8_420	= 19,
	XILINX_M2MSC_FMT_RGB8		= 20,
	XILINX_M2MSC_FMT_YUV8		= 21,
	XILINX_M2MSC_FMT_Y_UV10		= 22,
	XILINX_M2MSC_FMT_Y_UV10_420	= 23,
	XILINX_M2MSC_FMT_Y8		= 24,
	XILINX_M2MSC_FMT_Y10		= 25,
	XILINX_M2MSC_FMT_BGRX8		= 27,
	XILINX_M2MSC_FMT_UYVY8		= 28,
	XILINX_M2MSC_FMT_BGR8		= 29,
};

/**
 * struct xm2msc_fmt - driver info for each of the supported video formats
 * @name: human-readable device tree name for this entry
 * @fourcc: standard format identifier
 * @xm2msc_fmt: Xilinx Video Specific Color/Pixel Formats
 * @num_planes: number of planes supported by format
 */
struct xm2msc_fmt {
	char *name;
	u32 fourcc;
	enum xm2msc_pix_fmt xm2msc_fmt;
	u32 num_planes;
};

static const struct xm2msc_fmt formats[] = {
	{
		.name = "xbgr8888",
		.fourcc = V4L2_PIX_FMT_BGRX32,
		.xm2msc_fmt = XILINX_M2MSC_FMT_RGBX8,
		.num_planes = 1,
	},
	{
		.name = "xvuy8888",
		.fourcc = V4L2_PIX_FMT_XVUY32,
		.xm2msc_fmt = XILINX_M2MSC_FMT_YUVX8,
		.num_planes = 1,
	},
	{
		.name = "yuyv",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.xm2msc_fmt = XILINX_M2MSC_FMT_YUYV8,
		.num_planes = 1,
	},
	{
		.name = "xbgr2101010",
		.fourcc = V4L2_PIX_FMT_XBGR30,
		.xm2msc_fmt = XILINX_M2MSC_FMT_RGBX10,
		.num_planes = 1,
	},
	{
		.name = "yuvx2101010",
		.fourcc = V4L2_PIX_FMT_XVUY10,
		.xm2msc_fmt = XILINX_M2MSC_FMT_YUVX10,
		.num_planes = 1,
	},
	{
		.name = "nv16",
		.fourcc = V4L2_PIX_FMT_NV16,
		.xm2msc_fmt = XILINX_M2MSC_FMT_Y_UV8,
		.num_planes = 2,
	},
	{
		.name = "nv12",
		.fourcc = V4L2_PIX_FMT_NV12,
		.xm2msc_fmt = XILINX_M2MSC_FMT_Y_UV8_420,
		.num_planes = 2,
	},
	{
		.name = "bgr888",
		.fourcc = V4L2_PIX_FMT_RGB24,
		.xm2msc_fmt = XILINX_M2MSC_FMT_RGB8,
		.num_planes = 1,
	},
	{
		.name = "vuy888",
		.fourcc = V4L2_PIX_FMT_VUY24,
		.xm2msc_fmt = XILINX_M2MSC_FMT_YUV8,
		.num_planes = 1,
	},
	{
		.name = "xv20",
		.fourcc = V4L2_PIX_FMT_XV20,
		.xm2msc_fmt = XILINX_M2MSC_FMT_Y_UV10,
		.num_planes = 2,
	},
	{
		.name = "xv15",
		.fourcc = V4L2_PIX_FMT_XV15,
		.xm2msc_fmt = XILINX_M2MSC_FMT_Y_UV10_420,
		.num_planes = 2,
	},
	{
		.name = "y8",
		.fourcc = V4L2_PIX_FMT_GREY,
		.xm2msc_fmt = XILINX_M2MSC_FMT_Y8,
		.num_planes = 1,
	},
	{
		.name = "y10",
		.fourcc = V4L2_PIX_FMT_Y10,
		.xm2msc_fmt = XILINX_M2MSC_FMT_Y10,
		.num_planes = 1,
	},
	{
		.name = "xrgb8888",
		.fourcc = V4L2_PIX_FMT_XBGR32,
		.xm2msc_fmt = XILINX_M2MSC_FMT_BGRX8,
		.num_planes = 1,
	},
	{
		.name = "uyvy",
		.fourcc = V4L2_PIX_FMT_UYVY,
		.xm2msc_fmt = XILINX_M2MSC_FMT_UYVY8,
		.num_planes = 1,
	},
	{
		.name = "rgb888",
		.fourcc = V4L2_PIX_FMT_BGR24,
		.xm2msc_fmt = XILINX_M2MSC_FMT_BGR8,
		.num_planes = 1,
	},
};

/**
 * struct xm2msc_q_data - Per-queue, driver-specific private data
 * There is one source queue and one destination queue for each m2m context.
 * @width: frame width
 * @height: frame height
 * @stride: bytes per lines
 * @nplanes: Current number of planes
 * @bytesperline: bytes per line per plane
 * @sizeimage: image size per plane
 * @colorspace: supported colorspace
 * @field: supported field value
 * @fmt: format info
 */
struct xm2msc_q_data {
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int nplanes;
	unsigned int bytesperline[2];
	unsigned int sizeimage[2];
	enum v4l2_colorspace colorspace;
	enum v4l2_field field;
	const struct xm2msc_fmt *fmt;
};

/**
 * struct xm2msc_chan_ctx - Scaler Channel Info, Per-Channel context
 * @regs: IO mapped base address of the Channel
 * @xm2msc_dev: Pointer to struct xm2m_msc_dev
 * @num: HW Scaling Channel number
 * @minor: Minor number of the video device
 * @status: channel status, CHAN_ATTACHED or CHAN_OPENED
 * @taps: number of hwtaps required for channel
 * @frames: number of frames processed
 * @vfd: V4L2 device
 * @fh: v4l2 file handle
 * @m2m_dev: m2m device
 * @m2m_ctx: memory to memory context structure
 * @q_data: src & dst queue data
 */
struct xm2msc_chan_ctx {
	void __iomem *regs;
	struct xm2m_msc_dev *xm2msc_dev;
	u32 num;
	u32 minor;
	u8 status;
	u32 taps;
	unsigned long frames;

	struct video_device vfd;
	struct v4l2_fh fh;
	struct v4l2_m2m_dev *m2m_dev;
	struct v4l2_m2m_ctx *m2m_ctx;

	struct xm2msc_q_data q_data[2];
};

/**
 * struct xm2m_msc_dev - Xilinx M2M Multi-scaler Device
 * @dev: pointer to struct device instance used by the driver
 * @regs: IO mapped base address of the HW/IP
 * @irq: interrupt number
 * @max_chan: maximum number of Scaling Channels
 * @max_ht: maximum number of rows in a plane
 * @max_wd: maximum number of column in a plane
 * @supported_fmt: bitmap for all supported fmts by HW
 * @dma_addr_size: Size of dma address pointer in IP (either 32 or 64)
 * @rst_gpio: reset gpio handler
 * @opened_chan: bitmap for all open channel
 * @out_streamed_chan: bitmap for all out streamed channel
 * @cap_streamed_chan: bitmap for all capture streamed channel
 * @running_chan: currently running channels
 * @device_busy: HW device is busy or not
 * @isr_wait: flag to follow the ISR complete or not
 * @isr_finished: Wait queue used to wait for IP to complete processing
 * @v4l2_dev: main struct to for V4L2 device drivers
 * @dev_mutex: lock for V4L2 device
 * @mutex: lock for channel ctx
 * @lock: lock used in IRQ
 * @xm2msc_chan: arrey of channel context
 * @hscaler_coeff: Array of filter coefficients for the Horizontal Scaler
 * @vscaler_coeff: Array of filter coefficients for the Vertical Scaler
 */
struct xm2m_msc_dev {
	struct device *dev;
	void __iomem *regs;
	int irq;
	u32 max_chan;
	u32 max_ht;
	u32 max_wd;
	u32 supported_fmt;
	u32 dma_addr_size;
	struct gpio_desc *rst_gpio;

	u32 opened_chan;
	u32 out_streamed_chan;
	u32 cap_streamed_chan;
	u32 running_chan;
	bool device_busy;
	bool isr_wait;
	wait_queue_head_t isr_finished;

	struct v4l2_device v4l2_dev;

	struct mutex dev_mutex; /*the mutex for v4l2*/
	struct mutex mutex; /*lock for bitmap reg*/
	spinlock_t lock; /*IRQ lock*/

	struct xm2msc_chan_ctx xm2msc_chan[XM2MSC_MAX_CHAN];
	short hscaler_coeff[XSCALER_MAX_PHASES][XSCALER_MAX_TAPS];
	short vscaler_coeff[XSCALER_MAX_PHASES][XSCALER_MAX_TAPS];
};

#define fh_to_chanctx(__fh) container_of(__fh, struct xm2msc_chan_ctx, fh)

static inline u32 xm2msc_readreg(const volatile void __iomem *addr)
{
	return ioread32(addr);
}

static inline void xm2msc_write64reg(volatile void __iomem *addr, u64 value)
{
	iowrite32(lower_32_bits(value), addr);
	iowrite32(upper_32_bits(value), (void __iomem *)(addr + 4));
}

static inline void xm2msc_writereg(volatile void __iomem *addr, u32 value)
{
	iowrite32(value, addr);
}

static struct xm2msc_q_data *get_q_data(struct xm2msc_chan_ctx *chan_ctx,
					enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &chan_ctx->q_data[XM2MSC_CHAN_OUT];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &chan_ctx->q_data[XM2MSC_CHAN_CAP];
	default:
		v4l2_err(&chan_ctx->xm2msc_dev->v4l2_dev,
			 "Not supported Q type %d\n", type);
	}
	return NULL;
}

static u32 find_format_index(struct v4l2_format *f)
{
	const struct xm2msc_fmt *fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		fmt = &formats[i];
		if (fmt->fourcc == f->fmt.pix_mp.pixelformat)
			break;
	}

	return i;
}

static const struct xm2msc_fmt *find_format(struct v4l2_format *f)
{
	const struct xm2msc_fmt *fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		fmt = &formats[i];
		if (fmt->fourcc == f->fmt.pix_mp.pixelformat)
			break;
	}

	if (i == ARRAY_SIZE(formats))
		return NULL;

	return &formats[i];
}

static void
xv_hscaler_load_ext_coeff(struct xm2m_msc_dev *xm2msc,
			  const short *coeff, u32 ntaps)
{
	unsigned int i, j, pad, offset;
	const u32 nphases = XSCALER_MAX_PHASES;

	/* Determine if coefficient needs padding (effective vs. max taps) */
	pad = XSCALER_MAX_TAPS - ntaps;
	offset = pad >> 1;

	memset(xm2msc->hscaler_coeff, 0, sizeof(xm2msc->hscaler_coeff));

	/* Load coefficients into scaler coefficient table */
	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps; ++j)
			xm2msc->hscaler_coeff[i][j + offset] =
						coeff[i * ntaps + j];
	}
}

static void xv_hscaler_set_coeff(struct xm2msc_chan_ctx *chan_ctx,
				 const u32 base_addr)
{
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	int val, offset, rd_indx;
	unsigned int i, j;
	u32 ntaps = chan_ctx->taps;
	const u32 nphases = XSCALER_MAX_PHASES;

	offset = (XSCALER_MAX_TAPS - ntaps) / 2;
	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps / 2; j++) {
			rd_indx = j * 2 + offset;
			val = (xm2msc->hscaler_coeff[i][rd_indx + 1] <<
			       XM2MSC_BITSHIFT_16) |
			       (xm2msc->hscaler_coeff[i][rd_indx] &
			       XM2MSC_MASK_LOW_16BITS);
			 xm2msc_writereg((xm2msc->regs + base_addr) +
				    ((i * ntaps / 2 + j) * 4), val);
		}
	}
}

static void
xv_vscaler_load_ext_coeff(struct xm2m_msc_dev *xm2msc,
			  const short *coeff, const u32 ntaps)
{
	unsigned int i, j;
	int pad, offset;
	const u32 nphases = XSCALER_MAX_PHASES;

	/* Determine if coefficient needs padding (effective vs. max taps) */
	pad = XSCALER_MAX_TAPS - ntaps;
	offset = pad ? (pad >> 1) : 0;

	/* Zero Entire Array */
	memset(xm2msc->vscaler_coeff, 0, sizeof(xm2msc->vscaler_coeff));

	/* Load User defined coefficients into scaler coefficient table */
	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps; ++j)
			xm2msc->vscaler_coeff[i][j + offset] =
						coeff[i * ntaps + j];
	}
}

static void xv_vscaler_set_coeff(struct xm2msc_chan_ctx *chan_ctx,
				 const u32 base_addr)
{
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	u32 val, i, j, offset, rd_indx;
	u32 ntaps = chan_ctx->taps;
	const u32 nphases = XSCALER_MAX_PHASES;

	offset = (XSCALER_MAX_TAPS - ntaps) / 2;

	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps / 2; j++) {
			rd_indx = j * 2 + offset;
			val = (xm2msc->vscaler_coeff[i][rd_indx + 1] <<
			       XM2MSC_BITSHIFT_16) |
			       (xm2msc->vscaler_coeff[i][rd_indx] &
			       XM2MSC_MASK_LOW_16BITS);
			xm2msc_writereg((xm2msc->regs +
				   base_addr) + ((i * ntaps / 2 + j) * 4), val);
		}
	}
}

static void xm2mvsc_initialize_coeff_banks(struct xm2msc_chan_ctx *chan_ctx)
{
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	/* Bank 0 is init as 6 tap filter for 6, 8, 10 & 12 tap filters */
	xv_hscaler_load_ext_coeff(xm2msc, &xhsc_coeff_taps6[0][0],
				  XSCALER_TAPS_6);
	xv_hscaler_set_coeff(chan_ctx, XM2MVSC_HFLTCOEFF(chan_ctx->num));
	xv_vscaler_load_ext_coeff(xm2msc, &xvsc_coeff_taps6[0][0],
				  XSCALER_TAPS_6);
	xv_vscaler_set_coeff(chan_ctx, XM2MVSC_VFLTCOEFF(chan_ctx->num));
}

static void xm2msc_set_chan_params(struct xm2msc_chan_ctx *chan_ctx,
				   enum v4l2_buf_type type)
{
	struct xm2msc_q_data *q_data = get_q_data(chan_ctx, type);
	const struct xm2msc_fmt *fmt = q_data->fmt;
	void __iomem *base = chan_ctx->regs;

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		xm2msc_writereg(base + XM2MSC_WIDTHIN, q_data->width);
		xm2msc_writereg(base + XM2MSC_HEIGHTIN, q_data->height);
		xm2msc_writereg(base + XM2MSC_INPIXELFMT, fmt->xm2msc_fmt);
		xm2msc_writereg(base + XM2MSC_INSTRIDE, q_data->stride);
	} else {
		xm2msc_writereg(base + XM2MSC_WIDTHOUT, q_data->width);
		xm2msc_writereg(base + XM2MSC_HEIGHTOUT, q_data->height);
		xm2msc_writereg(base + XM2MSC_OUTPIXELFMT, fmt->xm2msc_fmt);
		xm2msc_writereg(base + XM2MSC_OUTSTRIDE, q_data->stride);
	}
}

static void xm2msc_set_chan_com_params(struct xm2msc_chan_ctx *chan_ctx)
{
	void __iomem *base = chan_ctx->regs;
	struct xm2msc_q_data *out_q_data = &chan_ctx->q_data[XM2MSC_CHAN_OUT];
	struct xm2msc_q_data *cap_q_data = &chan_ctx->q_data[XM2MSC_CHAN_CAP];
	u32 pixel_rate;
	u32 line_rate;

	chan_ctx->taps = XSCALER_TAPS_6; /* Currently only 6 tabs supported */
	xm2mvsc_initialize_coeff_banks(chan_ctx);

	pixel_rate = (out_q_data->width * XM2MSC_STEP_PRECISION) /
		cap_q_data->width;
	line_rate = (out_q_data->height * XM2MSC_STEP_PRECISION) /
		cap_q_data->height;

	xm2msc_writereg(base + XM2MSC_PIXELRATE, pixel_rate);
	xm2msc_writereg(base + XM2MSC_LINERATE, line_rate);
}

static void xm2msc_program_allchan(struct xm2m_msc_dev *xm2msc)
{
	u32 chan;

	for (chan = 0; chan < xm2msc->running_chan; chan++) {
		struct xm2msc_chan_ctx *chan_ctx;

		chan_ctx = &xm2msc->xm2msc_chan[chan];

		xm2msc_set_chan_params(chan_ctx,
				       V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		xm2msc_set_chan_params(chan_ctx,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		xm2msc_set_chan_com_params(chan_ctx);
	}
}

static void
xm2msc_pr_q(struct device *dev, struct xm2msc_q_data *q, int chan,
	    int type, const char *fun_name)
{
	unsigned int i;
	const struct xm2msc_fmt *fmt = q->fmt;

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		dev_dbg(dev, "\n\nOUTPUT Q (%d) Context from [[ %s ]]",
			chan, fun_name);
	else
		dev_dbg(dev, "\n\nCAPTURE Q (%d) Context from [[ %s ]]",
			chan, fun_name);

	dev_dbg(dev, "width height stride clrspace field planes\n");
	dev_dbg(dev, "  %d  %d    %d     %d       %d    %d\n",
		q->width, q->height, q->stride,
		q->colorspace, q->field, q->nplanes);

	for (i = 0; i < q->nplanes; i++) {
		dev_dbg(dev, "[plane %d ] bytesperline sizeimage\n", i);
		dev_dbg(dev, "                %d        %d\n",
			q->bytesperline[i], q->sizeimage[i]);
	}

	dev_dbg(dev, "fmt_name 4cc xlnx-fmt\n");
	dev_dbg(dev, "%s %d %d\n",
		fmt->name, fmt->fourcc, fmt->xm2msc_fmt);
	dev_dbg(dev, "\n\n");
}

static void
xm2msc_pr_status(struct xm2m_msc_dev *xm2msc,
		 const char *fun_name)
{
	struct device *dev = xm2msc->dev;

	dev_dbg(dev, "Status in %s\n", fun_name);
	dev_dbg(dev, "opened_chan out_streamed_chan cap_streamed_chan\n");
	dev_dbg(dev, "0x%x           0x%x               0x%x\n",
		xm2msc->opened_chan, xm2msc->out_streamed_chan,
		xm2msc->cap_streamed_chan);
	dev_dbg(dev, "\n\n");
}

static void
xm2msc_pr_chanctx(struct xm2msc_chan_ctx *ctx, const char *fun_name)
{
	struct device *dev = ctx->xm2msc_dev->dev;

	dev_dbg(dev, "\n\n----- [[ %s ]]: Channel %d (0x%p) context -----\n",
		fun_name, ctx->num, ctx);
	dev_dbg(dev, "minor = %d, taps = %d\n", ctx->minor, ctx->taps);
	dev_dbg(dev, "reg mapped at %p\n", ctx->regs);
	dev_dbg(dev, "xm2msc \tm2m_dev \tm2m_ctx\n");
	dev_dbg(dev, "%p \t%p \t%p\n", ctx->xm2msc_dev,
		ctx->m2m_dev, ctx->m2m_ctx);

	if (ctx->status & CHAN_OPENED)
		dev_dbg(dev, "Opened ");
	if (ctx->status & CHAN_ATTACHED)
		dev_dbg(dev, "and attached");
	dev_dbg(dev, "\n");
	dev_dbg(dev, "-----------------------------------\n");
	dev_dbg(dev, "\n\n");
}

static void
xm2msc_pr_screg(struct device *dev, const volatile void __iomem *base)
{
	dev_dbg(dev, "Ctr, GIE,  IE,  IS   OUT\n");
	dev_dbg(dev, "0x%x  0x%x   0x%x  0x%x  0x%x\n",
		xm2msc_readreg(base + XM2MSC_AP_CTRL),
		xm2msc_readreg(base + XM2MSC_GIE),
		xm2msc_readreg(base + XM2MSC_IER),
		xm2msc_readreg(base + XM2MSC_ISR),
		xm2msc_readreg(base + XM2MSC_NUM_OUTS));
}

static void
xm2msc_pr_chanreg(struct device *dev, const volatile void __iomem *base)
{
	dev_dbg(dev, "WIN HIN INPIXELFMT INSTRIDE SRCB0L/H SRCB1L/H\n");
	dev_dbg(dev, "%d   %d     %d       %d      0x%x/0x%x      0x%x/0x%x\n",
		xm2msc_readreg(base + XM2MSC_WIDTHIN),
		xm2msc_readreg(base + XM2MSC_HEIGHTIN),
		xm2msc_readreg(base + XM2MSC_INPIXELFMT),
		xm2msc_readreg(base + XM2MSC_INSTRIDE),
		xm2msc_readreg(base + XM2MSC_SRCIMGBUF0),
		xm2msc_readreg(base + XM2MSC_SRCIMGBUF0 + 4),
		xm2msc_readreg(base + XM2MSC_SRCIMGBUF1),
		xm2msc_readreg(base + XM2MSC_SRCIMGBUF1 + 4));
	dev_dbg(dev, "WOUT HOUT OUTPIXELFMT OUTSTRIDE DBUF0L/H DBUF1L/H\n");
	dev_dbg(dev, "%d   %d     %d       %d      0x%x/0x%x      0x%x/0x%x\n",
		xm2msc_readreg(base + XM2MSC_WIDTHOUT),
		xm2msc_readreg(base + XM2MSC_HEIGHTOUT),
		xm2msc_readreg(base + XM2MSC_OUTPIXELFMT),
		xm2msc_readreg(base + XM2MSC_OUTSTRIDE),
		xm2msc_readreg(base + XM2MSC_DSTIMGBUF0),
		xm2msc_readreg(base + XM2MSC_DSTIMGBUF0 + 4),
		xm2msc_readreg(base + XM2MSC_DSTIMGBUF1),
		xm2msc_readreg(base + XM2MSC_DSTIMGBUF1 + 4));

	dev_dbg(dev, "LINERATE PIXELRATE\n");
	dev_dbg(dev, "0x%x     0x%x\n",
		xm2msc_readreg(base + XM2MSC_LINERATE),
		xm2msc_readreg(base + XM2MSC_PIXELRATE));
}

static void
xm2msc_pr_allchanreg(struct xm2m_msc_dev *xm2msc)
{
	unsigned int i;
	struct xm2msc_chan_ctx *chan_ctx;
	struct device *dev = xm2msc->dev;

	xm2msc_pr_screg(xm2msc->dev, xm2msc->regs);

	for (i = 0; i < xm2msc->running_chan; i++) {
		chan_ctx = &xm2msc->xm2msc_chan[i];
		dev_dbg(dev, "Regs val for channel %d\n", i);
		dev_dbg(dev, "______________________________________________\n");
		xm2msc_pr_chanreg(dev, chan_ctx->regs);
		dev_dbg(dev, "processed frames = %lu\n", chan_ctx->frames);
		dev_dbg(dev, "______________________________________________\n");
	}
}

static inline bool xm2msc_testbit(int num, u32 *addr)
{
	return (*addr & BIT(num));
}

static inline void xm2msc_setbit(int num, u32 *addr)
{
	*addr |= BIT(num);
}

static inline void xm2msc_clrbit(int num, u32 *addr)
{
	*addr &= ~BIT(num);
}

static void xm2msc_stop(struct xm2m_msc_dev *xm2msc)
{
	void __iomem *base = xm2msc->regs;
	u32 data = xm2msc_readreg(base + XM2MSC_AP_CTRL);

	data &= ~XM2MSC_AP_CTRL_START;
	xm2msc_writereg(base + XM2MSC_AP_CTRL, data);
}

static void xm2msc_start(struct xm2m_msc_dev *xm2msc)
{
	void __iomem *base = xm2msc->regs;
	u32 data = xm2msc_readreg(base + XM2MSC_AP_CTRL);

	data |= XM2MSC_AP_CTRL_START;
	xm2msc_writereg(base + XM2MSC_AP_CTRL, data);
}

static void xm2msc_set_chan(struct xm2msc_chan_ctx *ctx, bool state)
{
	mutex_lock(&ctx->xm2msc_dev->mutex);
	if (state)
		xm2msc_setbit(ctx->num, &ctx->xm2msc_dev->opened_chan);
	else
		xm2msc_clrbit(ctx->num, &ctx->xm2msc_dev->opened_chan);
	mutex_unlock(&ctx->xm2msc_dev->mutex);
}

static void
xm2msc_set_chan_stream(struct xm2msc_chan_ctx *ctx, bool state, int type)
{
	u32 *ptr;

	if (type == XM2MSC_CHAN_OUT)
		ptr = &ctx->xm2msc_dev->out_streamed_chan;
	else
		ptr = &ctx->xm2msc_dev->cap_streamed_chan;

	spin_lock(&ctx->xm2msc_dev->lock);
	if (state)
		xm2msc_setbit(ctx->num, ptr);
	else
		xm2msc_clrbit(ctx->num, ptr);

	spin_unlock(&ctx->xm2msc_dev->lock);
}

static int
xm2msc_chk_chan_stream(struct xm2msc_chan_ctx *ctx, int type)
{
	u32 *ptr;
	int ret;

	if (type == XM2MSC_CHAN_OUT)
		ptr = &ctx->xm2msc_dev->out_streamed_chan;
	else
		ptr = &ctx->xm2msc_dev->cap_streamed_chan;

	mutex_lock(&ctx->xm2msc_dev->mutex);
	ret = xm2msc_testbit(ctx->num, ptr);
	mutex_unlock(&ctx->xm2msc_dev->mutex);

	return ret;
}

static void xm2msc_set_fmt(struct xm2m_msc_dev *xm2msc, u32 index)
{
	xm2msc_setbit(index, &xm2msc->supported_fmt);
}

static int xm2msc_chk_fmt(struct xm2m_msc_dev *xm2msc, u32 index)
{
	return xm2msc_testbit(index, &xm2msc->supported_fmt);
}

static void xm2msc_reset(struct xm2m_msc_dev *xm2msc)
{
	gpiod_set_value_cansleep(xm2msc->rst_gpio, XM2MSC_RESET_ASSERT);
	gpiod_set_value_cansleep(xm2msc->rst_gpio, XM2MSC_RESET_DEASSERT);
}

/*
 * mem2mem callbacks
 */
static int xm2msc_job_ready(void *priv)
{
	struct xm2msc_chan_ctx *chan_ctx = priv;

	if ((v4l2_m2m_num_src_bufs_ready(chan_ctx->m2m_ctx) > 0) &&
	    (v4l2_m2m_num_dst_bufs_ready(chan_ctx->m2m_ctx) > 0))
		return 1;
	return 0;
}

static bool xm2msc_alljob_ready(struct xm2m_msc_dev *xm2msc)
{
	struct xm2msc_chan_ctx *chan_ctx;
	unsigned int chan;

	for (chan = 0; chan < xm2msc->running_chan; chan++) {
		chan_ctx = &xm2msc->xm2msc_chan[chan];

		if (!xm2msc_job_ready((void *)chan_ctx)) {
			dev_info(xm2msc->dev, "chan %d not ready\n",
				 chan_ctx->num);
			return false;
		}
	}

	return true;
}

static void xm2msc_chan_abort_bufs(struct xm2msc_chan_ctx *chan_ctx)
{
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	struct vb2_v4l2_buffer *dst_vb, *src_vb;

	spin_lock(&xm2msc->lock);
	dev_dbg(xm2msc->dev, "aborting all buffers\n");

	while (v4l2_m2m_num_src_bufs_ready(chan_ctx->m2m_ctx) > 0) {
		src_vb = v4l2_m2m_src_buf_remove(chan_ctx->m2m_ctx);
		v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
	}

	while (v4l2_m2m_num_dst_bufs_ready(chan_ctx->m2m_ctx) > 0) {
		dst_vb = v4l2_m2m_dst_buf_remove(chan_ctx->m2m_ctx);
		v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);
	}

	v4l2_m2m_job_finish(chan_ctx->m2m_dev, chan_ctx->m2m_ctx);
	spin_unlock(&xm2msc->lock);
}

static void xm2msc_job_abort(void *priv)
{
	struct xm2msc_chan_ctx *chan_ctx = priv;

	xm2msc_chan_abort_bufs(chan_ctx);

	/*
	 * Stream off the channel as job_abort may not always
	 * be called after streamoff
	 */
	xm2msc_set_chan_stream(chan_ctx, false, XM2MSC_CHAN_OUT);
	xm2msc_set_chan_stream(chan_ctx, false, XM2MSC_CHAN_CAP);
}

static int xm2msc_set_bufaddr(struct xm2m_msc_dev *xm2msc)
{
	unsigned int chan;
	struct xm2msc_chan_ctx *chan_ctx;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	void __iomem *base;
	dma_addr_t src_luma, dst_luma;
	dma_addr_t src_croma, dst_croma;

	if (!xm2msc_alljob_ready(xm2msc))
		return -EINVAL;

	for (chan = 0; chan < xm2msc->running_chan; chan++) {
		chan_ctx = &xm2msc->xm2msc_chan[chan];
		base = chan_ctx->regs;

		src_vb = v4l2_m2m_next_src_buf(chan_ctx->m2m_ctx);
		dst_vb = v4l2_m2m_next_dst_buf(chan_ctx->m2m_ctx);

		if (!src_vb || !dst_vb) {
			v4l2_err(&xm2msc->v4l2_dev, "buffer not found chan = %d\n",
				 chan_ctx->num);
			return -EINVAL;
		}

		src_luma = vb2_dma_contig_plane_dma_addr(&src_vb->vb2_buf, 0);
		dst_luma = vb2_dma_contig_plane_dma_addr(&dst_vb->vb2_buf, 0);

		if (chan_ctx->q_data[XM2MSC_CHAN_OUT].nplanes == 2)
			src_croma =
			    vb2_dma_contig_plane_dma_addr(&src_vb->vb2_buf, 1);
		else
			src_croma = 0;

		if (chan_ctx->q_data[XM2MSC_CHAN_CAP].nplanes == 2)
			dst_croma =
			    vb2_dma_contig_plane_dma_addr(&dst_vb->vb2_buf, 1);
		else
			dst_croma = 0;

		if (xm2msc->dma_addr_size == 64 &&
		    sizeof(dma_addr_t) == sizeof(u64)) {
			xm2msc_write64reg(base + XM2MSC_SRCIMGBUF0, src_luma);
			xm2msc_write64reg(base + XM2MSC_SRCIMGBUF1, src_croma);
			xm2msc_write64reg(base + XM2MSC_DSTIMGBUF0, dst_luma);
			xm2msc_write64reg(base + XM2MSC_DSTIMGBUF1, dst_croma);
		} else {
			xm2msc_writereg(base + XM2MSC_SRCIMGBUF0, src_luma);
			xm2msc_writereg(base + XM2MSC_SRCIMGBUF1, src_croma);
			xm2msc_writereg(base + XM2MSC_DSTIMGBUF0, dst_luma);
			xm2msc_writereg(base + XM2MSC_DSTIMGBUF1, dst_croma);
		}
	}
	return 0;
}

static void xm2msc_job_finish(struct xm2m_msc_dev *xm2msc)
{
	unsigned int chan;

	for (chan = 0; chan < xm2msc->running_chan; chan++) {
		struct xm2msc_chan_ctx *chan_ctx;

		chan_ctx = &xm2msc->xm2msc_chan[chan];
		v4l2_m2m_job_finish(chan_ctx->m2m_dev, chan_ctx->m2m_ctx);
	}
}

static void xm2msc_job_done(struct xm2m_msc_dev *xm2msc)
{
	u32 chan;

	for (chan = 0; chan < xm2msc->running_chan; chan++) {
		struct xm2msc_chan_ctx *chan_ctx;
		struct vb2_v4l2_buffer *src_vb, *dst_vb;
		unsigned long flags;

		chan_ctx = &xm2msc->xm2msc_chan[chan];

		src_vb = v4l2_m2m_src_buf_remove(chan_ctx->m2m_ctx);
		dst_vb = v4l2_m2m_dst_buf_remove(chan_ctx->m2m_ctx);

		if (src_vb && dst_vb) {
			dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
			dst_vb->timecode = src_vb->timecode;
			dst_vb->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
			dst_vb->flags |=
			    src_vb->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

			spin_lock_irqsave(&xm2msc->lock, flags);
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
			spin_unlock_irqrestore(&xm2msc->lock, flags);
		}
		chan_ctx->frames++;
	}
}

static void xm2msc_device_run(void *priv)
{
	struct xm2msc_chan_ctx *chan_ctx = priv;
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	void __iomem *base = xm2msc->regs;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&xm2msc->lock, flags);
	if (xm2msc->device_busy) {
		spin_unlock_irqrestore(&xm2msc->lock, flags);
		return;
	}
	xm2msc->device_busy = true;

	if (xm2msc->running_chan != NUM_STREAM(xm2msc)) {
		dev_dbg(xm2msc->dev, "Running chan was %d\n",
			xm2msc->running_chan);
		xm2msc->running_chan = NUM_STREAM(xm2msc);

		/* IP need reset for updating of XM2MSC_NUM_OUT */
		xm2msc_reset(xm2msc);
		xm2msc_writereg(base + XM2MSC_NUM_OUTS, xm2msc->running_chan);
		xm2msc_program_allchan(xm2msc);
	}
	spin_unlock_irqrestore(&xm2msc->lock, flags);

	dev_dbg(xm2msc->dev, "Running chan = %d\n", xm2msc->running_chan);
	if (!xm2msc->running_chan) {
		xm2msc->device_busy = false;
		return;
	}

	ret = xm2msc_set_bufaddr(xm2msc);
	if (ret) {
		/*
		 * All channel does not have buffer
		 * Currently we do not handle the removal of any Intermediate
		 * channel while streaming is going on
		 */
		if (xm2msc->out_streamed_chan || xm2msc->cap_streamed_chan)
			dev_err(xm2msc->dev,
				"Buffer not available, streaming chan 0x%x\n",
				xm2msc->cap_streamed_chan);

		xm2msc->device_busy = false;
		return;
	}

	xm2msc_writereg(base + XM2MSC_GIE, XM2MSC_GIE_EN);
	xm2msc_writereg(base + XM2MSC_IER, XM2MSC_ISR_DONE);

	xm2msc_pr_status(xm2msc, __func__);
	xm2msc_pr_screg(xm2msc->dev, base);
	xm2msc_pr_allchanreg(xm2msc);

	xm2msc_start(xm2msc);

	xm2msc->isr_wait = true;
	wait_event(xm2msc->isr_finished, !xm2msc->isr_wait);

	xm2msc_job_done(xm2msc);

	xm2msc->device_busy = false;

	if (xm2msc_alljob_ready(xm2msc))
		xm2msc_device_run(xm2msc->xm2msc_chan);

	xm2msc_job_finish(xm2msc);
}

static irqreturn_t xm2msc_isr(int irq, void *data)
{
	struct xm2m_msc_dev *xm2msc = (struct xm2m_msc_dev *)data;
	void __iomem *base = xm2msc->regs;
	u32 status;

	status = xm2msc_readreg(base + XM2MSC_ISR);
	if (!(status & XM2MSC_ISR_DONE))
		return IRQ_NONE;

	xm2msc_writereg(base + XM2MSC_ISR, status & XM2MSC_ISR_DONE);

	xm2msc_stop(xm2msc);

	xm2msc->isr_wait = false;
	wake_up(&xm2msc->isr_finished);

	return IRQ_HANDLED;
}

static int xm2msc_streamon(struct file *file, void *fh,
			   enum v4l2_buf_type type)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return v4l2_m2m_streamon(file, chan_ctx->m2m_ctx, type);
}

static int xm2msc_streamoff(struct file *file, void *fh,
			    enum v4l2_buf_type type)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);
	int ret;

	ret = v4l2_m2m_streamoff(file, chan_ctx->m2m_ctx, type);

	/* Check if any channel is still running */
	xm2msc_device_run(chan_ctx);
	return ret;
}

static int xm2msc_qbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return v4l2_m2m_qbuf(file, chan_ctx->m2m_ctx, buf);
}

static int xm2msc_dqbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return v4l2_m2m_dqbuf(file, chan_ctx->m2m_ctx, buf);
}

static int xm2msc_expbuf(struct file *file, void *fh,
			 struct v4l2_exportbuffer *eb)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return v4l2_m2m_expbuf(file, chan_ctx->m2m_ctx, eb);
}

static int xm2msc_createbufs(struct file *file, void *fh,
			     struct v4l2_create_buffers *cb)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return v4l2_m2m_create_bufs(file, chan_ctx->m2m_ctx, cb);
}

static int xm2msc_reqbufs(struct file *file, void *fh,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return v4l2_m2m_reqbufs(file, chan_ctx->m2m_ctx, reqbufs);
}

static int xm2msc_querybuf(struct file *file, void *fh,
			   struct v4l2_buffer *buf)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return v4l2_m2m_querybuf(file, chan_ctx->m2m_ctx, buf);
}

static unsigned int
xm2msc_cal_stride(unsigned int width, enum xm2msc_pix_fmt xfmt)
{
	unsigned int stride;

	/* Stride in Bytes = (Width × Bytes per Pixel); */
	/* TODO: The Width value must be a multiple of Pixels per Clock */
	switch (xfmt) {
	case XILINX_M2MSC_FMT_RGBX8:
	case XILINX_M2MSC_FMT_YUVX8:
	case XILINX_M2MSC_FMT_RGBX10:
	case XILINX_M2MSC_FMT_YUVX10:
	case XILINX_M2MSC_FMT_BGRX8:
		stride = width * 4;
		break;
	case XILINX_M2MSC_FMT_YUYV8:
	case XILINX_M2MSC_FMT_UYVY8:
		stride = width * 2;
		break;
	case XILINX_M2MSC_FMT_Y_UV8:
	case XILINX_M2MSC_FMT_Y_UV8_420:
	case XILINX_M2MSC_FMT_Y8:
		stride = width * 1;
		break;
	case XILINX_M2MSC_FMT_RGB8:
	case XILINX_M2MSC_FMT_YUV8:
	case XILINX_M2MSC_FMT_BGR8:
		stride = width * 3;
		break;
	case XILINX_M2MSC_FMT_Y_UV10:
	case XILINX_M2MSC_FMT_Y_UV10_420:
	case XILINX_M2MSC_FMT_Y10:
		/* 4 bytes per 3 pixels */
		stride = DIV_ROUND_UP(width * 4, 3);
		break;
	default:
		stride = 0;
	}

	return stride;
}

static int
vidioc_try_fmt(struct xm2msc_chan_ctx *chan_ctx, struct v4l2_format *f)
{
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct xm2msc_q_data *q_data;
	struct vb2_queue *vq;
	int index;

	if (pix->width < XM2MSC_MIN_WIDTH || pix->width > xm2msc->max_wd ||
	    pix->height < XM2MSC_MIN_HEIGHT || pix->height > xm2msc->max_ht)
		dev_dbg(xm2msc->dev,
			"Wrong input parameters %d, wxh: %dx%d.\n",
			f->type, f->fmt.pix.width, f->fmt.pix.height);
	/*
	 * V4L2 specification suggests the driver corrects the
	 * format struct if any of the dimensions is unsupported
	 */
	if (pix->height < XM2MSC_MIN_HEIGHT)
		pix->height = XM2MSC_MIN_HEIGHT;
	else if (pix->height > xm2msc->max_ht)
		pix->height = xm2msc->max_ht;

	if (pix->width < XM2MSC_MIN_WIDTH)
		pix->width = XM2MSC_MIN_WIDTH;
	else if (pix->width > xm2msc->max_wd)
		pix->width = xm2msc->max_wd;

	vq = v4l2_m2m_get_vq(chan_ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(chan_ctx, f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&xm2msc->v4l2_dev,
			 "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fmt = find_format(f);
	index = find_format_index(f);
	if (!q_data->fmt || index == ARRAY_SIZE(formats) ||
	    !xm2msc_chk_fmt(xm2msc, index)) {
		v4l2_err(&xm2msc->v4l2_dev,
			 "Couldn't set format type %d, wxh: %dx%d. ",
			 f->type, f->fmt.pix.width, f->fmt.pix.height);
		v4l2_err(&xm2msc->v4l2_dev,
			 "fmt: %d, field: %d\n",
			 f->fmt.pix.pixelformat, f->fmt.pix.field);
		return -EINVAL;
	}

	return 0;
}

static int
vidioc_s_fmt(struct xm2msc_chan_ctx *chan_ctx, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct xm2msc_q_data *q_data = get_q_data(chan_ctx, f->type);
	unsigned int i;

	q_data = get_q_data(chan_ctx, f->type);

	q_data->width = pix->width;
	q_data->height = pix->height;
	q_data->stride = xm2msc_cal_stride(pix->width,
					   q_data->fmt->xm2msc_fmt);
	q_data->colorspace = pix->colorspace;
	q_data->field = pix->field;
	q_data->nplanes = q_data->fmt->num_planes;

	for (i = 0; i < q_data->nplanes; i++) {
		q_data->bytesperline[i] = q_data->stride;
		pix->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		q_data->sizeimage[i] = q_data->stride * q_data->height;
		pix->plane_fmt[i].sizeimage = q_data->sizeimage[i];
	}

	xm2msc_pr_q(chan_ctx->xm2msc_dev->dev, q_data,
		    chan_ctx->num, f->type, __func__);

	return 0;
}

static int xm2msc_try_fmt_vid_out(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return vidioc_try_fmt(chan_ctx, f);
}

static int xm2msc_try_fmt_vid_cap(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return vidioc_try_fmt(chan_ctx, f);
}

static int xm2msc_s_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	int ret;
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	ret = xm2msc_try_fmt_vid_cap(file, fh, f);
	if (ret)
		return ret;
	return vidioc_s_fmt(chan_ctx, f);
}

static int xm2msc_s_fmt_vid_out(struct file *file, void *fh,
				struct v4l2_format *f)
{
	int ret;
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	ret = xm2msc_try_fmt_vid_out(file, fh, f);
	if (ret)
		return ret;

	return vidioc_s_fmt(chan_ctx, f);
}

static int vidioc_g_fmt(struct xm2msc_chan_ctx *chan_ctx, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct xm2msc_q_data *q_data;
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	unsigned int i;

	vq = v4l2_m2m_get_vq(chan_ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(chan_ctx, f->type);
	if (!q_data)
		return -EINVAL;

	pix->width = q_data->width;
	pix->height = q_data->height;
	pix->field = V4L2_FIELD_NONE;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->colorspace = q_data->colorspace;
	pix->num_planes = q_data->nplanes;

	for (i = 0; i < pix->num_planes; i++) {
		pix->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		pix->plane_fmt[i].sizeimage = q_data->sizeimage[i];
	}

	return 0;
}

static int xm2msc_g_fmt_vid_out(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return vidioc_g_fmt(chan_ctx, f);
}

static int xm2msc_g_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	return vidioc_g_fmt(chan_ctx, f);
}

static int enum_fmt(struct xm2m_msc_dev *xm2msc, struct v4l2_fmtdesc *f)
{
	const struct xm2msc_fmt *fmt;
	unsigned int i, enabled = 0;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (xm2msc_chk_fmt(xm2msc, i) && enabled++ == f->index)
			break;
	}

	if (i == ARRAY_SIZE(formats))
		/* Format not found */
		return -EINVAL;

	/* Format found */
	fmt = &formats[i];
	strlcpy(f->description, fmt->name,
		sizeof(f->description));
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int xm2msc_enum_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	return enum_fmt(chan_ctx->xm2msc_dev, f);
}

static int xm2msc_enum_fmt_vid_out(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(fh);

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	return enum_fmt(chan_ctx->xm2msc_dev, f);
}

static int xm2msc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
	strncpy(cap->driver, XM2MSC_DRIVER_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, XM2MSC_DRIVER_NAME, sizeof(cap->card) - 1);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", XM2MSC_DRIVER_NAME);
	/*
	 * This is only a mem-to-mem video device. The STREAMING
	 * device capability flags are left only for compatibility
	 * and are scheduled for removal.
	 */
	cap->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int xm2msc_queue_setup(struct vb2_queue *vq,
			      unsigned int *nbuffers, unsigned int *nplanes,
			      unsigned int sizes[], struct device *alloc_devs[])
{
	unsigned int i;
	struct xm2msc_chan_ctx *chan_ctx = vb2_get_drv_priv(vq);
	struct xm2msc_q_data *q_data;

	q_data = get_q_data(chan_ctx, vq->type);
	if (!q_data)
		return -EINVAL;

	*nplanes = q_data->nplanes;

	for (i = 0; i < *nplanes; i++)
		sizes[i] = q_data->sizeimage[i];

	dev_dbg(chan_ctx->xm2msc_dev->dev, "get %d buffer(s) of size %d",
		*nbuffers, sizes[0]);
	if (q_data->nplanes == 2)
		dev_dbg(chan_ctx->xm2msc_dev->dev, " and %d\n", sizes[1]);

	return 0;
}

static int xm2msc_buf_prepare(struct vb2_buffer *vb)
{
	struct xm2msc_chan_ctx *chan_ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	struct xm2msc_q_data *q_data;
	unsigned int i, num_planes;

	q_data = get_q_data(chan_ctx, vb->vb2_queue->type);
	if (!q_data)
		return -EINVAL;
	num_planes = q_data->nplanes;

	for (i = 0; i < num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			v4l2_err(&xm2msc->v4l2_dev, "data will not fit into plane ");
				   v4l2_err(&xm2msc->v4l2_dev, "(%lu < %lu)\n",
					    vb2_plane_size(vb, i),
					    (long)q_data->sizeimage[i]);
			return -EINVAL;
		}
	}

	for (i = 0; i < num_planes; i++)
		vb2_set_plane_payload(vb, i, q_data->sizeimage[i]);

	return 0;
}

static void xm2msc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct xm2msc_chan_ctx *chan_ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(chan_ctx->m2m_ctx, vbuf);
}

static void xm2msc_return_all_buffers(struct xm2msc_chan_ctx *chan_ctx,
				      struct vb2_queue *q,
				      enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *vb;
	unsigned long flags;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vb = v4l2_m2m_src_buf_remove(chan_ctx->m2m_ctx);
		else
			vb = v4l2_m2m_dst_buf_remove(chan_ctx->m2m_ctx);
		if (!vb)
			break;
		spin_lock_irqsave(&chan_ctx->xm2msc_dev->lock, flags);
		v4l2_m2m_buf_done(vb, state);
		spin_unlock_irqrestore(&chan_ctx->xm2msc_dev->lock, flags);
	}
}

static int xm2msc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct xm2msc_chan_ctx *chan_ctx = vb2_get_drv_priv(q);
	static struct xm2msc_q_data *q_data;
	int type;

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		xm2msc_set_chan_stream(chan_ctx, true, XM2MSC_CHAN_OUT);
	else
		xm2msc_set_chan_stream(chan_ctx, true, XM2MSC_CHAN_CAP);

	xm2msc_set_chan_params(chan_ctx, q->type);

	if (xm2msc_chk_chan_stream(chan_ctx, XM2MSC_CHAN_CAP) &&
	    xm2msc_chk_chan_stream(chan_ctx, XM2MSC_CHAN_OUT))
		xm2msc_set_chan_com_params(chan_ctx);

	type = V4L2_TYPE_IS_OUTPUT(q->type) ?
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q_data = get_q_data(chan_ctx, type);
	xm2msc_pr_q(chan_ctx->xm2msc_dev->dev, q_data, chan_ctx->num,
		    type, __func__);
	xm2msc_pr_status(chan_ctx->xm2msc_dev, __func__);

	return 0;
}

static void xm2msc_stop_streaming(struct vb2_queue *q)
{
	struct xm2msc_chan_ctx *chan_ctx = vb2_get_drv_priv(q);

	xm2msc_return_all_buffers(chan_ctx, q, VB2_BUF_STATE_ERROR);

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		xm2msc_set_chan_stream(chan_ctx, false, XM2MSC_CHAN_OUT);
	else
		xm2msc_set_chan_stream(chan_ctx, false, XM2MSC_CHAN_CAP);
}

static const struct vb2_ops xm2msc_qops = {
	.queue_setup = xm2msc_queue_setup,
	.buf_prepare = xm2msc_buf_prepare,
	.buf_queue = xm2msc_buf_queue,
	.start_streaming = xm2msc_start_streaming,
	.stop_streaming = xm2msc_stop_streaming,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct xm2msc_chan_ctx *chan_ctx = priv;
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->drv_priv = chan_ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &xm2msc_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &xm2msc->dev_mutex;
	src_vq->dev = xm2msc->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = chan_ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &xm2msc_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &xm2msc->dev_mutex;
	dst_vq->dev = xm2msc->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static const struct v4l2_ioctl_ops xm2msc_ioctl_ops = {
	.vidioc_querycap = xm2msc_querycap,

	.vidioc_enum_fmt_vid_cap_mplane = xm2msc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane = xm2msc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap_mplane = xm2msc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap_mplane = xm2msc_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out_mplane = xm2msc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out_mplane = xm2msc_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out_mplane = xm2msc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out_mplane = xm2msc_s_fmt_vid_out,

	.vidioc_reqbufs = xm2msc_reqbufs,
	.vidioc_querybuf = xm2msc_querybuf,
	.vidioc_expbuf = xm2msc_expbuf,
	.vidioc_create_bufs = xm2msc_createbufs,

	.vidioc_qbuf = xm2msc_qbuf,
	.vidioc_dqbuf = xm2msc_dqbuf,

	.vidioc_streamon = xm2msc_streamon,
	.vidioc_streamoff = xm2msc_streamoff,
};

static int xm2msc_open(struct file *file)
{
	struct xm2m_msc_dev *xm2msc = video_drvdata(file);
	struct xm2msc_chan_ctx *chan_ctx = NULL;
	u32 minor, chan;
	int ret;

	if (mutex_lock_interruptible(&xm2msc->dev_mutex))
		return -ERESTARTSYS;

	minor = iminor(file_inode(file));

	for (chan = 0; chan < xm2msc->max_chan; chan++) {
		chan_ctx = &xm2msc->xm2msc_chan[chan];

		if ((chan_ctx->status & CHAN_ATTACHED) &&
		    chan_ctx->minor == minor)
			break;
	}

	if (chan == xm2msc->max_chan) {
		v4l2_err(&xm2msc->v4l2_dev,
			 "%s Chan not found with minor = %d\n",
			 __func__, minor);
		ret = -EBADF;
		goto unlock;
	}

	/* Already opened, do not allow same channel
	 * to be open more then once
	 */
	if (chan_ctx->status & CHAN_OPENED) {
		v4l2_warn(&xm2msc->v4l2_dev,
			  "%s Chan already opened for minor = %d\n",
			  __func__, minor);
		ret = -EBUSY;
		goto unlock;
	}

	v4l2_fh_init(&chan_ctx->fh, &chan_ctx->vfd);
	file->private_data = &chan_ctx->fh;
	v4l2_fh_add(&chan_ctx->fh);

	chan_ctx->m2m_ctx = v4l2_m2m_ctx_init(chan_ctx->m2m_dev,
					      chan_ctx, &queue_init);
	if (IS_ERR(chan_ctx->m2m_ctx)) {
		ret = PTR_ERR(chan_ctx->m2m_ctx);
		v4l2_err(&xm2msc->v4l2_dev,
			 "%s Chan M2M CTX not creted for minor %d\n",
			 __func__, minor);
		goto error_m2m;
	}

	chan_ctx->fh.m2m_ctx = chan_ctx->m2m_ctx;
	chan_ctx->status |= CHAN_OPENED;
	chan_ctx->xm2msc_dev = xm2msc;
	chan_ctx->frames = 0;
	xm2msc_set_chan(chan_ctx, true);

	v4l2_info(&xm2msc->v4l2_dev, "Channel %d instance created\n", chan);

	mutex_unlock(&xm2msc->dev_mutex);
	xm2msc_pr_chanctx(chan_ctx, __func__);
	xm2msc_pr_status(xm2msc, __func__);
	return 0;

error_m2m:
	v4l2_fh_del(&chan_ctx->fh);
	v4l2_fh_exit(&chan_ctx->fh);
unlock:
	mutex_unlock(&xm2msc->dev_mutex);
	xm2msc_pr_chanctx(chan_ctx, __func__);
	xm2msc_pr_status(xm2msc, __func__);
	return ret;
}

static int xm2msc_release(struct file *file)
{
	struct xm2m_msc_dev *xm2msc = video_drvdata(file);
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(file->private_data);

	if (mutex_lock_interruptible(&xm2msc->dev_mutex))
		return -ERESTARTSYS;

	v4l2_m2m_ctx_release(chan_ctx->m2m_ctx);
	v4l2_fh_del(&chan_ctx->fh);
	v4l2_fh_exit(&chan_ctx->fh);
	chan_ctx->status &= ~CHAN_OPENED;
	xm2msc_set_chan(chan_ctx, false);

	v4l2_info(&xm2msc->v4l2_dev, "Channel %d instance released\n",
		  chan_ctx->num);

	mutex_unlock(&xm2msc->dev_mutex);
	return 0;
}

static unsigned int xm2msc_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct xm2msc_chan_ctx *chan_ctx = fh_to_chanctx(file->private_data);
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	int ret;

	mutex_lock(&xm2msc->dev_mutex);
	ret = v4l2_m2m_poll(file, chan_ctx->m2m_ctx, wait);
	mutex_unlock(&xm2msc->dev_mutex);

	return ret;
}

static int xm2msc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct xm2msc_chan_ctx *chan_ctx = file->private_data;
	struct xm2m_msc_dev *xm2msc = chan_ctx->xm2msc_dev;
	int ret;

	mutex_lock(&xm2msc->dev_mutex);
	ret = v4l2_m2m_mmap(file, chan_ctx->m2m_ctx, vma);

	mutex_unlock(&xm2msc->dev_mutex);
	return ret;
}

static const struct v4l2_file_operations xm2msc_fops = {
	.owner = THIS_MODULE,
	.open = xm2msc_open,
	.release = xm2msc_release,
	.poll = xm2msc_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = xm2msc_mmap,
};

static const struct video_device xm2msc_videodev = {
	.name = XM2MSC_DRIVER_NAME,
	.fops = &xm2msc_fops,
	.ioctl_ops = &xm2msc_ioctl_ops,
	.minor = -1,
	.release = video_device_release_empty,
	.vfl_dir = VFL_DIR_M2M,
};

static const struct v4l2_m2m_ops xm2msc_m2m_ops = {
	.device_run = xm2msc_device_run,
	.job_ready = xm2msc_job_ready,
	.job_abort = xm2msc_job_abort,
};

static int xm2msc_parse_of(struct platform_device *pdev,
			   struct xm2m_msc_dev *xm2msc)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	int hw_vid_fmt_cnt;
	const char *vid_fmts[ARRAY_SIZE(formats)];
	int ret;
	u32 i, j;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xm2msc->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR((__force void *)xm2msc->regs))
		return PTR_ERR((__force const void *)xm2msc->regs);

	dev_dbg(dev, "IO Mem 0x%llx mapped at %p\n", res->start, xm2msc->regs);

	ret = of_property_read_u32(node, "xlnx,max-chan",
				   &xm2msc->max_chan);
	if (ret < 0)
		return ret;

	if (xm2msc->max_chan < XM2MSC_MIN_CHAN ||
	    xm2msc->max_chan > XM2MSC_MAX_CHAN) {
		dev_err(dev,
			"Invalid maximum scaler channels : %d",
			xm2msc->max_chan);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,max-width",
				   &xm2msc->max_wd);
	if (ret < 0) {
		dev_err(dev,
			"missing xlnx,max-width prop\n");
		return ret;
	}

	if (xm2msc->max_wd < XM2MSC_MIN_WIDTH ||
	    xm2msc->max_wd > XM2MSC_MAX_WIDTH) {
		dev_err(dev, "Invalid width : %d",
			xm2msc->max_wd);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,max-height",
				   &xm2msc->max_ht);
	if (ret < 0) {
		dev_err(dev, "missing xlnx,max-height prop\n");
		return ret;
	}

	if (xm2msc->max_ht < XM2MSC_MIN_HEIGHT ||
	    xm2msc->max_ht > XM2MSC_MAX_HEIGHT) {
		dev_err(dev, "Invalid height : %d",
			xm2msc->max_ht);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,dma-addr-width",
				   &xm2msc->dma_addr_size);
	if (ret || (xm2msc->dma_addr_size != 32 &&
		    xm2msc->dma_addr_size != 64)) {
		dev_err(dev, "missing/invalid addr width dts prop\n");
		return -EINVAL;
	}

	xm2msc->irq = irq_of_parse_and_map(node, 0);
	if (xm2msc->irq < 0) {
		dev_err(dev, "Unable to get IRQ");
		return xm2msc->irq;
	}

	dev_dbg(dev, "Max Channel Supported = %d\n", xm2msc->max_chan);
	dev_dbg(dev, "DMA Addr width Supported = %d\n", xm2msc->dma_addr_size);
	dev_dbg(dev, "Max col/row Supported = (%d) / (%d)\n",
		xm2msc->max_wd, xm2msc->max_ht);
	/* read supported video formats and update internal table */
	hw_vid_fmt_cnt = of_property_count_strings(node, "xlnx,vid-formats");

	ret = of_property_read_string_array(node, "xlnx,vid-formats",
					    vid_fmts, hw_vid_fmt_cnt);
	if (ret < 0) {
		dev_err(dev,
			"Missing or invalid xlnx,vid-formats dts prop\n");
		return ret;
	}

	dev_dbg(dev, "Supported format = ");
	for (i = 0; i < hw_vid_fmt_cnt; i++) {
		const char *vid_fmt_name = vid_fmts[i];

		for (j = 0; j < ARRAY_SIZE(formats); j++) {
			const char *dts_name = formats[j].name;

			if (strcmp(vid_fmt_name, dts_name))
				continue;
			dev_dbg(dev, "%s ", dts_name);

			xm2msc_set_fmt(xm2msc, j);
		}
	}
	dev_dbg(dev, "\n");
	xm2msc->rst_gpio = devm_gpiod_get(dev, "reset",
					  GPIOD_OUT_HIGH);
	if (IS_ERR(xm2msc->rst_gpio)) {
		ret = PTR_ERR(xm2msc->rst_gpio);
		if (ret == -EPROBE_DEFER)
			dev_info(dev,
				 "Probe deferred due to GPIO reset defer\n");
		else
			dev_err(dev,
				"Unable to locate reset property in dt\n");
		return ret;
	}

	return 0;
}

static void xm2msc_unreg_video_n_m2m(struct xm2m_msc_dev *xm2msc)
{
	struct xm2msc_chan_ctx *chan_ctx;
	unsigned int chan;

	for (chan = 0; chan < xm2msc->max_chan; chan++) {
		chan_ctx = &xm2msc->xm2msc_chan[chan];
		if (!(chan_ctx->status & CHAN_ATTACHED))
			break;	/*We register video sequentially */
		video_unregister_device(&chan_ctx->vfd);
		chan_ctx->status &= ~CHAN_ATTACHED;

		if (!IS_ERR(chan_ctx->m2m_dev))
			v4l2_m2m_release(chan_ctx->m2m_dev);
	}
}

static int xm2m_msc_probe(struct platform_device *pdev)
{
	int ret;
	struct xm2m_msc_dev *xm2msc;
	struct xm2msc_chan_ctx *chan_ctx;
	struct video_device *vfd;
	unsigned int chan;

	xm2msc = devm_kzalloc(&pdev->dev, sizeof(*xm2msc), GFP_KERNEL);
	if (!xm2msc)
		return -ENOMEM;

	ret = xm2msc_parse_of(pdev, xm2msc);
	if (ret < 0)
		return ret;

	xm2msc->dev = &pdev->dev;

	xm2msc_reset(xm2msc);

	spin_lock_init(&xm2msc->lock);

	ret = v4l2_device_register(&pdev->dev, &xm2msc->v4l2_dev);
	if (ret)
		return ret;

	for (chan = 0; chan < xm2msc->max_chan; chan++) {
		chan_ctx = &xm2msc->xm2msc_chan[chan];

		vfd = &chan_ctx->vfd;
		*vfd = xm2msc_videodev;
		vfd->lock = &xm2msc->dev_mutex;
		vfd->v4l2_dev = &xm2msc->v4l2_dev;

		ret = video_register_device(vfd, VFL_TYPE_GRABBER, chan);
		if (ret) {
			v4l2_err(&xm2msc->v4l2_dev,
				 "Failed to register video dev for chan %d\n",
				 chan);
			goto unreg_dev;
		}

		chan_ctx->status = CHAN_ATTACHED;

		video_set_drvdata(vfd, xm2msc);
		snprintf(vfd->name, sizeof(vfd->name),
			 "%s", xm2msc_videodev.name);
		v4l2_info(&xm2msc->v4l2_dev,
			  " Device registered as /dev/video%d\n", vfd->num);

		dev_dbg(xm2msc->dev, "%s Device registered as /dev/video%d\n",
			__func__, vfd->num);

		chan_ctx->m2m_dev = v4l2_m2m_init(&xm2msc_m2m_ops);
		if (IS_ERR(chan_ctx->m2m_dev)) {
			v4l2_err(&xm2msc->v4l2_dev,
				 "Failed to init mem2mem device for chan %d\n",
				 chan);
			ret = PTR_ERR(chan_ctx->m2m_dev);
			goto unreg_dev;
		}
		chan_ctx->xm2msc_dev = xm2msc;
		chan_ctx->regs = xm2msc->regs + XM2MSC_CHAN_REGS_START(chan);
		chan_ctx->num = chan;
		chan_ctx->minor = vfd->minor;
		xm2msc_pr_chanctx(chan_ctx, __func__);
	}

	mutex_init(&xm2msc->dev_mutex);
	mutex_init(&xm2msc->mutex);
	init_waitqueue_head(&xm2msc->isr_finished);

	ret = devm_request_irq(&pdev->dev, xm2msc->irq,
			       xm2msc_isr, IRQF_SHARED,
			       XM2MSC_DRIVER_NAME, xm2msc);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to register IRQ\n");
		goto unreg_dev;
	}

	platform_set_drvdata(pdev, xm2msc);

	return 0;

 unreg_dev:
	xm2msc_unreg_video_n_m2m(xm2msc);
	v4l2_device_unregister(&xm2msc->v4l2_dev);
	return ret;
}

static int xm2m_msc_remove(struct platform_device *pdev)
{
	struct xm2m_msc_dev *xm2msc = platform_get_drvdata(pdev);

	xm2msc_unreg_video_n_m2m(xm2msc);
	v4l2_device_unregister(&xm2msc->v4l2_dev);
	return 0;
}

static const struct of_device_id xm2m_msc_of_id_table[] = {
	{.compatible = "xlnx,v-multi-scaler-v1.0"},
	{}
};

MODULE_DEVICE_TABLE(of, xm2m_msc_of_id_table);

static struct platform_driver xm2m_msc_driver = {
	.driver = {
		.name = "xilinx-multiscaler",
		.of_match_table = xm2m_msc_of_id_table,
	},
	.probe = xm2m_msc_probe,
	.remove = xm2m_msc_remove,
};

module_platform_driver(xm2m_msc_driver);

MODULE_DESCRIPTION("Xilinx M2M Multi-Scaler Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("xlnx_m2m_multiscaler_dev");

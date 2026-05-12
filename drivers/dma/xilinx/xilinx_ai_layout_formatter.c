// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DMAEngine driver for Xilinx AI_Layout_Formatter IP
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * Authors: Mounik Katikala <mounik.katikala@amd.com>
 *
 * Description:
 * The AXI AI_Layout_Formatter core is a soft Xilinx IP core that
 * provides high-bandwidth direct memory access between memory
 * and AXI4-Stream.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <linux/dma/xilinx_ai_layout_formatter.h>

#include "../dmaengine.h"

/* platform_driver.driver.name (sysfs / debug identifiers). */
#define XILINX_AI_LAYOUT_FORMATTER_DRIVER_NAME	"xilinx-ai-layout-formatter"

/* Maximum plane buffers per interleaved descriptor / hardware address slots */
#define XILINX_AI_LAYOUT_FORMATTER_MAX_PLANES	4

/* Register/Descriptor Offsets */
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_OFFSET		0x00
#define XILINX_AI_LAYOUT_FORMATTER_GIE_OFFSET		0x04
#define XILINX_AI_LAYOUT_FORMATTER_IE_OFFSET		0x08
#define XILINX_AI_LAYOUT_FORMATTER_ISR_OFFSET		0x0c
#define XILINX_AI_LAYOUT_FORMATTER_WIDTH_OFFSET		0x10
#define XILINX_AI_LAYOUT_FORMATTER_HEIGHT_OFFSET		0x18
#define XILINX_AI_LAYOUT_FORMATTER_ADDR_OFFSET		0x30
#define XILINX_AI_LAYOUT_FORMATTER_ADDR2_OFFSET		0x3c
#define XILINX_AI_LAYOUT_FORMATTER_ADDR3_OFFSET		0x54
#define XILINX_AI_LAYOUT_FORMATTER_ADDR4_OFFSET		0x60
#define XILINX_AI_LAYOUT_FORMATTER_ADDR5_OFFSET		0x70
#define XILINX_AI_LAYOUT_FORMATTER_LAYOUT_FORMAT_OFFSET		0x80
#define XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_OFFSET		0x90
#define XILINX_AI_LAYOUT_FORMATTER_CHANNELS_OUT_OFFSET		0xa0

/* Control Registers */
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_START		BIT(0)
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_DONE		BIT(1)
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_IDLE		BIT(2)
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_READY		BIT(3)
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_FLUSH		BIT(5)
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_FLUSH_DONE		BIT(6)
#define XILINX_AI_LAYOUT_FORMATTER_CTRL_AUTO_RESTART		BIT(7)
#define XILINX_AI_LAYOUT_FORMATTER_GIE_EN		BIT(0)

/* Interrupt Status and Control */
#define XILINX_AI_LAYOUT_FORMATTER_IE_AP_DONE		BIT(0)
#define XILINX_AI_LAYOUT_FORMATTER_IE_AP_READY		BIT(1)

#define XILINX_AI_LAYOUT_FORMATTER_ISR_AP_DONE_IRQ		BIT(0)
#define XILINX_AI_LAYOUT_FORMATTER_ISR_AP_READY_IRQ		BIT(1)

#define XILINX_AI_LAYOUT_FORMATTER_ISR_ALL_IRQ_MASK	\
		(XILINX_AI_LAYOUT_FORMATTER_ISR_AP_DONE_IRQ | \
		XILINX_AI_LAYOUT_FORMATTER_ISR_AP_READY_IRQ)

/* Video Format Register Settings */

#define XILINX_AI_LAYOUT_FORMATTER_FMT_Y8			24
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB8			29

#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_BF161616		44
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_FP161616		45
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB323232		46
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA8888		47
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_BF16161616		48
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_FP16161616		49
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA32323232		50
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB8M		51
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_BF161616M		52
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_FP161616M		53
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB323232M		54
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA8M		55
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_BF16161616M		56
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_FP16161616M		57
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA32323232M		58
#define XILINX_AI_LAYOUT_FORMATTER_FMT_GREY_BF16		60
#define XILINX_AI_LAYOUT_FORMATTER_FMT_GREY_FP16		61
#define XILINX_AI_LAYOUT_FORMATTER_FMT_GREY32		62
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_8_4_4		63
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_BF16_4_4		64
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_FP16_4_4		65
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_32_4_4		66
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_8_4_4		67
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_BF16_4_4		68
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_FP16_4_4		69
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_32_4_4		70
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_8_4_3		71
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_BF16_4_3		72
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_FP16_4_3		73
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_32_4_3		74
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_8_4_3		75
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_BF16_4_3		76
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_FP16_4_3		77
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_32_4_3		78
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_8_3_3		79
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_BF16_3_3		80
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_FP16_3_3		81
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_32_3_3		82
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_8_3_3		83
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_BF16_3_3		84
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_FP16_3_3		85
#define XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_32_3_3		86
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_24M_4_3		87
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_BF48M_4_3		88
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_FP48M_4_3		89
#define XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_323232M_4_3		90

/* Layout Format Register */
#define XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC		0
#define XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4		1
#define XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8		2
#define XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW		3

/* Data type Register */
#define XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8		0
#define XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16		1
#define XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16		2
#define XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32		3

#define XILINX_AI_LAYOUT_FORMATTER_ALIGN_MUL		8

#define XILINX_AI_LAYOUT_FORMATTER_WAIT_FOR_FLUSH_DONE		25
#define XILINX_AI_LAYOUT_FORMATTER_WAIT_FOR_AP_IDLE		2000
#define XILINX_AI_LAYOUT_FORMATTER_RESET_PULSE_US		1
#define XILINX_AI_LAYOUT_FORMATTER_FRAME_BOUNDARY_MS		50
#define XILINX_AI_LAYOUT_FORMATTER_FLUSH_SLEEP_MAX_US		2100
#define XILINX_AI_LAYOUT_FORMATTER_FLUSH_TIMEOUT_US		\
	(XILINX_AI_LAYOUT_FORMATTER_WAIT_FOR_FLUSH_DONE * \
	 XILINX_AI_LAYOUT_FORMATTER_FLUSH_SLEEP_MAX_US)
#define XILINX_AI_LAYOUT_FORMATTER_DMA_ADDR_WIDTH_32		32
#define XILINX_AI_LAYOUT_FORMATTER_DMA_ADDR_WIDTH_64		64

/* Video frame dimension limits (device tree may clamp within this range). */
#define XILINX_AI_LAYOUT_FORMATTER_MIN_HEIGHT		64
#define XILINX_AI_LAYOUT_FORMATTER_MIN_WIDTH		64
#define XILINX_AI_LAYOUT_FORMATTER_MAX_WIDTH		15360
#define XILINX_AI_LAYOUT_FORMATTER_MAX_HEIGHT		8640

/* Internal: V4L2 fourcc path for format verification (only type supported today). */
#define XILINX_AI_DMA_V4L2		1

/**
 * struct xilinx_ai_layout_formatter_desc_hw - Hardware Descriptor
 * @channel_plane_addr: DMA addresses for up to four planes. [0] is the first
 *	plane (packed RGB/RGBA holds all components; planar: typically Y or
 *	RGB). [1] is the second plane (e.g. U or UV chroma); unused for packed
 *	formats. [2] is the third plane (e.g. V for 3-plane YUV); unused for
 *	packed or two-plane formats. [3] is the fourth plane (e.g. separate
 *	alpha); unused when the format has fewer planes.
 * @vsize: Vertical size in lines
 * @hsize: Horizontal size in pixels
 * @stride: Bytes between the first pixels of consecutive horizontal lines
 */
struct xilinx_ai_layout_formatter_desc_hw {
	dma_addr_t channel_plane_addr[XILINX_AI_LAYOUT_FORMATTER_MAX_PLANES];
	u32 vsize;
	u32 hsize;
	u32 stride;
};

/**
 * struct xilinx_ai_layout_formatter_tx_descriptor - Per Transaction structure
 * @async_tx: Async transaction descriptor
 * @hw: Hardware descriptor
 * @node: Node in the channel descriptors list
 */
struct xilinx_ai_layout_formatter_tx_descriptor {
	struct dma_async_tx_descriptor async_tx;
	struct xilinx_ai_layout_formatter_desc_hw hw;
	struct list_head node;
};

/**
 * struct xilinx_ai_layout_formatter_chan - Driver specific dma channel structure
 * @xdev: Driver specific device structure
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @done_list: Complete descriptors
 * @active_desc: Descriptor currently running in hardware until AP_DONE
 * @common: DMA common channel
 * @dev: The dma device
 * @write_addr: callback that will write dma addresses to IP (32 or 64 bit)
 * @irq: Channel IRQ
 * @irq_status: Interrupt status bits saved for threaded handler
 * @direction: Transfer direction
 * @idle: Channel idle state
 * @vid_fmt: Reference to currently assigned video format description; must be
 *	     non-NULL before any transfer is started.
 * @mode: Select operation mode
 */
struct xilinx_ai_layout_formatter_chan {
	struct xilinx_ai_layout_formatter_device *xdev;
	spinlock_t lock;
	struct list_head pending_list;
	struct list_head done_list;
	struct xilinx_ai_layout_formatter_tx_descriptor *active_desc;
	struct dma_chan common;
	struct device *dev;
	void (*write_addr)(struct xilinx_ai_layout_formatter_chan *chan, u32 reg,
			   dma_addr_t value);
	int irq;
	u32 irq_status;
	enum dma_transfer_direction direction;
	bool idle;
	const struct xilinx_ai_layout_formatter_format_desc *vid_fmt;
	enum xilinx_vid_dma_mode mode;
};

/**
 * struct xilinx_ai_layout_formatter_format_desc - lookup table to match fourcc to format
 * @dts_name: Device tree name for this entry.
 * @id: Format ID
 * @bpw: Bits of pixel data + padding in a 32-bit word (luma plane for semi-pl)
 * @ppw: Number of pixels represented in a 32-bit word (luma plane for semi-pl)
 * @num_planes: Expected number of plane buffers in ai_layout_formatter for this format
 * @v4l2_fmt: Video 4 Linux framework equivalent fourcc code
 * @layout_fmt_bitmask: Flag identifying this format in device-specific "enabled"
 *	bitmap for ai layout formatter.
 * @layout_format: Enumerated value specifying the layout format
 *	(such as single, semi-planar, or multi-planar).
 * @data_type: Numeric identifier for the data type used with this format.
 * @channels_out: Number of output data channels for this format.
 */
struct xilinx_ai_layout_formatter_format_desc {
	const char *dts_name;
	u32 id;
	u32 bpw;
	u32 ppw;
	u32 num_planes;
	u32 v4l2_fmt;
	u64 layout_fmt_bitmask;
	u32 layout_format;
	u32 data_type;
	u32 channels_out;
};

static const struct xilinx_ai_layout_formatter_format_desc xilinx_ai_layout_formatter_formats[] = {
	{
		.dts_name = "bgr888",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB8,
		.bpw = 24,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_RGB24,
		.layout_fmt_bitmask = BIT_ULL(0),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "y8",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_Y8,
		.bpw = 32,
		.ppw = 4,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_GREY,
		.layout_fmt_bitmask = BIT_ULL(17),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 1,
	},
	{
		.dts_name = "rgb888",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB8,
		.bpw = 24,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_RGB24,
		.layout_fmt_bitmask = BIT_ULL(1),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "rgb_bf16",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_BF161616,
		.bpw = 48,
		.ppw = 1,
		.num_planes = 1,
		.layout_fmt_bitmask = BIT_ULL(2),
		.v4l2_fmt = V4L2_PIX_FMT_RGB_BF48,
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 3,
	},
	{
		.dts_name = "rgb_fp16",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_FP161616,
		.bpw = 48,
		.ppw = 1,
		.num_planes = 1,
		.layout_fmt_bitmask = BIT_ULL(3),
		.v4l2_fmt = V4L2_PIX_FMT_RGB_FP48,
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 3,
	},
	{
		.dts_name = "rgb323232",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB323232,
		.bpw = 96,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_RGB_FP323232,
		.layout_fmt_bitmask = BIT_ULL(4),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 3,
	},
	{
		.dts_name = "rgba8888",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA8888,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA32,
		.layout_fmt_bitmask = BIT_ULL(5),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 4,
	},
	{
		.dts_name = "rgba_bf16161616",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_BF16161616,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA_BF64,
		.layout_fmt_bitmask = BIT_ULL(6),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 4,
	},
	{
		.dts_name = "rgba_fp16161616",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_FP16161616,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA_FP64,
		.layout_fmt_bitmask = BIT_ULL(7),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 4,
	},
	{
		.dts_name = "rgba32323232",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA32323232,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA_FP32323232,
		.layout_fmt_bitmask = BIT_ULL(8),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 4,
	},
	{
		.dts_name = "rgb888m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB8M,
		.bpw = 32,
		.ppw = 4,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB24P,
		.layout_fmt_bitmask = BIT_ULL(9),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "rgb_bf161616m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_BF161616M,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB_BF48P,
		.layout_fmt_bitmask = BIT_ULL(10),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 3,
	},
	{
		.dts_name = "rgb_fp161616m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_FP161616M,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB_FP48P,
		.layout_fmt_bitmask = BIT_ULL(11),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 3,
	},
	{
		.dts_name = "rgb323232m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB323232M,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB_FP323232P,
		.layout_fmt_bitmask = BIT_ULL(12),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 3,
	},
	{
		.dts_name = "rgba8888m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA8M,
		.bpw = 32,
		.ppw = 4,
		.num_planes = 4,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA32P,
		.layout_fmt_bitmask = BIT_ULL(13),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 4,
	},
	{
		.dts_name = "rgba_bf16161616m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_BF16161616M,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 4,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA_BF64P,
		.layout_fmt_bitmask = BIT_ULL(14),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 4,
	},
	{
		.dts_name = "rgba_fp16161616m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA_FP16161616M,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 4,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA_FP64P,
		.layout_fmt_bitmask = BIT_ULL(15),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 4,
	},
	{
		.dts_name = "rgba32323232m",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGBA32323232M,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 4,
		.v4l2_fmt = V4L2_PIX_FMT_RGBA_FP32323232P,
		.layout_fmt_bitmask = BIT_ULL(16),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 4,
	},
	{
		.dts_name = "gray_bf16",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_GREY_BF16,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_GREY_BF16,
		.layout_fmt_bitmask = BIT_ULL(18),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 1,
	},
	{
		.dts_name = "gray_fp16",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_GREY_FP16,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_GREY_FP16,
		.layout_fmt_bitmask = BIT_ULL(19),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 1,
	},
	{
		.dts_name = "gray32",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_GREY32,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_GREY_FP32,
		.layout_fmt_bitmask = BIT_ULL(20),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NHWC,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 1,
	},
	{
		.dts_name = "HCWNC4_8_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_8_4_4,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_8_4_4,
		.layout_fmt_bitmask = BIT_ULL(21),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC4_BF16_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_BF16_4_4,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_BF16_4_4,
		.layout_fmt_bitmask = BIT_ULL(22),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC4_FP16_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_FP16_4_4,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_FP16_4_4,
		.layout_fmt_bitmask = BIT_ULL(23),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC4_32_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_32_4_4,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_FP32_4_4,
		.layout_fmt_bitmask = BIT_ULL(24),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC8_8_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_8_4_4,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_8_4_4,
		.layout_fmt_bitmask = BIT_ULL(25),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC8_BF16_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_BF16_4_4,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_BF16_4_4,
		.layout_fmt_bitmask = BIT_ULL(26),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC8_FP16_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_FP16_4_4,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_FP16_4_4,
		.layout_fmt_bitmask = BIT_ULL(27),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC8_32_4_4",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_32_4_4,
		.bpw = 256,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_FP32_4_4,
		.layout_fmt_bitmask = BIT_ULL(28),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 4,
	},
	{
		.dts_name = "HCWNC4_8_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_8_4_3,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_8_4_3,
		.layout_fmt_bitmask = BIT_ULL(29),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC4_BF16_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_BF16_4_3,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_BF16_4_3,
		.layout_fmt_bitmask = BIT_ULL(30),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC4_FP16_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_FP16_4_3,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_FP16_4_3,
		.layout_fmt_bitmask = BIT_ULL(31),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC4_32_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_32_4_3,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_FP32_4_3,
		.layout_fmt_bitmask = BIT_ULL(32),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_8_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_8_4_3,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_8_4_3,
		.layout_fmt_bitmask = BIT_ULL(33),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_BF16_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_BF16_4_3,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_BF16_4_3,
		.layout_fmt_bitmask = BIT_ULL(34),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_FP16_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_FP16_4_3,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_FP16_4_3,
		.layout_fmt_bitmask = BIT_ULL(35),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_32_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_32_4_3,
		.bpw = 256,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_FP32_4_3,
		.layout_fmt_bitmask = BIT_ULL(36),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC4_8_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_8_3_3,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_8_3_3,
		.layout_fmt_bitmask = BIT_ULL(37),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC4_BF16_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_BF16_3_3,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_BF16_3_3,
		.layout_fmt_bitmask = BIT_ULL(38),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC4_FP16_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_FP16_3_3,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_FP16_3_3,
		.layout_fmt_bitmask = BIT_ULL(39),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC4_32_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC4_32_3_3,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC4_FP32_3_3,
		.layout_fmt_bitmask = BIT_ULL(40),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC4,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_8_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_8_3_3,
		.bpw = 64,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_8_3_3,
		.layout_fmt_bitmask = BIT_ULL(41),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_BF16_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_BF16_3_3,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_BF16_3_3,
		.layout_fmt_bitmask = BIT_ULL(42),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_FP16_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_FP16_3_3,
		.bpw = 128,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_FP16_3_3,
		.layout_fmt_bitmask = BIT_ULL(43),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 3,
	},
	{
		.dts_name = "HCWNC8_32_3_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_HCWNC8_32_3_3,
		.bpw = 256,
		.ppw = 1,
		.num_planes = 1,
		.v4l2_fmt = V4L2_PIX_FMT_HCWNC8_FP32_3_3,
		.layout_fmt_bitmask = BIT_ULL(44),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_HCWNC8,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 3,
	},
	{
		.dts_name = "RGB_24M_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_24M_4_3,
		.bpw = 32,
		.ppw = 4,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB24P_4_3,
		.layout_fmt_bitmask = BIT_ULL(45),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_INT8,
		.channels_out = 3,
	},
	{
		.dts_name = "RGB_BF48M_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_BF48M_4_3,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB_BF48P_4_3,
		.layout_fmt_bitmask = BIT_ULL(46),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_BF16,
		.channels_out = 3,
	},
	{
		.dts_name = "RGB_FP48M_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_FP48M_4_3,
		.bpw = 32,
		.ppw = 2,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB_FP48P_4_3,
		.layout_fmt_bitmask = BIT_ULL(47),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP16,
		.channels_out = 3,
	},
	{
		.dts_name = "RGB_323232M_4_3",
		.id = XILINX_AI_LAYOUT_FORMATTER_FMT_RGB_323232M_4_3,
		.bpw = 32,
		.ppw = 1,
		.num_planes = 3,
		.v4l2_fmt = V4L2_PIX_FMT_RGB_FP323232P_4_3,
		.layout_fmt_bitmask = BIT_ULL(48),
		.layout_format = XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW,
		.data_type = XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_FP32,
		.channels_out = 3,
	},
};

/**
 * struct xilinx_ai_layout_formatter_feature - dt or IP property structure
 * @direction: dma transfer mode and direction
 * @flags: Bitmask of properties enabled in IP or dt
 */
struct xilinx_ai_layout_formatter_feature {
	enum dma_transfer_direction direction;
	u32 flags;
};

/**
 * struct xilinx_ai_layout_formatter_device - dma device structure
 * @regs: I/O mapped base address
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific dma channel
 * @rst_gpio: GPIO reset
 * @enabled_vid_fmts: Bitmask of video formats enabled in hardware
 * @v4l2_memory_fmts: Array of supported V4L2 fourcc codes
 * @v4l2_fmt_cnt: Count of supported V4L2 fourcc codes
 * @cfg: Pointer to AI_Layout_Formatter Feature config struct
 * @max_width: Maximum pixel width supported in IP.
 * @max_height: Maximum number of lines supported in IP.
 * @ppc: Pixels per clock supported in IP.
 * @ap_clk: Video core clock
 */
struct xilinx_ai_layout_formatter_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_ai_layout_formatter_chan chan;
	struct gpio_desc *rst_gpio;
	u64 enabled_vid_fmts;
	u32 v4l2_memory_fmts[ARRAY_SIZE(xilinx_ai_layout_formatter_formats)];
	u32 v4l2_fmt_cnt;
	const struct xilinx_ai_layout_formatter_feature *cfg;
	u32 max_width;
	u32 max_height;
	u32 ppc;
	struct clk *ap_clk;
};

static const struct xilinx_ai_layout_formatter_feature xlnx_ai_layout_formatter_cfg_v1 = {
	.direction = DMA_DEV_TO_MEM,
};

/* Helper macros and inlines */
#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_ai_layout_formatter_chan, common)
#define to_dma_tx_descriptor(tx) \
	container_of(tx, struct xilinx_ai_layout_formatter_tx_descriptor, async_tx)

static inline u32 ai_layout_formatter_read(struct xilinx_ai_layout_formatter_chan *chan, u32 reg)
{
	return readl(chan->xdev->regs + reg);
}

static inline void ai_layout_formatter_write(struct xilinx_ai_layout_formatter_chan *chan, u32 reg,
					     u32 value)
{
	writel(value, chan->xdev->regs + reg);
}

static inline void ai_layout_formatter_writeq(struct xilinx_ai_layout_formatter_chan *chan, u32 reg,
					      u64 value)
{
	writel(lower_32_bits(value), chan->xdev->regs + reg);
	writel(upper_32_bits(value), chan->xdev->regs + reg + 4);
}

static void writeq_addr(struct xilinx_ai_layout_formatter_chan *chan, u32 reg,
			dma_addr_t addr)
{
	ai_layout_formatter_writeq(chan, reg, (u64)addr);
}

/* 32-bit DMA: hardware registers hold only the low 32 bits of the address. */
static void write_addr(struct xilinx_ai_layout_formatter_chan *chan, u32 reg,
		       dma_addr_t addr)
{
	ai_layout_formatter_write(chan, reg, lower_32_bits(addr));
}

static inline void ai_layout_formatter_clr(struct xilinx_ai_layout_formatter_chan *chan, u32 reg,
					   u32 clr)
{
	ai_layout_formatter_write(chan, reg, ai_layout_formatter_read(chan, reg) & ~clr);
}

static inline void ai_layout_formatter_set(struct xilinx_ai_layout_formatter_chan *chan, u32 reg,
					   u32 set)
{
	ai_layout_formatter_write(chan, reg, ai_layout_formatter_read(chan, reg) | set);
}

static void ai_layout_formatter_init_format_array(struct xilinx_ai_layout_formatter_device *xdev)
{
	u32 i, cnt;

	for (i = 0; i < ARRAY_SIZE(xilinx_ai_layout_formatter_formats); i++) {
		if (!(xdev->enabled_vid_fmts &
				xilinx_ai_layout_formatter_formats[i].layout_fmt_bitmask))
			continue;

		if (xilinx_ai_layout_formatter_formats[i].v4l2_fmt) {
			cnt = xdev->v4l2_fmt_cnt++;
			xdev->v4l2_memory_fmts[cnt] =
				xilinx_ai_layout_formatter_formats[i].v4l2_fmt;
		}
	}
}

static struct xilinx_ai_layout_formatter_chan *xilinx_ai_chan_get(struct dma_chan *chan)
{
	if (!chan || !xilinx_ai_layout_formatter_is_dma_device(chan))
		return ERR_PTR(-EINVAL);
	return to_xilinx_chan(chan);
}

static int ai_layout_formatter_verify_format(struct dma_chan *chan, u32 fourcc, u32 type)
{
	struct xilinx_ai_layout_formatter_chan *xil_chan = to_xilinx_chan(chan);
	u32 i, sz = ARRAY_SIZE(xilinx_ai_layout_formatter_formats);

	if (type != XILINX_AI_DMA_V4L2)
		return -EINVAL;

	for (i = 0; i < sz; i++) {
		if (fourcc != xilinx_ai_layout_formatter_formats[i].v4l2_fmt)
			continue;

		if (!(xilinx_ai_layout_formatter_formats[i].layout_fmt_bitmask &
		      xil_chan->xdev->enabled_vid_fmts))
			continue;

		xil_chan->vid_fmt = &xilinx_ai_layout_formatter_formats[i];
		return 0;
	}
	return -EINVAL;
}

static void xilinx_ai_xdma_set_config(struct dma_chan *chan, u32 fourcc, u32 type)
{
	int ret;

	if (!chan || !xilinx_ai_layout_formatter_is_dma_device(chan))
		return;

	ret = ai_layout_formatter_verify_format(chan, fourcc, type);
	if (ret) {
		dev_err(chan->device->dev,
			"AI_Layout_Formatter not configured for fourcc 0x%x\n",
			fourcc);
		return;
	}
}

void xilinx_ai_xdma_set_mode(struct dma_chan *chan, enum xilinx_vid_dma_mode mode)
{
	struct xilinx_ai_layout_formatter_chan *xil_chan;

	xil_chan = xilinx_ai_chan_get(chan);
	if (IS_ERR(xil_chan))
		return;

	xil_chan->mode = mode;
}
EXPORT_SYMBOL_GPL(xilinx_ai_xdma_set_mode);

void xilinx_ai_xdma_v4l2_config(struct dma_chan *chan, u32 v4l2_fourcc)
{
	xilinx_ai_xdma_set_config(chan, v4l2_fourcc, XILINX_AI_DMA_V4L2);
}
EXPORT_SYMBOL_GPL(xilinx_ai_xdma_v4l2_config);

int xilinx_ai_xdma_get_v4l2_vid_fmts(struct dma_chan *chan, u32 *fmt_cnt,
				     u32 **fmts)
{
	struct xilinx_ai_layout_formatter_chan *xil_chan;

	xil_chan = xilinx_ai_chan_get(chan);
	if (IS_ERR(xil_chan))
		return PTR_ERR(xil_chan);

	*fmt_cnt = xil_chan->xdev->v4l2_fmt_cnt;
	*fmts = xil_chan->xdev->v4l2_memory_fmts;

	return 0;
}
EXPORT_SYMBOL_GPL(xilinx_ai_xdma_get_v4l2_vid_fmts);

int xilinx_ai_xdma_get_width_align(struct dma_chan *chan, u32 *width_align)
{
	struct xilinx_ai_layout_formatter_chan *xil_chan;

	xil_chan = xilinx_ai_chan_get(chan);
	if (IS_ERR(xil_chan))
		return PTR_ERR(xil_chan);
	*width_align = xil_chan->xdev->ppc;

	return 0;
}
EXPORT_SYMBOL_GPL(xilinx_ai_xdma_get_width_align);

/**
 * of_dma_xilinx_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success or error code on error
 */
static struct dma_chan *of_dma_xilinx_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct xilinx_ai_layout_formatter_device *xdev = ofdma->of_dma_data;

	return dma_get_slave_channel(&xdev->chan.common);
}

/* -----------------------------------------------------------------------------
 * Descriptors alloc and free
 */

/**
 * xilinx_ai_layout_formatter_free_desc_list - Free descriptors list
 * @chan: Driver specific dma channel
 * @list: List to parse and delete the descriptor
 */
static void xilinx_ai_layout_formatter_free_desc_list(struct xilinx_ai_layout_formatter_chan *chan,
						      struct list_head *list)
{
	struct xilinx_ai_layout_formatter_tx_descriptor *desc, *next;

	list_for_each_entry_safe(desc, next, list, node) {
		list_del(&desc->node);
		kfree(desc);
	}
}

/**
 * xilinx_ai_layout_formatter_free_descriptors_locked - Free channel descriptors
 * @chan: Driver specific dma channel
 *
 * Context: Caller must hold @chan->lock.
 */
static void
xilinx_ai_layout_formatter_free_descriptors_locked(struct xilinx_ai_layout_formatter_chan *chan)
{
	xilinx_ai_layout_formatter_free_desc_list(chan, &chan->pending_list);
	xilinx_ai_layout_formatter_free_desc_list(chan, &chan->done_list);
	kfree(chan->active_desc);

	chan->active_desc = NULL;
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);
}

/**
 * xilinx_ai_layout_formatter_free_descriptors - Free channel descriptors
 * @chan: Driver specific dma channel
 */
static void
xilinx_ai_layout_formatter_free_descriptors(struct xilinx_ai_layout_formatter_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_ai_layout_formatter_free_descriptors_locked(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_ai_layout_formatter_free_chan_resources - Free channel resources
 * @dchan: DMA channel
 */
static void xilinx_ai_layout_formatter_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_ai_layout_formatter_chan *chan = to_xilinx_chan(dchan);

	xilinx_ai_layout_formatter_free_descriptors(chan);
}

/**
 * xilinx_ai_layout_formatter_chan_desc_cleanup - Clean channel descriptors
 * @chan: Driver specific dma channel
 */
static void
xilinx_ai_layout_formatter_chan_desc_cleanup(struct xilinx_ai_layout_formatter_chan *chan)
{
	struct xilinx_ai_layout_formatter_tx_descriptor *desc, *next;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	list_for_each_entry_safe(desc, next, &chan->done_list, node) {
		dma_async_tx_callback callback;
		void *callback_param;

		list_del(&desc->node);

		/* Run the link descriptor callback function */
		callback = desc->async_tx.callback;
		callback_param = desc->async_tx.callback_param;
		if (callback) {
			spin_unlock_irqrestore(&chan->lock, flags);
			callback(callback_param);
			spin_lock_irqsave(&chan->lock, flags);
		}

		/* Run any dependencies, then free the descriptor */
		dma_run_dependencies(&desc->async_tx);
		kfree(desc);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_ai_layout_formatter_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_ai_layout_formatter_alloc_chan_resources(struct dma_chan *dchan)
{
	dma_cookie_init(dchan);

	return 0;
}

/**
 * xilinx_ai_layout_formatter_tx_status - Get ai_layout_formatter transaction status
 * @dchan: DMA channel
 * @cookie: Transaction identifier
 * @txstate: Transaction state
 *
 * Return: DMA transaction status from dma_cookie_status()
 */
static enum dma_status xilinx_ai_layout_formatter_tx_status(struct dma_chan *dchan,
							    dma_cookie_t cookie,
							    struct dma_tx_state *txstate)
{
	return dma_cookie_status(dchan, cookie, txstate);
}

/**
 * xilinx_ai_layout_formatter_halt - Halt ai_layout_formatter channel
 * @chan: Driver specific dma channel
 *
 * Context: Must be called with @chan->lock held whenever the threaded IRQ may
 * run start_transfer(), which performs read-modify-write cycles on the same
 * CTRL register.
 */
static void xilinx_ai_layout_formatter_halt(struct xilinx_ai_layout_formatter_chan *chan)
{
	ai_layout_formatter_clr(chan, XILINX_AI_LAYOUT_FORMATTER_CTRL_OFFSET,
				XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_START | chan->mode);
	chan->idle = true;
}

/**
 * xilinx_ai_layout_formatter_start - Start dma channel
 * @chan: Driver specific dma channel
 */
static void xilinx_ai_layout_formatter_start(struct xilinx_ai_layout_formatter_chan *chan)
{
	ai_layout_formatter_set(chan, XILINX_AI_LAYOUT_FORMATTER_CTRL_OFFSET,
				XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_START | chan->mode);
	chan->idle = false;
}

/**
 * ai_layout_formatter_wait_ap_idle - Wait for AP_IDLE after stopping the core
 * @chan: Channel
 *
 * With XILINX_VID_DMA_AUTO_RESTART, the core can re-arm as soon as AP_DONE asserts. Software
 * must clear AP_START and XILINX_VID_DMA_AUTO_RESTART, then observe AP_IDLE, before rewriting
 * buffer address registers to avoid torn frames.
 */
static void ai_layout_formatter_wait_ap_idle(struct xilinx_ai_layout_formatter_chan *chan)
{
	unsigned int count;

	for (count = 0; count < XILINX_AI_LAYOUT_FORMATTER_WAIT_FOR_AP_IDLE; count++) {
		if (ai_layout_formatter_read(chan, XILINX_AI_LAYOUT_FORMATTER_CTRL_OFFSET) &
		    XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_IDLE)
			return;
		usleep_range(1, 4);
	}

	dev_warn_ratelimited(chan->xdev->dev,
			     "AI layout formatter: timed out waiting for AP_IDLE; buffer may tear\n");
}

/**
 * xilinx_ai_layout_formatter_complete_descriptor - Mark the active descriptor as complete
 * @chan: xilinx ai_layout_formatter channel
 * This function is invoked with spinlock held
 *
 * CONTEXT: IRQ thread (same lock ordering as former tasklet path)
 */
static void
xilinx_ai_layout_formatter_complete_descriptor(struct xilinx_ai_layout_formatter_chan *chan)
{
	struct xilinx_ai_layout_formatter_tx_descriptor *desc = chan->active_desc;

	dma_cookie_complete(&desc->async_tx);
	list_add_tail(&desc->node, &chan->done_list);
}

/**
 * xilinx_ai_layout_formatter_start_transfer - Starts ai_layout_formatter transfer
 * @chan: Driver specific channel struct pointer
 *
 * Requires @chan->vid_fmt (set via the exported video-format configuration path)
 * before the first descriptor can run. Without it, pending work is completed on
 * the done queue with an error so the channel does not stall.
 */
static void xilinx_ai_layout_formatter_start_transfer(struct xilinx_ai_layout_formatter_chan *chan)
{
	struct xilinx_ai_layout_formatter_tx_descriptor *desc;

	if (!chan->idle)
		return;

	if (list_empty(&chan->pending_list))
		return;

	desc = list_first_entry(&chan->pending_list,
				struct xilinx_ai_layout_formatter_tx_descriptor,
				node);

	if (unlikely(!chan->vid_fmt)) {
		WARN_ONCE(1, "%s: DMA start without video format configuration\n",
			  dev_name(chan->xdev->dev));
		dev_err(chan->xdev->dev,
			"program a video format before dma_async_issue_pending()\n");
		list_del(&desc->node);
		dma_cookie_complete(&desc->async_tx);
		list_add_tail(&desc->node, &chan->done_list);
		return;
	}

	if (chan->mode == XILINX_VID_DMA_AUTO_RESTART) {
		ai_layout_formatter_clr(chan, XILINX_AI_LAYOUT_FORMATTER_CTRL_OFFSET,
					XILINX_AI_LAYOUT_FORMATTER_CTRL_AP_START | chan->mode);
		ai_layout_formatter_wait_ap_idle(chan);
	}

	/* Start the transfer */
	if (chan->vid_fmt->layout_format == XILINX_AI_LAYOUT_FORMATTER_LAYOUT_NCHW) {
		chan->write_addr(chan,
				 XILINX_AI_LAYOUT_FORMATTER_ADDR2_OFFSET,
				 desc->hw.channel_plane_addr[0]);
		chan->write_addr(chan,
				 XILINX_AI_LAYOUT_FORMATTER_ADDR3_OFFSET,
				 desc->hw.channel_plane_addr[1]);
		chan->write_addr(chan,
				 XILINX_AI_LAYOUT_FORMATTER_ADDR4_OFFSET,
				 desc->hw.channel_plane_addr[2]);
		chan->write_addr(chan,
				 XILINX_AI_LAYOUT_FORMATTER_ADDR5_OFFSET,
				 desc->hw.channel_plane_addr[3]);
	} else {
		chan->write_addr(chan,
				 XILINX_AI_LAYOUT_FORMATTER_ADDR_OFFSET,
				 desc->hw.channel_plane_addr[0]);
	}

	/* HW expects these parameters to be same for one transaction */
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_WIDTH_OFFSET, desc->hw.hsize);
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_HEIGHT_OFFSET, desc->hw.vsize);

	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_LAYOUT_FORMAT_OFFSET,
				  chan->vid_fmt->layout_format);
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_DATA_TYPE_OFFSET,
				  chan->vid_fmt->data_type);
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_CHANNELS_OUT_OFFSET,
				  chan->vid_fmt->channels_out);

	/* Start the hardware */
	xilinx_ai_layout_formatter_start(chan);
	list_del(&desc->node);

	chan->active_desc = desc;
}

/**
 * xilinx_ai_layout_formatter_issue_pending - Issue pending transactions
 * @dchan: DMA channel
 */
static void xilinx_ai_layout_formatter_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_ai_layout_formatter_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_ai_layout_formatter_start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_ai_layout_formatter_reset - Reset ai_layout_formatter channel
 * @chan: Driver specific dma channel
 */
static void xilinx_ai_layout_formatter_reset(struct xilinx_ai_layout_formatter_chan *chan)
{
	/* Reset ip */
	if (gpiod_cansleep(chan->xdev->rst_gpio)) {
		gpiod_set_value_cansleep(chan->xdev->rst_gpio, 1);
		udelay(XILINX_AI_LAYOUT_FORMATTER_RESET_PULSE_US);
		gpiod_set_value_cansleep(chan->xdev->rst_gpio, 0);
	} else {
		gpiod_set_value(chan->xdev->rst_gpio, 1);
		udelay(XILINX_AI_LAYOUT_FORMATTER_RESET_PULSE_US);
		gpiod_set_value(chan->xdev->rst_gpio, 0);
	}
}

/**
 * xilinx_ai_layout_formatter_chan_reset - Reset ai_layout_formatter channel and enable interrupts
 * @chan: Driver specific ai_layout_formatter channel
 */
static void xilinx_ai_layout_formatter_chan_reset(struct xilinx_ai_layout_formatter_chan *chan)
{
	xilinx_ai_layout_formatter_reset(chan);
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_IE_OFFSET,
				  XILINX_AI_LAYOUT_FORMATTER_IE_AP_DONE);
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_GIE_OFFSET,
				  XILINX_AI_LAYOUT_FORMATTER_GIE_EN);
}

static irqreturn_t xilinx_ai_layout_formatter_irq_handler(int irq, void *data)
{
	struct xilinx_ai_layout_formatter_chan *chan = data;
	u32 status;

	status = ai_layout_formatter_read(chan, XILINX_AI_LAYOUT_FORMATTER_ISR_OFFSET);
	if (!(status & XILINX_AI_LAYOUT_FORMATTER_ISR_ALL_IRQ_MASK))
		return IRQ_NONE;

	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_ISR_OFFSET,
				  status & XILINX_AI_LAYOUT_FORMATTER_ISR_ALL_IRQ_MASK);
	chan->irq_status = status;
	return IRQ_WAKE_THREAD;
}

static irqreturn_t xilinx_ai_layout_formatter_irq_thread_fn(int irq, void *data)
{
	struct xilinx_ai_layout_formatter_chan *chan = data;
	u32 status = chan->irq_status;

	if (status & XILINX_AI_LAYOUT_FORMATTER_ISR_AP_DONE_IRQ) {
		spin_lock(&chan->lock);
		chan->idle = true;
		if (chan->active_desc) {
			xilinx_ai_layout_formatter_complete_descriptor(chan);
			chan->active_desc = NULL;
		}

		xilinx_ai_layout_formatter_start_transfer(chan);
		spin_unlock(&chan->lock);
	}

	xilinx_ai_layout_formatter_chan_desc_cleanup(chan);
	return IRQ_HANDLED;
}

/**
 * xilinx_ai_layout_formatter_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor
 *
 * Return: cookie value on success and failure value on error
 */
static dma_cookie_t xilinx_ai_layout_formatter_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_ai_layout_formatter_tx_descriptor *desc = to_dma_tx_descriptor(tx);
	struct xilinx_ai_layout_formatter_chan *chan = to_xilinx_chan(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	cookie = dma_cookie_assign(tx);
	list_add_tail(&desc->node, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * xilinx_ai_layout_formatter_dma_prep_interleaved - prepare a descriptor for a
 *	DMA_SLAVE transaction
 * @dchan: DMA channel
 * @xt: Interleaved template pointer
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xilinx_ai_layout_formatter_dma_prep_interleaved(struct dma_chan *dchan,
						struct dma_interleaved_template *xt,
						unsigned long flags)
{
	struct xilinx_ai_layout_formatter_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_ai_layout_formatter_tx_descriptor *desc;
	struct xilinx_ai_layout_formatter_desc_hw *hw;
	u32 vsize, hsize;

	if (chan->direction != xt->dir || !chan->vid_fmt)
		goto error;

	if (!xt->numf || !xt->sgl[0].size)
		goto error;

	if (xt->frame_size != chan->vid_fmt->num_planes)
		goto error;

	vsize = xt->numf;
	hsize = (xt->sgl[0].size * chan->vid_fmt->ppw * BITS_PER_BYTE) /
		chan->vid_fmt->bpw;
	/*
	 * Hardware requires even horizontal size; round up if the derived
	 * width ends up odd due to bytes-per-line to pixels conversion.
	 */
	hsize = ALIGN(hsize, 2);

	if (vsize > chan->xdev->max_height || hsize > chan->xdev->max_width) {
		dev_dbg(chan->xdev->dev,
			"vsize %d max vsize %d hsize %d max hsize %d\n",
			vsize, chan->xdev->max_height, hsize,
			chan->xdev->max_width);
		dev_err(chan->xdev->dev, "Requested size not supported!\n");
		goto error;
	}

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_ai_layout_formatter_tx_submit;

	hw = &desc->hw;
	hw->vsize = xt->numf;
	hw->stride = xt->sgl[0].icg + xt->sgl[0].size;
	hw->hsize = hsize;

	hw->channel_plane_addr[0] = xt->dst_start;
	if (xt->frame_size >= 2 &&
	    xt->frame_size <= XILINX_AI_LAYOUT_FORMATTER_MAX_PLANES)
		hw->channel_plane_addr[1] = xt->dst_start + xt->numf * hw->stride +
					xt->sgl[0].dst_icg;
	if (xt->frame_size >= 3 &&
	    xt->frame_size <= XILINX_AI_LAYOUT_FORMATTER_MAX_PLANES)
		hw->channel_plane_addr[2] = hw->channel_plane_addr[1] + xt->numf * hw->stride +
					xt->sgl[0].dst_icg;
	if (xt->frame_size == XILINX_AI_LAYOUT_FORMATTER_MAX_PLANES)
		hw->channel_plane_addr[3] = hw->channel_plane_addr[2] + xt->numf * hw->stride +
					xt->sgl[0].dst_icg;

	return &desc->async_tx;

error:
	dev_err(chan->xdev->dev,
		"Invalid dma template or missing dma video fmt config\n");
	return NULL;
}

/**
 * xilinx_ai_layout_formatter_terminate_all - Halt the channel and free descriptors
 * @dchan: Driver specific dma channel pointer
 *
 * Halt and descriptor teardown run under @chan->lock so CTRL register RMW does
 * not interleave with the threaded IRQ path. synchronize_irq() runs before the
 * sleeping flush poll so any in-flight handler has finished.
 *
 * Return: 0 on success, %-ETIMEDOUT if the flush-done bit was not observed within
 *	   the bounded poll (hardware may be stuck); the channel is still reset
 *	   afterward as best-effort recovery.
 */
static int xilinx_ai_layout_formatter_terminate_all(struct dma_chan *dchan)
{
	struct xilinx_ai_layout_formatter_chan *chan = to_xilinx_chan(dchan);
	void __iomem *ctrl_regs = chan->xdev->regs + XILINX_AI_LAYOUT_FORMATTER_CTRL_OFFSET;
	unsigned long flags;
	u32 ctrl;
	int ret;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_ai_layout_formatter_halt(chan);
	xilinx_ai_layout_formatter_free_descriptors_locked(chan);
	spin_unlock_irqrestore(&chan->lock, flags);

	synchronize_irq(chan->irq);

	/* worst case frame-to-frame boundary; ensure frame output complete */
	msleep(XILINX_AI_LAYOUT_FORMATTER_FRAME_BOUNDARY_MS);

	/*
	 * Flush the ai_layout_formatter FIFO and
	 * wait for flush done (bounded poll).
	 */
	spin_lock_irqsave(&chan->lock, flags);
	ai_layout_formatter_set(chan, XILINX_AI_LAYOUT_FORMATTER_CTRL_OFFSET,
				XILINX_AI_LAYOUT_FORMATTER_CTRL_FLUSH);
	spin_unlock_irqrestore(&chan->lock, flags);
	ret = readl_poll_timeout(ctrl_regs, ctrl,
				 ctrl & XILINX_AI_LAYOUT_FORMATTER_CTRL_FLUSH_DONE,
				 XILINX_AI_LAYOUT_FORMATTER_FLUSH_SLEEP_MAX_US,
				 XILINX_AI_LAYOUT_FORMATTER_FLUSH_TIMEOUT_US);
	if (ret)
		dev_err(chan->xdev->dev, "AI_Layout_Formatter Flush not done!\n");

	xilinx_ai_layout_formatter_chan_reset(chan);

	return ret;
}

/**
 * xilinx_ai_layout_formatter_synchronize - Wait for IRQ thread to finish
 * @dchan: Driver specific dma channel pointer
 */
static void xilinx_ai_layout_formatter_synchronize(struct dma_chan *dchan)
{
	struct xilinx_ai_layout_formatter_chan *chan = to_xilinx_chan(dchan);

	synchronize_irq(chan->irq);
}

/* -----------------------------------------------------------------------------
 * Probe and remove
 */

/**
 * xilinx_ai_layout_formatter_chan_remove - Per Channel remove function
 * @chan: Driver specific dma channel
 */
static void xilinx_ai_layout_formatter_chan_remove(struct xilinx_ai_layout_formatter_chan *chan)
{
	/* Disable all interrupts */
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_GIE_OFFSET, 0);
	ai_layout_formatter_clr(chan, XILINX_AI_LAYOUT_FORMATTER_IE_OFFSET,
				XILINX_AI_LAYOUT_FORMATTER_ISR_ALL_IRQ_MASK);

	synchronize_irq(chan->irq);
	list_del(&chan->common.device_node);
}

/**
 * xilinx_ai_layout_formatter_chan_probe - Per-channel setup
 * Reads channel parameters from firmware (device properties) and initializes
 * per-channel IRQ, addressing, and DMA channel bookkeeping. Hardware reset and
 * interrupt enables are deferred to xilinx_ai_layout_formatter_chan_reset() from
 * xilinx_ai_layout_formatter_probe() after dma_async_device_register() succeeds.
 *
 * @xdev: Driver specific device structure
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_ai_layout_formatter_chan_probe(struct xilinx_ai_layout_formatter_device *xdev)
{
	struct xilinx_ai_layout_formatter_chan *chan;
	u32 dma_addr_size;
	int err;

	chan = &xdev->chan;

	chan->dev = xdev->dev;
	chan->xdev = xdev;
	chan->idle = true;
	chan->mode = XILINX_VID_DMA_AUTO_RESTART;

	err = device_property_read_u32(xdev->dev, "xlnx,dma-addr-width", &dma_addr_size);
	if (err) {
		dev_err(xdev->dev, "missing or invalid addr width dts prop\n");
		return err;
	}
	if (dma_addr_size != XILINX_AI_LAYOUT_FORMATTER_DMA_ADDR_WIDTH_32 &&
	    dma_addr_size != XILINX_AI_LAYOUT_FORMATTER_DMA_ADDR_WIDTH_64) {
		dev_err(xdev->dev, "missing or invalid addr width dts prop\n");
		return -EINVAL;
	}

	if (sizeof(dma_addr_t) != sizeof(u64) &&
	    dma_addr_size == XILINX_AI_LAYOUT_FORMATTER_DMA_ADDR_WIDTH_64)
		return dev_err_probe(xdev->dev, -EINVAL,
				     "device needs 32-bit DMA but kernel uses 64-bit\n");

	if (dma_addr_size == XILINX_AI_LAYOUT_FORMATTER_DMA_ADDR_WIDTH_64)
		chan->write_addr = writeq_addr;
	else
		chan->write_addr = write_addr;

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);

	chan->irq = platform_get_irq(to_platform_device(xdev->dev), 0);
	if (chan->irq < 0)
		return chan->irq;

	err = devm_request_threaded_irq(xdev->dev, chan->irq,
					xilinx_ai_layout_formatter_irq_handler,
					xilinx_ai_layout_formatter_irq_thread_fn,
					IRQF_ONESHOT | IRQF_SHARED,
					"xilinx_ai_layout_formatter", chan);
	if (err) {
		dev_err(xdev->dev, "unable to request IRQ %d\n", chan->irq);
		return err;
	}

	/*
	 * Hook the IRQ line but keep IE/GIE off until dma_async_device_register()
	 * succeeds; otherwise a shared IRQ during probe teardown could run the
	 * threaded handler before the DMA engine is fully initialized.
	 */
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_IE_OFFSET, 0);
	ai_layout_formatter_write(chan, XILINX_AI_LAYOUT_FORMATTER_GIE_OFFSET, 0);

	/*
	 * Initialize the DMA channel and add it to the DMA engine channels
	 * list.
	 */
	chan->common.device = &xdev->common;

	list_add_tail(&chan->common.device_node, &xdev->common.channels);

	return 0;
}

/**
 * xilinx_ai_layout_formatter_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_ai_layout_formatter_probe(struct platform_device *pdev)
{
	const char *vid_fmts[ARRAY_SIZE(xilinx_ai_layout_formatter_formats)];
	struct xilinx_ai_layout_formatter_device *xdev;
	int hw_vid_fmt_cnt;
	u32 align;
	int i, j;
	int err;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;

	xdev->cfg = device_get_match_data(&pdev->dev);
	if (!xdev->cfg)
		return -ENODEV;

	xdev->ap_clk = devm_clk_get_enabled(xdev->dev, "ap_clk");
	if (IS_ERR(xdev->ap_clk))
		return dev_err_probe(xdev->dev, PTR_ERR(xdev->ap_clk),
				     "failed to get ap_clk\n");

	xdev->rst_gpio = devm_gpiod_get(&pdev->dev, "reset",
					GPIOD_OUT_HIGH);
	if (IS_ERR(xdev->rst_gpio))
		return dev_err_probe(xdev->dev, PTR_ERR(xdev->rst_gpio),
				     "Unable to get reset GPIO\n");

	gpiod_set_value_cansleep(xdev->rst_gpio, 0);

	xdev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xdev->regs))
		return PTR_ERR(xdev->regs);

	err = device_property_read_u32(xdev->dev, "xlnx,max-height", &xdev->max_height);
	if (err) {
		dev_err(xdev->dev, "xlnx,max-height is missing or invalid (%pe)\n", ERR_PTR(err));
		return err;
	}
	if (xdev->max_height > XILINX_AI_LAYOUT_FORMATTER_MAX_HEIGHT ||
	    xdev->max_height < XILINX_AI_LAYOUT_FORMATTER_MIN_HEIGHT) {
		dev_err(xdev->dev, "Invalid height in dt\n");
		return -EINVAL;
	}

	err = device_property_read_u32(xdev->dev, "xlnx,max-width", &xdev->max_width);
	if (err) {
		dev_err(xdev->dev, "xlnx,max-width is missing or invalid (%pe)\n", ERR_PTR(err));
		return err;
	}
	if (xdev->max_width > XILINX_AI_LAYOUT_FORMATTER_MAX_WIDTH ||
	    xdev->max_width < XILINX_AI_LAYOUT_FORMATTER_MIN_WIDTH) {
		dev_err(xdev->dev, "Invalid width in dt\n");
		return -EINVAL;
	}

	/* Initialize the DMA engine */
	err = device_property_read_u32(xdev->dev, "xlnx,pixels-per-clock", &xdev->ppc);
	if (err) {
		dev_err(xdev->dev, "missing or invalid pixels per clock dts prop\n");
		return err;
	}
	if (!is_power_of_2(xdev->ppc) || xdev->ppc > 8) {
		dev_err(xdev->dev, "missing or invalid pixels per clock dts prop\n");
		return -EINVAL;
	}

	err = device_property_read_u32(xdev->dev, "xlnx,dma-align", &align);
	if (err)
		align = xdev->ppc * XILINX_AI_LAYOUT_FORMATTER_ALIGN_MUL;

	if (!align || align < (xdev->ppc * XILINX_AI_LAYOUT_FORMATTER_ALIGN_MUL) ||
	    ffs(align) != fls(align)) {
		dev_err(xdev->dev, "invalid dma align dts prop\n");
		return -EINVAL;
	}

	xdev->common.copy_align = fls(align) - 1;
	xdev->common.dev = &pdev->dev;

	INIT_LIST_HEAD(&xdev->common.channels);
	dma_cap_set(DMA_SLAVE, xdev->common.cap_mask);
	dma_cap_set(DMA_PRIVATE, xdev->common.cap_mask);

	/* Initialize the channels */
	err = xilinx_ai_layout_formatter_chan_probe(xdev);
	if (err < 0)
		return err;

	xdev->chan.direction = xdev->cfg->direction;
	/* This IP is device-to-memory only; extend if a variant adds other directions. */
	xdev->common.directions = BIT(DMA_DEV_TO_MEM);

	/* read supported video formats and update internal table */
	hw_vid_fmt_cnt = device_property_string_array_count(xdev->dev, "xlnx,vid-formats");
	if (hw_vid_fmt_cnt < 0) {
		err = hw_vid_fmt_cnt;
		dev_err(xdev->dev, "missing or invalid xlnx,vid-formats dts prop\n");
		goto remove_chan;
	}

	err = device_property_read_string_array(xdev->dev, "xlnx,vid-formats",
						vid_fmts, hw_vid_fmt_cnt);
	if (err < 0) {
		dev_err(xdev->dev, "Missing or invalid xlnx,vid-formats dts prop\n");
		goto remove_chan;
	}

	for (i = 0; i < hw_vid_fmt_cnt; i++) {
		const char *vid_fmt_name = vid_fmts[i];

		for (j = 0; j < ARRAY_SIZE(xilinx_ai_layout_formatter_formats); j++) {
			const char *dts_name =
				xilinx_ai_layout_formatter_formats[j].dts_name;

			if (strcmp(vid_fmt_name, dts_name))
				continue;

			xdev->enabled_vid_fmts |=
				xilinx_ai_layout_formatter_formats[j].layout_fmt_bitmask;
		}
	}

	/* Determine supported vid framework formats */
	ai_layout_formatter_init_format_array(xdev);

	xdev->common.device_alloc_chan_resources =
				xilinx_ai_layout_formatter_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
				xilinx_ai_layout_formatter_free_chan_resources;
	xdev->common.device_prep_interleaved_dma =
				xilinx_ai_layout_formatter_dma_prep_interleaved;
	xdev->common.device_terminate_all = xilinx_ai_layout_formatter_terminate_all;
	xdev->common.device_synchronize = xilinx_ai_layout_formatter_synchronize;
	xdev->common.device_tx_status = xilinx_ai_layout_formatter_tx_status;
	xdev->common.device_issue_pending = xilinx_ai_layout_formatter_issue_pending;

	platform_set_drvdata(pdev, xdev);

	/* Register the DMA engine with the core */
	err = dma_async_device_register(&xdev->common);
	if (err) {
		dev_err(xdev->dev, "failed to register DMA async device (%d)\n", err);
		goto remove_chan;
	}

	xilinx_ai_layout_formatter_chan_reset(&xdev->chan);

	err = of_dma_controller_register(dev_of_node(xdev->dev),
					 of_dma_xilinx_xlate, xdev);
	if (err < 0) {
		dev_err(xdev->dev, "Unable to register DMA to DT\n");
		goto unregister_dma;
	}

	dev_dbg(xdev->dev, "Xilinx AXI AI_Layout_Formatter engine probed\n");

	return 0;
unregister_dma:
	dma_async_device_unregister(&xdev->common);
remove_chan:
	xilinx_ai_layout_formatter_chan_remove(&xdev->chan);
	return err;
}

/**
 * xilinx_ai_layout_formatter_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 */
static void xilinx_ai_layout_formatter_remove(struct platform_device *pdev)
{
	struct xilinx_ai_layout_formatter_device *xdev = platform_get_drvdata(pdev);

	of_dma_controller_free(dev_of_node(&pdev->dev));

	dma_async_device_unregister(&xdev->common);
	xilinx_ai_layout_formatter_chan_remove(&xdev->chan);
}

static const struct of_device_id xilinx_ai_layout_formatter_of_ids[] = {
	{
		.compatible = "xlnx,ai-layout-formatter-wr-v1",
		.data = &xlnx_ai_layout_formatter_cfg_v1,
	},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xilinx_ai_layout_formatter_of_ids);

static struct platform_driver xilinx_ai_layout_formatter_driver = {
	.driver = {
		.name = XILINX_AI_LAYOUT_FORMATTER_DRIVER_NAME,
		.of_match_table = xilinx_ai_layout_formatter_of_ids,
	},
	.probe = xilinx_ai_layout_formatter_probe,
	.remove = xilinx_ai_layout_formatter_remove,
};

/**
 * xilinx_ai_layout_formatter_is_dma_device - True if @chan belongs to this driver
 * @chan: DMA channel to test
 *
 * Compares struct device::driver against this module's struct device_driver so
 * the check does not depend on driver name strings and does not dereference
 * driver->name when driver is NULL.
 */
bool xilinx_ai_layout_formatter_is_dma_device(const struct dma_chan *chan)
{
	struct device *dev;

	if (!chan || !chan->device)
		return false;

	dev = chan->device->dev;
	if (!dev || !dev->driver)
		return false;

	return dev->driver == &xilinx_ai_layout_formatter_driver.driver;
}
EXPORT_SYMBOL_GPL(xilinx_ai_layout_formatter_is_dma_device);

module_platform_driver(xilinx_ai_layout_formatter_driver);

MODULE_AUTHOR("Mounik Katikala <mounik.katikala@amd.com>");
MODULE_DESCRIPTION("Xilinx AI_Layout_Formatter driver");
MODULE_LICENSE("GPL");

/*
 * Xilinx HLS Scaler
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2017 Xilinx, Inc.
 *
 * Contacts: Radhey Shyam Pandey <radheys@xilinx.com>
 *           Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
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

#include <linux/device.h>
#include <linux/fixp-arith.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>
#include <linux/delay.h>
#include "xilinx-vip.h"
#include "xilinx-hscaler-hw.h"
#include "xilinx-vscaler-hw.h"
#include "xilinx-scaler-coeff.h"

#define XSCALER_MIN_WIDTH			(32)
#define XSCALER_MAX_WIDTH			(4096)
#define XSCALER_MIN_HEIGHT			(32)
#define XSCALER_MAX_HEIGHT			(4096)

/* Modify to defaults incase it is not configured from application */
#define XSCALER_DEF_IN_HEIGHT			(720)
#define XSCALER_DEF_IN_WIDTH			(1280)
#define XSCALER_DEF_OUT_HEIGHT			(1080)
#define XSCALER_DEF_OUT_WIDTH			(1920)

#define XSCALER_HSF				(0x0100)
#define XSCALER_VSF				(0x0104)
#define XSCALER_SF_SHIFT			(20)
#define XSCALER_SF_MASK				(0xffffff)
#define XSCALER_SOURCE_SIZE			(0x0108)
#define XSCALER_SIZE_HORZ_SHIFT			(0)
#define XSCALER_SIZE_VERT_SHIFT			(16)
#define XSCALER_SIZE_MASK			(0xfff)
#define XSCALER_HAPERTURE			(0x010c)
#define XSCALER_VAPERTURE			(0x0110)
#define XSCALER_APERTURE_START_SHIFT		(0)
#define XSCALER_APERTURE_END_SHIFT		(16)
#define XSCALER_OUTPUT_SIZE			(0x0114)
#define XSCALER_COEF_DATA_IN			(0x0134)
#define XSCALER_COEF_DATA_IN_SHIFT		(16)

/* Video subsytems block offset */
#define S_AXIS_RESET_OFF			(0x00010000)
#define V_HSCALER_OFF				(0x00000000)
#define V_VSCALER_OFF				(0x00020000)

/* HW Reset Network GPIO Channel */
#define GPIO_CH_RESET_SEL					(1)
#define RESET_MASK_VIDEO_IN					(0x01)
#define RESET_MASK_IP_AXIS					(0x02)
#define RESET_MASK_IP_AXIMM					(0x01)
#define RESET_MASK_ALL_BLOCKS		(RESET_MASK_VIDEO_IN  | \
						RESET_MASK_IP_AXIS)
#define XGPIO_DATA_OFFSET					(0x0)
#define XGPIO_TRI_OFFSET					(0x4)
#define XGPIO_DATA2_OFFSET					(0x8)
#define XGPIO_TRI2_OFFSET					(0xC)

#define XGPIO_GIE_OFFSET			(0x11C)
#define XGPIO_ISR_OFFSET			(0x120)
#define XGPIO_IER_OFFSET			(0x128)
#define XGPIO_CHAN_OFFSET			(8)
#define STEP_PRECISION				(65536)

/* Video IP Formats */
#define XVIDC_CSF_RGB				(0)
#define XVIDC_CSF_YCRCB_444			(1)
#define XVIDC_CSF_YCRCB_422			(2)
#define XVIDC_CSF_YCRCB_420			(3)

/* Mask definitions for Low and high 16 bits in a 32 bit number */
#define XHSC_MASK_LOW_16BITS			(0x0000FFFF)
#define XHSC_MASK_HIGH_16BITS			(0xFFFF0000)
#define STEP_PRECISION_SHIFT			(16)

/**
 * struct xscaler_device - Xilinx Scaler device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: V4L2 media bus formats at the sink and source pads
 * @default_formats: default V4L2 media bus formats
 * @vip_format: Xilinx Video IP format
 * @crop: Active crop rectangle for the sink pad
 * @num_hori_taps: number of vertical taps
 * @num_vert_taps: number of vertical taps
 * @max_num_phases: maximum number of phases
 * @separate_yc_coef: separate coefficients for Luma(y) and Chroma(c)
 * @separate_hv_coef: separate coefficients for Horizontal(h) and Vertical(v)
 */
struct xscaler_device {
	struct xvip_device xvip;

	struct media_pad pads[2];

	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];
	const struct xvip_video_format *vip_formats[2];

	u32 num_hori_taps;
	u32 num_vert_taps;
	u32 max_num_phases;
	u32 pix_per_clk;
	u32 max_pixels;
	u32 max_lines;
	bool separate_yc_coef;
	bool separate_hv_coef;
	u64 phasesH[XV_HSCALER_MAX_LINE_WIDTH];
	short hscaler_coeff[XV_HSCALER_MAX_H_PHASES][XV_HSCALER_MAX_H_TAPS];
	short vscaler_coeff[XV_VSCALER_MAX_V_PHASES][XV_VSCALER_MAX_V_TAPS];
};

static inline struct xscaler_device *to_scaler(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xscaler_device, xvip.subdev);
}

static void calculate_phases(struct xscaler_device *xscaler, u32 width_in,
			    u32 width_out, u32 pixel_rate)
{
	int loop_width;
	int x, s;
	int offset = 0;
	int xwrite_pos = 0;
	u64 output_write_en;
	int get_new_pix;
	u64 phaseH;
	u64 array_idx;
	int xread_pos = 0;
	int nr_rds = 0;
	int nr_rds_clck = 0;
	int nphases = xscaler->max_num_phases;
	int nppc = xscaler->pix_per_clk;
	int shift = STEP_PRECISION_SHIFT - ilog2(nphases);

	loop_width = ((width_in > width_out) ? width_in + (nppc-1) :
		     width_out  + (nppc-1))/nppc;
	array_idx = 0;
	for (x = 0; x < loop_width; x++) {
		xscaler->phasesH[x] = 0;
		nr_rds_clck = 0;
		for (s = 0; s < nppc; s++) {
			phaseH = (offset >> shift) & (nphases - 1);
			get_new_pix = 0;
			output_write_en = 0;
			if ((offset >> STEP_PRECISION_SHIFT) != 0) {
				/* read a new input sample */
				get_new_pix = 1;
				offset = offset - (1<<STEP_PRECISION_SHIFT);
				output_write_en = 0;
				array_idx++;
				xread_pos++;
			}
			if (((offset >> STEP_PRECISION_SHIFT) == 0) &&
			    (xwrite_pos < width_out)) {
				/* produce a new output sample */
				offset += pixel_rate;
				output_write_en = 1;
				xwrite_pos++;
			}

			xscaler->phasesH[x] = xscaler->phasesH[x] |
			  (phaseH << (s*9));
			xscaler->phasesH[x] = xscaler->phasesH[x] |
			  ((array_idx << 6) + (s*9));
			xscaler->phasesH[x] = xscaler->phasesH[x] |
			  ((output_write_en << 8) + (s*9));

			if (get_new_pix)
				nr_rds_clck++;
		}
		if (array_idx >= nppc)
			array_idx &= (nppc-1);

		nr_rds += nr_rds_clck;
		if (nr_rds >= nppc)
			nr_rds -= nppc;
	}
}

static void
xv_hscaler_load_ext_coeff(struct xscaler_device *xscaler,
			const short *coeff, int ntaps)
{
	int i, j, pad, offset;
	int  nphases = xscaler->max_num_phases;
	/*
	 * @TO : validate input arguments
	 */

	switch (ntaps) {
	case XV_HSCALER_TAPS_6:
	case XV_HSCALER_TAPS_8:
	case XV_HSCALER_TAPS_10:
	case XV_HSCALER_TAPS_12:
	break;

	default:
		dev_err(xscaler->xvip.dev,
		"H Scaler %d Taps not supported\n", ntaps);
	return;
	}

	/* Determine if coefficient needs padding (effective vs. max taps) */
	pad = XV_HSCALER_MAX_H_TAPS - ntaps;
	offset = ((pad) ? (pad>>1) : 0);
	dev_info(xscaler->xvip.dev,
		"Pad = %d Offset = %d Nphases = %d ntaps = %d",
		pad, offset, nphases, ntaps);

	/* Load User defined coefficients into scaler coefficient table */
	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps; ++j)
			xscaler->hscaler_coeff[i][j+offset] = coeff[i*ntaps+j];
	}

	if (pad) { /* effective taps < max_taps */
		for (i = 0; i < nphases; i++) {
			/* pad left */
			for (j = 0; j < offset; j++)
				xscaler->hscaler_coeff[i][j] = 0;
			/* pad right */
			j = ntaps+offset;
			for (; j < XV_HSCALER_MAX_H_TAPS; j++)
				xscaler->hscaler_coeff[i][j] = 0;
		}
	}

}

static void
xv_hscaler_select_coeff(struct xscaler_device *xscaler, u32 width_in,
				  u32 width_out)
{
	const short *coeff;
	u16 hscale_ratio;
	u16 is_scale_down;
	int ntaps;

	/*
	 * @TO : validate input arguments
	 */

	is_scale_down = (width_out < width_in);

	/*
	 * Scale Down Mode will use dynamic filter selection logic
	 * Scale Up Mode (including 1:1) will always use 6 tap filter
	 */
	if (is_scale_down) {
		hscale_ratio = ((width_in * 10)/width_out);

		switch (xscaler->num_hori_taps) {
		case XV_HSCALER_TAPS_6:
			dev_info(xscaler->xvip.dev, "h-scaler : scale down 6 tap");
			coeff = &xhsc_coeff_taps6[0][0];
			ntaps = XV_HSCALER_TAPS_6;
		break;
		case XV_HSCALER_TAPS_8:
			if (hscale_ratio > 15) {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 8 tap");
				coeff = &xhsc_coeff_taps8[0][0];
				ntaps = XV_HSCALER_TAPS_8;
			} else {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 6 tap");
				coeff = &xhsc_coeff_taps6[0][0];
				ntaps = XV_HSCALER_TAPS_6;
			}
		break;
		case XV_HSCALER_TAPS_10:
			if (hscale_ratio > 25) {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 10 tap");
				coeff = &xhsc_coeff_taps10[0][0];
				ntaps = XV_HSCALER_TAPS_10;
			} else if (hscale_ratio > 15) {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 8 tap");
				coeff = &xhsc_coeff_taps8[0][0];
				ntaps = XV_HSCALER_TAPS_8;
			} else {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 6 tap");
				coeff = &xhsc_coeff_taps6[0][0];
				ntaps = XV_HSCALER_TAPS_6;
			}
		break;
		case XV_HSCALER_TAPS_12:
			if (hscale_ratio > 35) {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 12 tap");
				coeff = &xhsc_coeff_taps12[0][0];
				ntaps = XV_HSCALER_TAPS_12;
			} else if (hscale_ratio > 25) {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 10 tap");
				coeff = &xhsc_coeff_taps10[0][0];
				ntaps = XV_HSCALER_TAPS_10;
			} else if (hscale_ratio > 15) {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 8 tap");
				coeff = &xhsc_coeff_taps8[0][0];
				ntaps = XV_HSCALER_TAPS_8;
			} else {
				dev_info(xscaler->xvip.dev, "h-scaler : scale down 6 tap");
				coeff = &xhsc_coeff_taps6[0][0];
				ntaps = XV_HSCALER_TAPS_6;
			}
		break;
		default:
			ntaps = xscaler->num_hori_taps;
			dev_err(xscaler->xvip.dev,
			"H-Scaler %d Taps Not Supported\n", ntaps);
			return;
		}
	} else { /* Scale Up */
		dev_info(xscaler->xvip.dev, "h-scaler : scale up 6 tap");
		coeff = &xhsc_coeff_taps6[0][0];
		ntaps = XV_HSCALER_TAPS_6;
	}

	xv_hscaler_load_ext_coeff(xscaler, coeff, ntaps);

}

static void xv_hscaler_set_coeff(struct xscaler_device *xscaler)
{

	int val, i, j, offset, rdIndx;
	int ntaps = xscaler->num_hori_taps;
	int nphases = xscaler->max_num_phases;
	u32 baseAddr;

	offset = (XV_HSCALER_MAX_H_TAPS - ntaps)/2;
	baseAddr = V_HSCALER_OFF + XV_HSCALER_CTRL_ADDR_HWREG_HFLTCOEFF_BASE;
	dev_info(xscaler->xvip.dev, "Hscaler writing to 0x%x", baseAddr);
	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps/2; j++) {
			rdIndx = j*2+offset;
			val = (xscaler->hscaler_coeff[i][rdIndx+1] << 16) |
			  (xscaler->hscaler_coeff[i][rdIndx] &
			   XHSC_MASK_LOW_16BITS);
			 xvip_write(&xscaler->xvip, baseAddr +
				    ((i*ntaps/2+j)*4), val);
		}
	}
}

static void
xv_vscaler_load_ext_coeff(struct xscaler_device *xscaler,
			const short *coeff, int ntaps)
{
	int i, j, pad, offset;
	int nphases = xscaler->max_num_phases;
	/*
	 * TODO : validate input arguments
	 */

	switch (ntaps) {
	case XV_VSCALER_TAPS_6:
	case XV_VSCALER_TAPS_8:
	case XV_VSCALER_TAPS_10:
	case XV_VSCALER_TAPS_12:
	break;
	default:
		dev_err(xscaler->xvip.dev,
		"V Scaler %d taps not supported.\n", ntaps);
	return;
	}

	/* Determine if coefficient needs padding (effective vs. max taps) */
	pad = XV_VSCALER_MAX_V_TAPS - ntaps;
	offset = ((pad) ? (pad>>1) : 0);

	dev_info(xscaler->xvip.dev,
		"Pad = %d Offset = %d Nphases = %d ntaps = %d",
		pad, offset, nphases, ntaps);

	/* Load User defined coefficients into scaler coefficient table */
	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps; ++j)
			xscaler->vscaler_coeff[i][j+offset] = coeff[i*ntaps+j];
	}

	if (pad) { /* effective taps < max_taps */
		for (i = 0; i < nphases; i++) {
			/* pad left */
			for (j = 0; j < offset; j++)
				xscaler->vscaler_coeff[i][j] = 0;
		}
		/* pad right */
		for (j = (ntaps+offset); j < XV_VSCALER_MAX_V_TAPS; j++)
			xscaler->vscaler_coeff[i][j] = 0;
	}
}

static void xv_vscaler_set_coeff(struct xscaler_device *xscaler)
{
	int nphases = xscaler->max_num_phases;
	int ntaps   = xscaler->num_vert_taps;

	int val, i, j, offset, rdIndx;
	u32 baseAddr;

	offset = (XV_VSCALER_MAX_V_TAPS - ntaps)/2;
	baseAddr = V_VSCALER_OFF + XV_VSCALER_CTRL_ADDR_HWREG_VFLTCOEFF_BASE;
	dev_info(xscaler->xvip.dev, "Vscaler writing to 0x%x", baseAddr);

	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps/2; j++) {
			rdIndx = j*2+offset;
			val = (xscaler->vscaler_coeff[i][rdIndx+1] << 16) |
			  (xscaler->vscaler_coeff[i][rdIndx] &
			   XVSC_MASK_LOW_16BITS);
			xvip_write(&xscaler->xvip, baseAddr+((i*ntaps/2+j)*4),
				   val);
		}
	}
}

static void
xv_vscaler_select_coeff(struct xscaler_device *xscaler, u32 height_in,
				  u32 height_out)
{
	const short *coeff;
	u16 vscale_ratio;
	u16 is_scale_down;
	int ntaps;
	/*
	 * TODO: validates input arguments
	 */
	is_scale_down = (height_out < height_in);
	/* Scale Down Mode will use dynamic filter selection logic
	 * Scale Up Mode (including 1:1) will always use 6 tap filter
	 */


	if (is_scale_down) {
		vscale_ratio = ((height_in * 10) / height_out);

		switch (xscaler->num_vert_taps) {
		case XV_VSCALER_TAPS_6:
			dev_info(xscaler->xvip.dev, "v-scaler : scale down 6 tap");
			coeff = &xvsc_coeff_taps6[0][0];
			ntaps = XV_VSCALER_TAPS_6;
		break;
		case XV_VSCALER_TAPS_8:
			if (vscale_ratio > 15) {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 8 tap");
				coeff = &xvsc_coeff_taps8[0][0];
				ntaps = XV_VSCALER_TAPS_8;
			} else {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 6 tap");
				coeff = &xvsc_coeff_taps6[0][0];
				ntaps = XV_VSCALER_TAPS_6;
			}
		break;
		case XV_VSCALER_TAPS_10:
			if (vscale_ratio > 25) {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 10 tap");
				coeff = &xvsc_coeff_taps10[0][0];
				ntaps = XV_VSCALER_TAPS_10;
			} else if (vscale_ratio > 15) {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 8 tap");
				coeff = &xvsc_coeff_taps8[0][0];
				ntaps = XV_VSCALER_TAPS_8;
			} else {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 6 tap");
				coeff = &xvsc_coeff_taps6[0][0];
				ntaps = XV_VSCALER_TAPS_6;
			}
		break;
		case XV_VSCALER_TAPS_12:
			if (vscale_ratio > 35) {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 12 tap");
				coeff = &xvsc_coeff_taps12[0][0];
				ntaps = XV_VSCALER_TAPS_12;
			} else if (vscale_ratio > 25) {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 10 tap");
				coeff = &xvsc_coeff_taps10[0][0];
				ntaps = XV_VSCALER_TAPS_10;
			} else if (vscale_ratio > 15) {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 8 tap");
				coeff = &xvsc_coeff_taps8[0][0];
				ntaps = XV_VSCALER_TAPS_8;
			} else {
				dev_info(xscaler->xvip.dev, "v-scaler : scale down 6 tap");
				coeff = &xvsc_coeff_taps6[0][0];
				ntaps = XV_VSCALER_TAPS_6;
			}
		break;
		default:
			ntaps = xscaler->num_vert_taps;
			dev_err(xscaler->xvip.dev,
				"ERR: V-Scaler %d Taps Not Supported", ntaps);
			return;
		}
	} else { /* Scale up */
		dev_info(xscaler->xvip.dev, "v-scaler : scale up 6 tap");
		coeff = &xvsc_coeff_taps6[0][0];
		ntaps = XV_VSCALER_TAPS_6;
	}

	xv_vscaler_load_ext_coeff(xscaler, coeff, ntaps);
}

/*
 * V4L2 Subdevice Video Operations
 */

static inline void xv_procss_reset_block(struct xvip_device *xvip, u32 channel,
								u32 ip_block)
{
	u32 val;

	if (xvip) {
		val = xvip_read(xvip, ((channel - 1) * XGPIO_CHAN_OFFSET) +
				XGPIO_DATA_OFFSET + S_AXIS_RESET_OFF);
		val &= ~ip_block;
		xvip_write(xvip, ((channel - 1) * XGPIO_CHAN_OFFSET) +
			   XGPIO_DATA_OFFSET + S_AXIS_RESET_OFF, val);
	}
}

inline void xv_procss_enable_block(struct xvip_device *xvip, u32 channel,
				 u32 ip_block)
{
	u32 val;

	if (xvip) {
		val = xvip_read(xvip, ((channel - 1) * XGPIO_CHAN_OFFSET) +
				XGPIO_DATA_OFFSET + S_AXIS_RESET_OFF);
		val |= ip_block;
		xvip_write(xvip, ((channel - 1) * XGPIO_CHAN_OFFSET) +
			   XGPIO_DATA_OFFSET + S_AXIS_RESET_OFF, val);
	}
}

static inline void xscaler_reset(struct xscaler_device *xscaler)
{
	/* Reset All IP Blocks on AXIS interface*/
	xv_procss_reset_block(&xscaler->xvip, GPIO_CH_RESET_SEL,
			    RESET_MASK_ALL_BLOCKS);
	udelay(100);
	xv_procss_enable_block(&xscaler->xvip, GPIO_CH_RESET_SEL,
			     RESET_MASK_IP_AXIS);
}

static int xv_vscaler_setup_video_fmt
		(struct xscaler_device *xscaler,
				u32 code_in)
{
	int video_in;

	switch (code_in) {
	case MEDIA_BUS_FMT_UYVY8_1_5X8:
		dev_info(xscaler->xvip.dev,
			"Vscaler Input Media Format YUV 420\n");
		video_in = XVIDC_CSF_YCRCB_420;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		dev_info(xscaler->xvip.dev,
			"Vscaler Input Media Format YUV 422\n");
		video_in = XVIDC_CSF_YCRCB_422;
		break;
	case MEDIA_BUS_FMT_VUY8_1X24:
		dev_info(xscaler->xvip.dev,
			"Vscaler Input Media Format YUV 444\n");
		video_in = XVIDC_CSF_YCRCB_444;
		break;
	case MEDIA_BUS_FMT_RBG888_1X24:
		dev_info(xscaler->xvip.dev,
			"Vscaler Input Media Format RGB\n");
		video_in = XVIDC_CSF_RGB;
		break;
	default:
		dev_err(xscaler->xvip.dev,
			"Vscaler Unsupported Input Media Format 0x%x",
			code_in);
		return -EINVAL;
	}
	xvip_write(&xscaler->xvip, V_VSCALER_OFF +
		XV_VSCALER_CTRL_ADDR_HWREG_COLORMODE_DATA,
						(u32)video_in);
	/*
	 * Vscaler will upscale to YUV 422 before
	 * Hscaler starts operation
	 */
	if (video_in == XVIDC_CSF_YCRCB_420)
		return XVIDC_CSF_YCRCB_422;
	return video_in;
}

static int xv_hscaler_setup_video_fmt
	(struct xscaler_device *xscaler, u32 code_out, u32 vsc_out)
{

	u32 video_out;

	switch (vsc_out) {
	case XVIDC_CSF_YCRCB_420:
		dev_info(xscaler->xvip.dev,
		"Hscaler Input Media Format is YUV 420");
		break;

	case XVIDC_CSF_YCRCB_422:
		dev_info(xscaler->xvip.dev,
		"Hscaler Input Media Format is YUV 422");
		break;
	case XVIDC_CSF_YCRCB_444:
		dev_info(xscaler->xvip.dev,
		"Hscaler Input Media Format is YUV 444");
		break;
	case XVIDC_CSF_RGB:
		dev_info(xscaler->xvip.dev,
		"Hscaler Input Media Format is RGB");
		break;
	default:
		dev_err(xscaler->xvip.dev,
		"Hscaler got unsupported format from Vscaler");
		return -EINVAL;
	}

	xvip_write(&xscaler->xvip, V_HSCALER_OFF +
		XV_HSCALER_CTRL_ADDR_HWREG_COLORMODE_DATA,
		vsc_out);

	switch (code_out) {
	case MEDIA_BUS_FMT_UYVY8_1_5X8:
		dev_info(xscaler->xvip.dev,
			"Hscaler Output Media Format YUV 420\n");
		video_out = XVIDC_CSF_YCRCB_420;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		dev_info(xscaler->xvip.dev,
			"Hscaler Output Media Format YUV 422\n");
		video_out = XVIDC_CSF_YCRCB_422;
		break;
	case MEDIA_BUS_FMT_VUY8_1X24:
		dev_info(xscaler->xvip.dev,
			"Hscaler Output Media Format YUV 444\n");
		video_out = XVIDC_CSF_YCRCB_444;
		break;
	case MEDIA_BUS_FMT_RBG888_1X24:
		dev_info(xscaler->xvip.dev,
			"Hscaler Output Media Format YUV 444\n");
		video_out = XVIDC_CSF_RGB;
		break;
	default:
		dev_err(xscaler->xvip.dev,
			"Hscaler Unsupported Output Media Format 0x%x",
			code_out);
		return -EINVAL;
	}
	xvip_write(&xscaler->xvip, V_HSCALER_OFF +
		XV_HSCALER_CTRL_ADDR_HWREG_COLORMODEOUT_DATA,
						video_out);
	return 0;
}

static int xscaler_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	u32 width_in, width_out;
	u32 height_in, height_out;
	u32 code_in, code_out;
	u32 pixel_rate;
	u32 line_rate;
	u32 loop_width;
	u32 offset;
	u32 val, lsb, msb, index, i;
	int rval;

	if (!enable) {
		xscaler_reset(xscaler);
		return 0;
	}

	dev_info(xscaler->xvip.dev, "Stream On");

	/* Get input width / height / media pad format */
	width_in = xscaler->formats[XVIP_PAD_SINK].width;
	height_in = xscaler->formats[XVIP_PAD_SINK].height;
	code_in = xscaler->formats[XVIP_PAD_SINK].code;

	/* Get output width / height / media pad format */
	width_out = xscaler->formats[XVIP_PAD_SOURCE].width;
	height_out = xscaler->formats[XVIP_PAD_SOURCE].height;
	code_out = xscaler->formats[XVIP_PAD_SOURCE].code;


	/* UpScale mode V Scaler is before H Scaler
	 * V-Scaler_setup
	 */
	line_rate = (height_in * STEP_PRECISION) / height_out;

	xv_vscaler_select_coeff(xscaler, height_in, height_out);
	xv_vscaler_set_coeff(xscaler);

	xvip_write(&xscaler->xvip, V_VSCALER_OFF +
		   XV_VSCALER_CTRL_ADDR_HWREG_HEIGHTIN_DATA, height_in);
	xvip_write(&xscaler->xvip, V_VSCALER_OFF +
		   XV_VSCALER_CTRL_ADDR_HWREG_WIDTH_DATA, width_in);
	xvip_write(&xscaler->xvip, V_VSCALER_OFF +
		   XV_VSCALER_CTRL_ADDR_HWREG_HEIGHTOUT_DATA, height_out);
	xvip_write(&xscaler->xvip, V_VSCALER_OFF +
		   XV_VSCALER_CTRL_ADDR_HWREG_LINERATE_DATA, line_rate);
	rval = xv_vscaler_setup_video_fmt(xscaler, code_in);
	if (rval < XVIDC_CSF_RGB || rval > XVIDC_CSF_YCRCB_420) {
		dev_err(xscaler->xvip.dev, "Failed xv_vscaler_setup_video_fmt");
		return -EINVAL;
	}

	/* H-Scaler_setup */
	pixel_rate = (width_in * STEP_PRECISION)/width_out;

	xvip_write(&xscaler->xvip, V_HSCALER_OFF +
		   XV_HSCALER_CTRL_ADDR_HWREG_HEIGHT_DATA, height_out);
	xvip_write(&xscaler->xvip, V_HSCALER_OFF +
		   XV_HSCALER_CTRL_ADDR_HWREG_WIDTHIN_DATA, width_in);
	xvip_write(&xscaler->xvip, V_HSCALER_OFF +
		   XV_HSCALER_CTRL_ADDR_HWREG_WIDTHOUT_DATA, width_out);
	xvip_write(&xscaler->xvip, V_HSCALER_OFF +
		   XV_HSCALER_CTRL_ADDR_HWREG_PIXELRATE_DATA, pixel_rate);
	if (xv_hscaler_setup_video_fmt(xscaler, code_out, rval) < 0) {
		dev_err(xscaler->xvip.dev, "Failed xv_hscaler_setup_video_fmt");
		return -EINVAL;
	}

	/* Set Polyphasse coeff */
	xv_hscaler_select_coeff(xscaler, width_in, width_out);
	/* Program generated coefficients into the IP register bank */
	xv_hscaler_set_coeff(xscaler);


	/* Set HPHASE coeff */
	loop_width = xscaler->max_pixels/xscaler->pix_per_clk;
	offset = V_HSCALER_OFF + XV_HSCALER_CTRL_ADDR_HWREG_PHASESH_V_BASE;

	calculate_phases(xscaler, width_in, width_out, pixel_rate);

	/* phaseH is 64bits but only lower 16b of each entry is valid
	 * Form 32b word with 16bit LSB from 2 consecutive entries
	 * Need 1 32b write to get 2 entries into IP registers
	 * (i is array loc and index is address offset)
	 */
	index = 0;
	for (i = 0; i < loop_width; i += 2) {
		lsb = (u32)(xscaler->phasesH[i] & (u64)XHSC_MASK_LOW_16BITS);
		msb = (u32)(xscaler->phasesH[i+1] & (u64)XHSC_MASK_LOW_16BITS);
		val = (msb << 16 | lsb);
		xvip_write(&xscaler->xvip, offset + (index*4), val);
		++index;
	}

	/* Start Scaler sub-cores */
	xvip_write(&xscaler->xvip, V_HSCALER_OFF +
		   XV_HSCALER_CTRL_ADDR_AP_CTRL, 0x81);
	/* TODO : Split into start/autorestart enable calls */
	xvip_write(&xscaler->xvip, V_VSCALER_OFF +
		   XV_VSCALER_CTRL_ADDR_AP_CTRL, 0x81);
	xv_procss_enable_block(&xscaler->xvip, GPIO_CH_RESET_SEL,
			     RESET_MASK_VIDEO_IN);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xscaler_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, cfg, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	fse->min_width = XSCALER_MIN_WIDTH;
	fse->max_width = XSCALER_MAX_WIDTH;
	fse->min_height = XSCALER_MIN_HEIGHT;
	fse->max_height = XSCALER_MAX_HEIGHT;

	return 0;
}

static struct v4l2_mbus_framefmt *
__xscaler_get_pad_format(struct xscaler_device *xscaler,
			 struct v4l2_subdev_pad_config *cfg,
			 unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xscaler->xvip.subdev, cfg,
						  pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xscaler->formats[pad];
	default:
		return NULL;
	}
}

static int xscaler_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct xscaler_device *xscaler = to_scaler(subdev);

	fmt->format = *__xscaler_get_pad_format(xscaler, cfg, fmt->pad,
						fmt->which);

	return 0;
}

static int xscaler_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_mbus_framefmt *format;

	format = __xscaler_get_pad_format(xscaler, cfg, fmt->pad, fmt->which);
	*format = fmt->format;

	format->width = clamp_t(unsigned int, fmt->format.width,
				XSCALER_MIN_WIDTH, XSCALER_MAX_WIDTH);
	format->height = clamp_t(unsigned int, fmt->format.height,
				XSCALER_MIN_HEIGHT, XSCALER_MAX_HEIGHT);
	fmt->format = *format;
	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

static int
xscaler_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_mbus_framefmt *format;

	/* Initialize with default formats */
	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SINK);
	*format = xscaler->default_formats[XVIP_PAD_SINK];

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SOURCE);
	*format = xscaler->default_formats[XVIP_PAD_SOURCE];

	return 0;
}

static int
xscaler_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_video_ops xscaler_video_ops = {
	.s_stream = xscaler_s_stream,
};

static struct v4l2_subdev_pad_ops xscaler_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xscaler_enum_frame_size,
	.get_fmt		= xscaler_get_format,
	.set_fmt		= xscaler_set_format,
};

static struct v4l2_subdev_ops xscaler_ops = {
	.video  = &xscaler_video_ops,
	.pad    = &xscaler_pad_ops,
};

static const struct v4l2_subdev_internal_ops xscaler_internal_ops = {
	.open	= xscaler_open,
	.close	= xscaler_close,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xscaler_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

static int __maybe_unused
xscaler_pm_suspend(__attribute__((unused)) struct device *dev)
{
	/* Placeholder for pm ops */

	return 0;
}

static int __maybe_unused
xscaler_pm_resume(struct device *dev)
{
	/* Placeholder for pm ops */

	return 0;
}

/*
 * Platform Device Driver
 */

static int xscaler_parse_of(struct xscaler_device *xscaler)
{
	struct device *dev = xscaler->xvip.dev;
	struct device_node *node = xscaler->xvip.dev->of_node;
	const struct xvip_video_format *vip_format;
	struct device_node *ports;
	struct device_node *port;
	int ret;
	u32 port_id = 0;

	ports = of_get_child_by_name(node, "ports");
	if (ports == NULL)
		ports = node;

	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0)) {
			vip_format = xvip_of_get_format(port);
			if (IS_ERR(vip_format)) {
				dev_err(dev, "invalid format in DT");
				return PTR_ERR(vip_format);
			}

			ret = of_property_read_u32(port, "reg", &port_id);
			if (ret < 0) {
				dev_err(dev, "No reg in DT");
				return ret;
			}

			if (port_id != 0 && port_id != 1) {
				dev_err(dev, "Invalid reg in DT");
				return -EINVAL;
			}
			xscaler->vip_formats[port_id] = vip_format;
		}
	}

	ret = of_property_read_u32(node, "xlnx,num-hori-taps",
				   &xscaler->num_hori_taps);
	if (ret < 0)
		return ret;
	dev_info(xscaler->xvip.dev,
		"Num Hori Taps %d", xscaler->num_hori_taps);
	ret = of_property_read_u32(node, "xlnx,num-vert-taps",
				   &xscaler->num_vert_taps);
	if (ret < 0)
		return ret;
	dev_info(xscaler->xvip.dev,
		"Num Vert Taps %d", xscaler->num_vert_taps);

	ret = of_property_read_u32(node, "xlnx,max-num-phases",
				   &xscaler->max_num_phases);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "xlnx,max-lines",
				   &xscaler->max_lines);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "xlnx,max-pixels",
				   &xscaler->max_pixels);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "xlnx,pix-per-clk",
				   &xscaler->pix_per_clk);
	if (ret < 0)
		return ret;

	xscaler->separate_yc_coef =
		of_property_read_bool(node, "xlnx,separate-yc-coef");

	xscaler->separate_hv_coef =
		of_property_read_bool(node, "xlnx,separate-hv-coef");

	return 0;
}

static int xscaler_probe(struct platform_device *pdev)
{
	struct xscaler_device *xscaler;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *default_format;
	int ret;

	dev_info(&pdev->dev, "VPSS Scaler Only Probe Started\n");
	xscaler = devm_kzalloc(&pdev->dev, sizeof(*xscaler), GFP_KERNEL);
	if (!xscaler)
		return -ENOMEM;

	xscaler->xvip.dev = &pdev->dev;

	ret = xscaler_parse_of(xscaler);
	if (ret < 0)
		return ret;

	ret = xvip_init_resources(&xscaler->xvip);
	if (ret < 0)
		return ret;

	/* Reset and initialize the core */
	dev_info(xscaler->xvip.dev, "Reset VPSS Scalar\n");
	xscaler_reset(xscaler);

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xscaler->xvip.subdev;
	v4l2_subdev_init(subdev, &xscaler_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xscaler_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xscaler);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Initialize default and active formats */
	default_format = &xscaler->default_formats[XVIP_PAD_SINK];
	default_format->code = xscaler->vip_formats[XVIP_PAD_SINK]->code;
	default_format->field = V4L2_FIELD_NONE;
	default_format->colorspace = V4L2_COLORSPACE_SRGB;
	default_format->width = XSCALER_DEF_IN_WIDTH;
	default_format->height = XSCALER_DEF_IN_HEIGHT;

	xscaler->formats[XVIP_PAD_SINK] = *default_format;

	default_format = &xscaler->default_formats[XVIP_PAD_SOURCE];
	*default_format = xscaler->default_formats[XVIP_PAD_SINK];
	default_format->code = xscaler->vip_formats[XVIP_PAD_SOURCE]->code;
	default_format->width = XSCALER_DEF_OUT_WIDTH;
	default_format->height = XSCALER_DEF_OUT_HEIGHT;
	xscaler->formats[XVIP_PAD_SOURCE] = *default_format;

	xscaler->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xscaler->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xscaler_media_ops;

	ret = media_entity_pads_init(&subdev->entity, 2, xscaler->pads);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xscaler);

	dev_info(&pdev->dev, "VPSS Scaler Only Probe Successful\n");

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xscaler->xvip);
	return ret;
}

static int xscaler_remove(struct platform_device *pdev)
{
	struct xscaler_device *xscaler = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xscaler->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	xvip_cleanup_resources(&xscaler->xvip);

	return 0;
}

static SIMPLE_DEV_PM_OPS(xscaler_pm_ops, xscaler_pm_suspend, xscaler_pm_resume);

static const struct of_device_id xscaler_of_id_table[] = {
	{ .compatible = "xlnx,v-scaler-hls-8.1" },
	{ }
};
MODULE_DEVICE_TABLE(of, xscaler_of_id_table);

static struct platform_driver xscaler_driver = {
	.driver			= {
		.name		= "xilinx-scaler-hls",
		.of_match_table	= xscaler_of_id_table,
	},
	.probe			= xscaler_probe,
	.remove			= xscaler_remove,
};

module_platform_driver(xscaler_driver);

MODULE_DESCRIPTION("Xilinx Scaler Driver");
MODULE_LICENSE("GPL v2");

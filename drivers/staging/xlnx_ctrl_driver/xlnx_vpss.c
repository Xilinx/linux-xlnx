// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA Xilinx VPSS control driver.
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 * Author: Saurabh Sengar <saurabh.singh@xilinx.com>
 */

/* TODO: clock framework */

#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xlnx_ctrl.h>

/* VPSS block offset */
#define XHSCALER_OFFSET			0
#define XSAXIS_RST_OFFSET		0x10000
#define XVSCALER_OFFSET			0x20000

#define XVPSS_GPIO_CHAN			8

#define XVPSS_MAX_WIDTH			3840
#define XVPSS_MAX_HEIGHT		2160

#define XVPSS_STEPPREC			65536

/* Video IP PPC */
#define XVPSS_PPC_1			1
#define XVPSS_PPC_2			2

#define XVPSS_MAX_TAPS			12
#define XVPSS_PHASES			64
#define XVPSS_TAPS_6			6

/* Mask definitions for Low and high 16 bits in a 32 bit number */
#define XVPSS_MASK_LOW_16BITS		GENMASK(15, 0)
#define XVPSS_MASK_LOW_32BITS		GENMASK(31, 0)
#define XVPSS_STEP_PRECISION_SHIFT	(16)
#define XVPSS_PHASE_SHIFT_BY_6		(6)
#define XVPSS_PHASE_MULTIPLIER		(9)
#define XVPSS_BITSHIFT_16		(16)

/* VPSS AP Control Registers */
#define XVPSS_START			BIT(0)
#define XVPSS_RESTART			BIT(7)
#define XVPSS_STREAM_ON			(XVPSS_START | XVPSS_RESTART)

/* H-scaler registers */
#define XVPSS_H_AP_CTRL				(0x0000)
#define XVPSS_H_GIE				(0x0004)
#define XVPSS_H_IER				(0x0008)
#define XVPSS_H_ISR				(0x000c)
#define XVPSS_H_HEIGHT				(0x0010)
#define XVPSS_H_WIDTHIN				(0x0018)
#define XVPSS_H_WIDTHOUT			(0x0020)
#define XVPSS_H_COLOR				(0x0028)
#define XVPSS_H_PIXELRATE			(0x0030)
#define XVPSS_H_COLOROUT			(0X0038)
#define XVPSS_H_HFLTCOEFF_BASE			(0x0800)
#define XVPSS_H_HFLTCOEFF_HIGH			(0x0bff)
#define XVPSS_H_PHASESH_V_BASE			(0x2000)
#define XVPSS_H_PHASESH_V_HIGH			(0x3fff)

/* H-scaler masks */
#define XVPSS_PHASESH_WR_EN			BIT(8)

/* V-scaler registers */
#define XVPSS_V_AP_CTRL				(0x000)
#define XVPSS_V_GIE				(0x004)
#define XVPSS_V_IER				(0x008)
#define XVPSS_V_ISR				(0x00c)
#define XVPSS_V_HEIGHTIN			(0x010)
#define XVPSS_V_WIDTH				(0x018)
#define XVPSS_V_HEIGHTOUT			(0x020)
#define XVPSS_V_LINERATE			(0x028)
#define XVPSS_V_COLOR				(0x030)
#define XVPSS_V_VFLTCOEFF_BASE			(0x800)
#define XVPSS_V_VFLTCOEFF_HIGH			(0xbff)

#define XVPSS_GPIO_RST_SEL			1
#define XVPSS_GPIO_VIDEO_IN			BIT(0)
#define XVPSS_RST_IP_AXIS			BIT(1)
#define XVPSS_GPIO_MASK_ALL	(XVPSS_GPIO_VIDEO_IN | XVPSS_RST_IP_AXIS)

enum xvpss_color {
	XVPSS_YUV_RGB,
	XVPSS_YUV_444,
	XVPSS_YUV_422,
	XVPSS_YUV_420,
};

/* VPSS coefficients for 6 tap filters */
static const u16
xvpss_coeff_taps6[XVPSS_PHASES][XVPSS_TAPS_6] = {
	{  -132,   236,  3824,   236,  -132,    64, },
	{  -116,   184,  3816,   292,  -144,    64, },
	{  -100,   132,  3812,   348,  -160,    64, },
	{   -88,    84,  3808,   404,  -176,    64, },
	{   -72,    36,  3796,   464,  -192,    64, },
	{   -60,    -8,  3780,   524,  -208,    68, },
	{   -48,   -52,  3768,   588,  -228,    68, },
	{   -32,   -96,  3748,   652,  -244,    68, },
	{   -20,  -136,  3724,   716,  -260,    72, },
	{    -8,  -172,  3696,   784,  -276,    72, },
	{     0,  -208,  3676,   848,  -292,    72, },
	{    12,  -244,  3640,   920,  -308,    76, },
	{    20,  -276,  3612,   988,  -324,    76, },
	{    32,  -304,  3568,  1060,  -340,    80, },
	{    40,  -332,  3532,  1132,  -356,    80, },
	{    48,  -360,  3492,  1204,  -372,    84, },
	{    56,  -384,  3448,  1276,  -388,    88, },
	{    64,  -408,  3404,  1352,  -404,    88, },
	{    72,  -428,  3348,  1428,  -416,    92, },
	{    76,  -448,  3308,  1500,  -432,    92, },
	{    84,  -464,  3248,  1576,  -444,    96, },
	{    88,  -480,  3200,  1652,  -460,    96, },
	{    92,  -492,  3140,  1728,  -472,   100, },
	{    96,  -504,  3080,  1804,  -484,   104, },
	{   100,  -516,  3020,  1880,  -492,   104, },
	{   104,  -524,  2956,  1960,  -504,   104, },
	{   104,  -532,  2892,  2036,  -512,   108, },
	{   108,  -540,  2832,  2108,  -520,   108, },
	{   108,  -544,  2764,  2184,  -528,   112, },
	{   112,  -544,  2688,  2260,  -532,   112, },
	{   112,  -548,  2624,  2336,  -540,   112, },
	{   112,  -548,  2556,  2408,  -544,   112, },
	{   112,  -544,  2480,  2480,  -544,   112, },
	{   112,  -544,  2408,  2556,  -548,   112, },
	{   112,  -540,  2336,  2624,  -548,   112, },
	{   112,  -532,  2260,  2688,  -544,   112, },
	{   112,  -528,  2184,  2764,  -544,   108, },
	{   108,  -520,  2108,  2832,  -540,   108, },
	{   108,  -512,  2036,  2892,  -532,   104, },
	{   104,  -504,  1960,  2956,  -524,   104, },
	{   104,  -492,  1880,  3020,  -516,   100, },
	{   104,  -484,  1804,  3080,  -504,    96, },
	{   100,  -472,  1728,  3140,  -492,    92, },
	{    96,  -460,  1652,  3200,  -480,    88, },
	{    96,  -444,  1576,  3248,  -464,    84, },
	{    92,  -432,  1500,  3308,  -448,    76, },
	{    92,  -416,  1428,  3348,  -428,    72, },
	{    88,  -404,  1352,  3404,  -408,    64, },
	{    88,  -388,  1276,  3448,  -384,    56, },
	{    84,  -372,  1204,  3492,  -360,    48, },
	{    80,  -356,  1132,  3532,  -332,    40, },
	{    80,  -340,  1060,  3568,  -304,    32, },
	{    76,  -324,   988,  3612,  -276,    20, },
	{    76,  -308,   920,  3640,  -244,    12, },
	{    72,  -292,   848,  3676,  -208,     0, },
	{    72,  -276,   784,  3696,  -172,    -8, },
	{    72,  -260,   716,  3724,  -136,   -20, },
	{    68,  -244,   652,  3748,   -96,   -32, },
	{    68,  -228,   588,  3768,   -52,   -48, },
	{    68,  -208,   524,  3780,    -8,   -60, },
	{    64,  -192,   464,  3796,    36,   -72, },
	{    64,  -176,   404,  3808,    84,   -88, },
	{    64,  -160,   348,  3812,   132,  -100, },
	{    64,  -144,   292,  3816,   184,  -116, }
};

/**
 * struct xvpss_struct - Xilinx VPSS ctrl object
 *
 * @dev: device structure
 * @xvpss_miscdev: The misc device registered
 * @regs: Base address of VPSS
 * @n_taps: number of horizontal/vertical taps
 * @ppc: Pixels per Clock cycle the IP operates upon
 * @is_polyphase: True for polypshase else false
 * @vpss_coeff: The complete array of H-scaler/V-scaler coefficients
 * @H_phases: The phases needed to program the H-scaler for different taps
 * @reset_gpio: GPIO reset line to bring VPSS Scaler out of reset
 */
struct xvpss_struct {
	struct device *dev;
	struct miscdevice xvpss_miscdev;
	void __iomem *regs;
	int n_taps;
	int ppc;
	bool is_polyphase;
	short vpss_coeff[XVPSS_PHASES][XVPSS_MAX_TAPS];
	u32 H_phases[XVPSS_MAX_WIDTH];
	struct gpio_desc *reset_gpio;
};

struct xvpss_data {
	u32 height_in;
	u32 width_in;
	u32 height_out;
	u32 width_out;
	u32 color_in;
	u32 color_out;
};

/* Match table for of_platform binding */
static const struct of_device_id xvpss_of_match[] = {
	{ .compatible = "xlnx,ctrl-xvpss-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, xvpss_of_match);

static inline struct xvpss_struct *to_xvpss_struct(struct file *file)
{
	struct miscdevice *miscdev = file->private_data;

	return container_of(miscdev, struct xvpss_struct, xvpss_miscdev);
}

static inline u32 xvpss_ior(void __iomem *lp, off_t offset)
{
	return readl(lp + offset);
}

static inline void xvpss_iow(void __iomem *lp, off_t offset, u32 value)
{
	writel(value, (lp + offset));
}

static inline void xvpss_clr(void __iomem *base, u32 offset, u32 clr)
{
	xvpss_iow(base, offset, xvpss_ior(base, offset) & ~clr);
}

static inline void xvpss_set(void __iomem *base, u32 offset, u32 set)
{
	xvpss_iow(base, offset, xvpss_ior(base, offset) | set);
}

static inline void xvpss_disable_block(struct xvpss_struct *xvpss_g,
				       u32 channel, u32 ip_block)
{
	xvpss_clr(xvpss_g->regs, ((channel - 1) * XVPSS_GPIO_CHAN) +
		  XSAXIS_RST_OFFSET, ip_block);
}

static inline void
xvpss_enable_block(struct xvpss_struct *xvpss_g, u32 channel, u32 ip_block)
{
	xvpss_set(xvpss_g->regs, ((channel - 1) * XVPSS_GPIO_CHAN) +
		  XSAXIS_RST_OFFSET, ip_block);
}

static void xvpss_reset(struct xvpss_struct *xvpss_g)
{
	xvpss_disable_block(xvpss_g, XVPSS_GPIO_RST_SEL, XVPSS_GPIO_MASK_ALL);
	xvpss_enable_block(xvpss_g, XVPSS_GPIO_RST_SEL, XVPSS_RST_IP_AXIS);
}

static void xvpss_enable(struct xvpss_struct *xvpss_g)
{
	xvpss_iow(xvpss_g->regs, XHSCALER_OFFSET +
		  XVPSS_H_AP_CTRL, XVPSS_STREAM_ON);
	xvpss_iow(xvpss_g->regs, XVSCALER_OFFSET +
		  XVPSS_V_AP_CTRL, XVPSS_STREAM_ON);
	xvpss_enable_block(xvpss_g, XVPSS_GPIO_RST_SEL, XVPSS_RST_IP_AXIS);
}

static void xvpss_disable(struct xvpss_struct *xvpss_g)
{
	xvpss_disable_block(xvpss_g, XVPSS_GPIO_RST_SEL, XVPSS_GPIO_MASK_ALL);
}

static void xvpss_set_input(struct xvpss_struct *xvpss_g,
			    u32 width, u32 height, u32 color)
{
	xvpss_iow(xvpss_g->regs, XVSCALER_OFFSET + XVPSS_V_HEIGHTIN, height);
	xvpss_iow(xvpss_g->regs, XVSCALER_OFFSET + XVPSS_V_WIDTH, width);
	xvpss_iow(xvpss_g->regs, XHSCALER_OFFSET + XVPSS_H_WIDTHIN, width);
	xvpss_iow(xvpss_g->regs, XVSCALER_OFFSET + XVPSS_V_COLOR, color);
}

static void xvpss_set_output(struct xvpss_struct *xvpss_g, u32 width,
			     u32 height, u32 color)
{
	xvpss_iow(xvpss_g->regs, XVSCALER_OFFSET + XVPSS_V_HEIGHTOUT, height);
	xvpss_iow(xvpss_g->regs, XHSCALER_OFFSET + XVPSS_H_HEIGHT, height);
	xvpss_iow(xvpss_g->regs, XHSCALER_OFFSET + XVPSS_H_WIDTHOUT, width);
	xvpss_iow(xvpss_g->regs, XHSCALER_OFFSET + XVPSS_H_COLOROUT, color);
}

static void xvpss_load_ext_coeff(struct xvpss_struct *xvpss_g,
				 const short *coeff, u32 ntaps)
{
	unsigned int i, j, pad, offset;

	/* Determine if coefficient needs padding (effective vs. max taps) */
	pad = XVPSS_MAX_TAPS - ntaps;
	offset = pad >> 1;
	/* Load coefficients into vpss coefficient table */
	for (i = 0; i < XVPSS_PHASES; i++) {
		for (j = 0; j < ntaps; ++j)
			xvpss_g->vpss_coeff[i][j + offset] =
						coeff[i * ntaps + j];
	}
	if (pad) {
		for (i = 0; i < XVPSS_PHASES; i++) {
			for (j = 0; j < offset; j++)
				xvpss_g->vpss_coeff[i][j] = 0;
			j = ntaps + offset;
			for (; j < XVPSS_MAX_TAPS; j++)
				xvpss_g->vpss_coeff[i][j] = 0;
		}
	}
}

static void xvpss_select_coeff(struct xvpss_struct *xvpss_g)
{
	const short *coeff;
	u32 ntaps;

	coeff = &xvpss_coeff_taps6[0][0];
	ntaps = XVPSS_TAPS_6;

	xvpss_load_ext_coeff(xvpss_g, coeff, ntaps);
}

static void xvpss_set_coeff(struct xvpss_struct *xvpss_g)
{
	u32 nphases = XVPSS_PHASES;
	u32 ntaps = xvpss_g->n_taps;
	int val, i, j, offset, rd_indx;
	u32 v_addr, h_addr;

	offset = (XVPSS_MAX_TAPS - ntaps) / 2;
	v_addr = XVSCALER_OFFSET + XVPSS_V_VFLTCOEFF_BASE;
	h_addr = XHSCALER_OFFSET + XVPSS_H_HFLTCOEFF_BASE;

	for (i = 0; i < nphases; i++) {
		for (j = 0; j < ntaps / 2; j++) {
			rd_indx = j * 2 + offset;
			val = (xvpss_g->vpss_coeff[i][rd_indx + 1] <<
			XVPSS_BITSHIFT_16) | (xvpss_g->vpss_coeff[i][rd_indx] &
			XVPSS_MASK_LOW_16BITS);
			xvpss_iow(xvpss_g->regs, v_addr +
				 ((i * ntaps / 2 + j) * 4), val);
			xvpss_iow(xvpss_g->regs, h_addr +
				 ((i * ntaps / 2 + j) * 4), val);
		}
	}
}

static void xvpss_h_calculate_phases(struct xvpss_struct *xvpss_g,
				     u32 width_in, u32 width_out,
				     u32 pixel_rate)
{
	unsigned int loop_width, x, s, nphases = XVPSS_PHASES;
	unsigned int nppc = xvpss_g->ppc;
	unsigned int shift = XVPSS_STEP_PRECISION_SHIFT - ilog2(nphases);
	int offset = 0, xwrite_pos = 0, nr_rds, nr_rds_clck;
	bool output_write_en, get_new_pix;
	u64 phaseH;
	u32 array_idx = 0;

	loop_width = max_t(u32, width_in, width_out);
	loop_width = ALIGN(loop_width + nppc - 1, nppc);

	memset(xvpss_g->H_phases, 0, sizeof(xvpss_g->H_phases));
	for (x = 0; x < loop_width; x++) {
		nr_rds_clck = 0;
		for (s = 0; s < nppc; s++) {
			phaseH = (offset >> shift) & (nphases - 1);
			get_new_pix = false;
			output_write_en = false;
			if ((offset >> XVPSS_STEP_PRECISION_SHIFT) != 0) {
				get_new_pix = true;
				offset -= (1 << XVPSS_STEP_PRECISION_SHIFT);
				array_idx++;
			}

			if (((offset >> XVPSS_STEP_PRECISION_SHIFT) == 0) &&
			    xwrite_pos < width_out) {
				offset += pixel_rate;
				output_write_en = true;
				xwrite_pos++;
			}

			xvpss_g->H_phases[x] |= (phaseH <<
						(s * XVPSS_PHASE_MULTIPLIER));
			xvpss_g->H_phases[x] |= (array_idx <<
						(XVPSS_PHASE_SHIFT_BY_6 +
						(s * XVPSS_PHASE_MULTIPLIER)));
			if (output_write_en) {
				xvpss_g->H_phases[x] |= (XVPSS_PHASESH_WR_EN <<
						(s * XVPSS_PHASE_MULTIPLIER));
			}

			if (get_new_pix)
				nr_rds_clck++;
		}
		if (array_idx >= nppc)
			array_idx &= (nppc - 1);

		nr_rds += nr_rds_clck;
		if (nr_rds >= nppc)
			nr_rds -= nppc;
	}
}

static void xvpss_h_set_phases(struct xvpss_struct *xvpss_g)
{
	u32 loop_width, index, val, offset, i, lsb, msb;

	loop_width = XVPSS_MAX_WIDTH / xvpss_g->ppc;
	offset = XHSCALER_OFFSET + XVPSS_H_PHASESH_V_BASE;

	switch (xvpss_g->ppc) {
	case XVPSS_PPC_1:
		index = 0;
		for (i = 0; i < loop_width; i += 2) {
			lsb = xvpss_g->H_phases[i] & XVPSS_MASK_LOW_16BITS;
			msb = xvpss_g->H_phases[i + 1] & XVPSS_MASK_LOW_16BITS;
			val = (msb << 16 | lsb);
			xvpss_iow(xvpss_g->regs, offset +
				(index * 4), val);
			++index;
		}
		return;
	case XVPSS_PPC_2:
		for (i = 0; i < loop_width; i++) {
			val = (xvpss_g->H_phases[i] & XVPSS_MASK_LOW_32BITS);
			xvpss_iow(xvpss_g->regs, offset + (i * 4), val);
		}
		return;
	}
}

static void xvpss_algo_config(struct xvpss_struct *xvpss_g,
			      struct xvpss_data data)
{
	u32 pxl_rate, line_rate;
	u32 width_in = data.width_in;
	u32 width_out = data.width_out;
	u32 height_in = data.height_in;
	u32 height_out = data.height_out;

	line_rate = (height_in * XVPSS_STEPPREC) / height_out;

	if (xvpss_g->is_polyphase) {
		xvpss_select_coeff(xvpss_g);
		xvpss_set_coeff(xvpss_g);
	}
	xvpss_iow(xvpss_g->regs, XVSCALER_OFFSET + XVPSS_V_LINERATE, line_rate);
	pxl_rate = (width_in * XVPSS_STEPPREC) / width_out;
	xvpss_iow(xvpss_g->regs, XHSCALER_OFFSET + XVPSS_H_PIXELRATE, pxl_rate);

	xvpss_h_calculate_phases(xvpss_g, width_in, width_out, pxl_rate);
	xvpss_h_set_phases(xvpss_g);
}

static long xvpss_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	long retval = 0;
	struct xvpss_data data;
	struct xvpss_struct *xvpss_g = to_xvpss_struct(file);
	u32 hcol;

	switch (cmd) {
	case XVPSS_SET_CONFIGURE:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		xvpss_reset(xvpss_g);
		xvpss_set_input(xvpss_g, data.width_in, data.height_in,
				data.color_in);
		hcol = data.color_in;
		if (hcol == XVPSS_YUV_420)
			hcol = XVPSS_YUV_422;
		xvpss_iow(xvpss_g->regs, XHSCALER_OFFSET + XVPSS_H_COLOR, hcol);
		xvpss_set_output(xvpss_g, data.width_out, data.height_out,
				 data.color_out);
		xvpss_algo_config(xvpss_g, data);
		break;
	case XVPSS_SET_ENABLE:
		xvpss_enable(xvpss_g);
		break;
	case XVPSS_SET_DISABLE:
		xvpss_disable(xvpss_g);
		break;
	default:
		retval = -EINVAL;
	}
end:
	return retval;
}

static const struct file_operations xvpss_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = xvpss_ioctl,
};

static int xvpss_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct xvpss_struct *xvpss_g;
	struct device_node *node;

	xvpss_g = devm_kzalloc(&pdev->dev, sizeof(*xvpss_g), GFP_KERNEL);
	if (!xvpss_g)
		return -ENOMEM;

	xvpss_g->reset_gpio = devm_gpiod_get(&pdev->dev, "reset",
					     GPIOD_OUT_LOW);
	if (IS_ERR(xvpss_g->reset_gpio)) {
		ret = PTR_ERR(xvpss_g->reset_gpio);
		if (ret == -EPROBE_DEFER)
			dev_dbg(&pdev->dev, "No gpio probed, Deferring...\n");
		else
			dev_err(&pdev->dev, "No reset gpio info from dts\n");
		return ret;
	}
	gpiod_set_value_cansleep(xvpss_g->reset_gpio, 0);

	platform_set_drvdata(pdev, &xvpss_g);
	xvpss_g->dev = &pdev->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xvpss_g->regs = devm_ioremap_resource(xvpss_g->dev, res);
	if (IS_ERR(xvpss_g->regs))
		return PTR_ERR(xvpss_g->regs);

	node = pdev->dev.of_node;
	ret = of_property_read_u32(node, "xlnx,vpss-taps", &xvpss_g->n_taps);
	if (ret < 0) {
		dev_err(xvpss_g->dev, "taps not present in DT\n");
		return ret;
	}

	switch (xvpss_g->n_taps) {
	case 2:
	case 4:
		break;
	case 6:
		xvpss_g->is_polyphase = true;
		break;
	default:
		dev_err(xvpss_g->dev, "taps value not supported\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,vpss-ppc", &xvpss_g->ppc);
	if (ret < 0) {
		dev_err(xvpss_g->dev, "PPC is missing in DT\n");
		return ret;
	}
	if (xvpss_g->ppc != XVPSS_PPC_1 && xvpss_g->ppc != XVPSS_PPC_2) {
		dev_err(xvpss_g->dev, "Unsupported ppc: %d", xvpss_g->ppc);
		return -EINVAL;
	}

	xvpss_g->xvpss_miscdev.minor = MISC_DYNAMIC_MINOR;
	xvpss_g->xvpss_miscdev.name = "xvpss";
	xvpss_g->xvpss_miscdev.fops = &xvpss_fops;
	ret = misc_register(&xvpss_g->xvpss_miscdev);
	if (ret < 0) {
		pr_err("Xilinx VPSS registration failed!\n");
		return ret;
	}

	dev_info(xvpss_g->dev, "Xlnx VPSS control driver initialized!\n");

	return ret;
}

static int xvpss_remove(struct platform_device *pdev)
{
	struct xvpss_struct *xvpss_g = platform_get_drvdata(pdev);

	misc_deregister(&xvpss_g->xvpss_miscdev);
	return 0;
}

static struct platform_driver xvpss_driver = {
	.probe = xvpss_probe,
	.remove = xvpss_remove,
	.driver = {
		 .name = "xlnx_vpss",
		 .of_match_table = xvpss_of_match,
	},
};

module_platform_driver(xvpss_driver);

MODULE_DESCRIPTION("Xilinx VPSS control driver");
MODULE_AUTHOR("Saurabh Sengar");
MODULE_LICENSE("GPL v2");

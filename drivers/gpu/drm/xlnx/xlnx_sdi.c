// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA SDI Tx Subsystem driver.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Contacts: Saurabh Sengar <saurabhs@xilinx.com>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drmP.h>
#include <drm/drm_probe_helper.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <media/hdr-ctrls.h>
#include <video/videomode.h>
#include "xlnx_sdi_modes.h"
#include "xlnx_sdi_timing.h"

#include "xlnx_bridge.h"

/* SDI register offsets */
#define XSDI_TX_RST_CTRL		0x00
#define XSDI_TX_MDL_CTRL		0x04
#define XSDI_TX_GLBL_IER		0x0C
#define XSDI_TX_ISR_STAT		0x10
#define XSDI_TX_IER_STAT		0x14
#define XSDI_TX_ST352_LINE		0x18
#define XSDI_TX_ST352_DATA_CH0		0x1C
#define XSDI_TX_VER			0x3C
#define XSDI_TX_SYS_CFG			0x40
#define XSDI_TX_STS_SB_TDATA		0x60
#define XSDI_TX_AXI4S_STS1		0x68
#define XSDI_TX_AXI4S_STS2		0x6C
#define XSDI_TX_ST352_DATA_DS2		0x70

/* MODULE_CTRL register masks */
#define XSDI_TX_CTRL_M			BIT(7)
#define XSDI_TX_CTRL_INS_CRC		BIT(12)
#define XSDI_TX_CTRL_INS_ST352		BIT(13)
#define XSDI_TX_CTRL_OVR_ST352		BIT(14)
#define XSDI_TX_CTRL_INS_SYNC_BIT	BIT(16)
#define XSDI_TX_CTRL_USE_ANC_IN		BIT(18)
#define XSDI_TX_CTRL_INS_LN		BIT(19)
#define XSDI_TX_CTRL_INS_EDH		BIT(20)
#define XSDI_TX_CTRL_MODE		0x7
#define XSDI_TX_CTRL_MUX		0x7
#define XSDI_TX_CTRL_MODE_SHIFT		4
#define XSDI_TX_CTRL_M_SHIFT		7
#define XSDI_TX_CTRL_MUX_SHIFT		8
#define XSDI_TX_CTRL_ST352_F2_EN_SHIFT	15
#define XSDI_TX_CTRL_420_BIT		BIT(21)
#define XSDI_TX_CTRL_INS_ST352_CHROMA	BIT(23)
#define XSDI_TX_CTRL_USE_DS2_3GA	BIT(24)

/* TX_ST352_LINE register masks */
#define XSDI_TX_ST352_LINE_MASK		GENMASK(10, 0)
#define XSDI_TX_ST352_LINE_F2_SHIFT	16

/* ISR STAT register masks */
#define XSDI_GTTX_RSTDONE_INTR		BIT(0)
#define XSDI_TX_CE_ALIGN_ERR_INTR	BIT(1)
#define XSDI_TX_VSYNC_INTR		BIT(2)
#define XSDI_AXI4S_VID_LOCK_INTR	BIT(8)
#define XSDI_OVERFLOW_INTR		BIT(9)
#define XSDI_UNDERFLOW_INTR		BIT(10)
#define XSDI_IER_EN_MASK		(XSDI_GTTX_RSTDONE_INTR | \
					 XSDI_TX_CE_ALIGN_ERR_INTR | \
					 XSDI_TX_VSYNC_INTR | \
					 XSDI_OVERFLOW_INTR | \
					 XSDI_UNDERFLOW_INTR)

/* RST_CTRL_OFFSET masks */
#define XSDI_TX_CTRL_EN			BIT(0)
#define XSDI_TX_BRIDGE_CTRL_EN		BIT(8)
#define XSDI_TX_AXI4S_CTRL_EN		BIT(9)
/* STS_SB_TX_TDATA masks */
#define XSDI_TX_TDATA_GT_RESETDONE	BIT(2)

#define XSDI_TX_MUX_SD_HD_3GA		0
#define	XSDI_TX_MUX_3GB			1
#define	XSDI_TX_MUX_8STREAM_6G_12G	2
#define	XSDI_TX_MUX_4STREAM_6G		3
#define	XSDI_TX_MUX_16STREAM_12G	4

#define SDI_MAX_DATASTREAM		8
#define PIXELS_PER_CLK			2
#define XSDI_CH_SHIFT			29
#define XST352_PROG_PIC			BIT(6)
#define XST352_PROG_TRANS		BIT(7)
#define XST352_2048_SHIFT		BIT(6)
#define XST352_YUV420_MASK		0x03
#define ST352_BYTE3			0x00

/* Electro Optical Transfer Function */
#define XST352_BYTE2_EOTF_MASK		GENMASK(13, 12)
#define XST352_BYTE2_EOTF_SDRTV		0x0
#define XST352_BYTE2_EOTF_HLG		0x1
#define XST352_BYTE2_EOTF_SMPTE2084	0x2
#define XST352_BYTE2_EOTF_UNKNOWN	0x3
#define XST352_BYTE3_COLORIMETRY_HD	BIT(23)
#define XST352_BYTE3_COLORIMETRY	BIT(21)

#define ST352_BYTE4			0x01
#define GT_TIMEOUT			50
/* SDI modes */
#define XSDI_MODE_HD			0
#define	XSDI_MODE_SD			1
#define	XSDI_MODE_3GA			2
#define	XSDI_MODE_3GB			3
#define	XSDI_MODE_6G			4
#define	XSDI_MODE_12G			5

#define SDI_TIMING_PARAMS_SIZE		48
#define CLK_RATE			148500000UL

/**
 * enum payload_line_1 - Payload Ids Line 1 number
 * @PAYLD_LN1_HD_3_6_12G:	line 1 HD,3G,6G or 12G mode value
 * @PAYLD_LN1_SDPAL:		line 1 SD PAL mode value
 * @PAYLD_LN1_SDNTSC:		line 1 SD NTSC mode value
 */
enum payload_line_1 {
	PAYLD_LN1_HD_3_6_12G = 10,
	PAYLD_LN1_SDPAL = 9,
	PAYLD_LN1_SDNTSC = 13
};

/**
 * enum payload_line_2 - Payload Ids Line 2 number
 * @PAYLD_LN2_HD_3_6_12G:	line 2 HD,3G,6G or 12G mode value
 * @PAYLD_LN2_SDPAL:		line 2 SD PAL mode value
 * @PAYLD_LN2_SDNTSC:		line 2 SD NTSC mode value
 */
enum payload_line_2 {
	PAYLD_LN2_HD_3_6_12G = 572,
	PAYLD_LN2_SDPAL = 322,
	PAYLD_LN2_SDNTSC = 276
};

/**
 * struct xlnx_sdi - Core configuration SDI Tx subsystem device structure
 * @encoder: DRM encoder structure
 * @connector: DRM connector structure
 * @dev: device structure
 * @gt_rst_gpio: GPIO handle to reset GT phy
 * @base: Base address of SDI subsystem
 * @mode_flags: SDI operation mode related flags
 * @wait_event: wait event
 * @event_received: wait event status
 * @enable_st352_chroma: Able to send ST352 packets in Chroma stream.
 * @enable_anc_data: Enable/Disable Ancillary Data insertion for Audio
 * @sdi_mode: configurable SDI mode parameter, supported values are:
 *		0 - HD
 *		1 - SD
 *		2 - 3GA
 *		3 - 3GB
 *		4 - 6G
 *		5 - 12G
 * @sdi_mod_prop_val: configurable SDI mode parameter value
 * @sdi_data_strm: configurable SDI data stream parameter
 * @sdi_data_strm_prop_val: configurable number of SDI data streams
 *			    value currently supported are 2, 4 and 8
 * @sdi_420_in: Specifying input bus color format parameter to SDI
 * @sdi_420_in_val: 1 for yuv420 and 0 for yuv422
 * @sdi_420_out: configurable SDI out color format parameter
 * @sdi_420_out_val: 1 for yuv420 and 0 for yuv422
 * @is_frac_prop: configurable SDI fractional fps parameter
 * @is_frac_prop_val: configurable SDI fractional fps parameter value
 * @bridge: bridge structure
 * @height_out: configurable bridge output height parameter
 * @height_out_prop_val: configurable bridge output height parameter value
 * @width_out: configurable bridge output width parameter
 * @width_out_prop_val: configurable bridge output width parameter value
 * @in_fmt: configurable bridge input media format
 * @in_fmt_prop_val: configurable media bus format value
 * @out_fmt: configurable bridge output media format
 * @out_fmt_prop_val: configurable media bus format value
 * @en_st352_c_prop: configurable ST352 payload on Chroma stream parameter
 * @en_st352_c_val: configurable ST352 payload on Chroma parameter value
 * @use_ds2_3ga_prop: Use DS2 instead of DS3 in 3GA mode parameter
 * @use_ds2_3ga_val: Use DS2 instead of DS3 in 3GA mode parameter value
 * @c_encoding: configurable color encoding
 * @c_encoding_prop_val: 1 for UHDTV and 0 for Rec709
 * @video_mode: current display mode
 * @axi_clk: AXI Lite interface clock
 * @sditx_clk: SDI Tx Clock
 * @vidin_clk: Video Clock
 * @qpll1_enabled: indicates qpll1 presence
 * @picxo_enabled: indicates picxo core presence
 * @prev_eotf: previous end of transfer function
 */
struct xlnx_sdi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct device *dev;
	struct gpio_desc *gt_rst_gpio;
	void __iomem *base;
	u32 mode_flags;
	wait_queue_head_t wait_event;
	bool event_received;
	bool enable_st352_chroma;
	bool enable_anc_data;
	struct drm_property *sdi_mode;
	u32 sdi_mod_prop_val;
	struct drm_property *sdi_data_strm;
	u32 sdi_data_strm_prop_val;
	struct drm_property *sdi_420_in;
	bool sdi_420_in_val;
	struct drm_property *sdi_420_out;
	bool sdi_420_out_val;
	struct drm_property *is_frac_prop;
	bool is_frac_prop_val;
	struct xlnx_bridge *bridge;
	struct drm_property *height_out;
	u32 height_out_prop_val;
	struct drm_property *width_out;
	u32 width_out_prop_val;
	struct drm_property *in_fmt;
	u32 in_fmt_prop_val;
	struct drm_property *out_fmt;
	u32 out_fmt_prop_val;
	struct drm_property *en_st352_c_prop;
	bool en_st352_c_val;
	struct drm_property *use_ds2_3ga_prop;
	bool use_ds2_3ga_val;
	struct drm_property *c_encoding;
	u32 c_encoding_prop_val;
	struct drm_display_mode video_mode;
	struct clk *axi_clk;
	struct clk *sditx_clk;
	struct clk *vidin_clk;
	bool qpll1_enabled;
	bool picxo_enabled;
	u8 prev_eotf;
};

#define connector_to_sdi(c) container_of(c, struct xlnx_sdi, connector)
#define encoder_to_sdi(e) container_of(e, struct xlnx_sdi, encoder)

static inline void xlnx_sdi_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static inline u32 xlnx_sdi_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

/**
 * xlnx_sdi_en_axi4s - Enable SDI Tx AXI4S-to-Video core
 * @sdi:	Pointer to SDI Tx structure
 *
 * This function enables the SDI Tx AXI4S-to-Video core.
 */
static void xlnx_sdi_en_axi4s(struct xlnx_sdi *sdi)
{
	u32 data;

	data = xlnx_sdi_readl(sdi->base, XSDI_TX_RST_CTRL);
	data |= XSDI_TX_AXI4S_CTRL_EN;
	xlnx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, data);
}

/**
 * xlnx_sdi_en_bridge - Enable SDI Tx bridge
 * @sdi:	Pointer to SDI Tx structure
 *
 * This function enables the SDI Tx bridge.
 */
static void xlnx_sdi_en_bridge(struct xlnx_sdi *sdi)
{
	u32 data;

	data = xlnx_sdi_readl(sdi->base, XSDI_TX_RST_CTRL);
	data |= XSDI_TX_BRIDGE_CTRL_EN;
	xlnx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, data);
}

/**
 * xlnx_sdi_gt_reset - Reset cores through gpio
 * @sdi: Pointer to SDI Tx structure
 *
 * This function resets the GT phy core.
 */
static void xlnx_sdi_gt_reset(struct xlnx_sdi *sdi)
{
	gpiod_set_value(sdi->gt_rst_gpio, 1);
	gpiod_set_value(sdi->gt_rst_gpio, 0);
	/* delay added to get vtc_en signal */
	mdelay(5);
}

/**
 * xlnx_sdi_set_eotf - Set eotf field in payload
 * @sdi: Pointer to SDI Tx structure
 *
 * This function parse the hdr metadata and sets
 * eotf and colorimetry fields of payload.
 */
static void xlnx_sdi_set_eotf(struct xlnx_sdi *sdi)
{
	struct hdmi_drm_infoframe frame;
	struct drm_connector_state *state = sdi->connector.state;
	u32 payload, i;
	int ret;
	u8 eotf, colori;

	ret = drm_hdmi_infoframe_set_gen_hdr_metadata(&frame, state);
	if (ret)
		return;

	eotf = (__u8)frame.eotf;

	if (sdi->prev_eotf == eotf || eotf > XST352_BYTE2_EOTF_UNKNOWN)
		return;

	switch (eotf) {
	case V4L2_EOTF_BT_2100_HLG:
		eotf = XST352_BYTE2_EOTF_HLG;
		break;
	case V4L2_EOTF_TRADITIONAL_GAMMA_SDR:
		eotf = XST352_BYTE2_EOTF_SDRTV;
		break;
	case V4L2_EOTF_SMPTE_ST2084:
		eotf = XST352_BYTE2_EOTF_SMPTE2084;
		break;
	}

	colori = sdi->c_encoding_prop_val;
	payload = xlnx_sdi_readl(sdi->base, XSDI_TX_ST352_DATA_CH0);

	/*
	 * For HD mode, bit 23 and 20 of payload represents
	 * colorimetry as per SMPTE 292-1:2018 Sec 9.5.
	 * For other modes, its bit 21 and 20.
	 * For BT709 & BT2020 - bit 20 is always zero
	 */
	if (sdi->sdi_mod_prop_val == XSDI_MODE_HD) {
		payload &= ~(XST352_BYTE2_EOTF_MASK |
			     XST352_BYTE3_COLORIMETRY_HD);
		payload |= FIELD_PREP(XST352_BYTE2_EOTF_MASK, eotf) |
			FIELD_PREP(XST352_BYTE3_COLORIMETRY_HD, colori);
	} else {
		payload &= ~(XST352_BYTE2_EOTF_MASK |
			     XST352_BYTE3_COLORIMETRY);
		payload |= FIELD_PREP(XST352_BYTE2_EOTF_MASK, eotf) |
			FIELD_PREP(XST352_BYTE3_COLORIMETRY, colori);
	}

	dev_dbg(sdi->dev, "payload = 0x%x, eotf = %d\n", payload, eotf);
	for (i = 0; i < sdi->sdi_data_strm_prop_val / 2; i++)
		xlnx_sdi_writel(sdi->base,
				(XSDI_TX_ST352_DATA_CH0 + (i * 4)), payload);
	sdi->prev_eotf = eotf;
}

/**
 * xlnx_sdi_irq_handler - SDI Tx interrupt
 * @irq:	irq number
 * @data:	irq data
 *
 * Return: IRQ_HANDLED for all cases.
 *
 * This is the compact GT ready interrupt.
 */
static irqreturn_t xlnx_sdi_irq_handler(int irq, void *data)
{
	struct xlnx_sdi *sdi = (struct xlnx_sdi *)data;
	u32 reg;

	reg = xlnx_sdi_readl(sdi->base, XSDI_TX_ISR_STAT);

	if (reg & XSDI_TX_VSYNC_INTR)
		xlnx_sdi_set_eotf(sdi);
	if (reg & XSDI_GTTX_RSTDONE_INTR)
		dev_dbg(sdi->dev, "GT reset interrupt received\n");
	if (reg & XSDI_TX_CE_ALIGN_ERR_INTR)
		dev_err_ratelimited(sdi->dev, "SDI SD CE align error\n");
	if (reg & XSDI_OVERFLOW_INTR)
		dev_err_ratelimited(sdi->dev, "AXI-4 Stream Overflow error\n");
	if (reg & XSDI_UNDERFLOW_INTR)
		dev_err_ratelimited(sdi->dev, "AXI-4 Stream Underflow error\n");
	xlnx_sdi_writel(sdi->base, XSDI_TX_ISR_STAT,
			reg & ~(XSDI_AXI4S_VID_LOCK_INTR));

	reg = xlnx_sdi_readl(sdi->base, XSDI_TX_STS_SB_TDATA);
	if (reg & XSDI_TX_TDATA_GT_RESETDONE) {
		sdi->event_received = true;
		wake_up_interruptible(&sdi->wait_event);
	}
	return IRQ_HANDLED;
}

/**
 * xlnx_sdi_set_payload_line - set ST352 packet line number
 * @sdi:	Pointer to SDI Tx structure
 * @line_1:	line number used to insert st352 packet for field 1.
 * @line_2:	line number used to insert st352 packet for field 2.
 *
 * This function set 352 packet line number.
 */
static void xlnx_sdi_set_payload_line(struct xlnx_sdi *sdi,
				      u32 line_1, u32 line_2)
{
	u32 data;

	data = ((line_1 & XSDI_TX_ST352_LINE_MASK) |
		((line_2 & XSDI_TX_ST352_LINE_MASK) <<
		XSDI_TX_ST352_LINE_F2_SHIFT));

	xlnx_sdi_writel(sdi->base, XSDI_TX_ST352_LINE, data);

	data = xlnx_sdi_readl(sdi->base, XSDI_TX_MDL_CTRL);
	data |= (1 << XSDI_TX_CTRL_ST352_F2_EN_SHIFT);

	xlnx_sdi_writel(sdi->base, XSDI_TX_MDL_CTRL, data);
}

/**
 * xlnx_sdi_set_payload_data - set ST352 packet payload
 * @sdi:		Pointer to SDI Tx structure
 * @data_strm:		data stream number
 * @payload:		st352 packet payload
 *
 * This function set ST352 payload data to corresponding stream.
 */
static void xlnx_sdi_set_payload_data(struct xlnx_sdi *sdi,
				      u32 data_strm, u32 payload)
{
	xlnx_sdi_writel(sdi->base,
			(XSDI_TX_ST352_DATA_CH0 + (data_strm * 4)), payload);

	dev_dbg(sdi->dev, "enable_st352_chroma = %d and en_st352_c_val = %d\n",
		sdi->enable_st352_chroma, sdi->en_st352_c_val);
	if (sdi->enable_st352_chroma && sdi->en_st352_c_val) {
		xlnx_sdi_writel(sdi->base,
				(XSDI_TX_ST352_DATA_DS2 + (data_strm * 4)),
				payload);
	}
}

/**
 * xlnx_sdi_set_display_disable - Disable the SDI Tx IP core enable
 * register bit
 * @sdi: SDI structure having the updated user parameters
 *
 * This function takes the SDI strucure and disables the core enable bit
 * of core configuration register.
 */
static void xlnx_sdi_set_display_disable(struct xlnx_sdi *sdi)
{
	u32 i;

	for (i = 0; i < SDI_MAX_DATASTREAM; i++)
		xlnx_sdi_set_payload_data(sdi, i, 0);

	xlnx_sdi_writel(sdi->base, XSDI_TX_GLBL_IER, 0);
	xlnx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, 0);
}

/**
 * xlnx_sdi_payload_config -  config the SDI payload parameters
 * @sdi:	pointer Xilinx SDI Tx structure
 * @mode:	display mode
 *
 * This function config the SDI st352 payload parameter.
 */
static void xlnx_sdi_payload_config(struct xlnx_sdi *sdi, u32 mode)
{
	u32 payload_1, payload_2;

	switch (mode) {
	case XSDI_MODE_SD:
		payload_1 = PAYLD_LN1_SDPAL;
		payload_2 = PAYLD_LN2_SDPAL;
		break;
	case XSDI_MODE_HD:
	case XSDI_MODE_3GA:
	case XSDI_MODE_3GB:
	case XSDI_MODE_6G:
	case XSDI_MODE_12G:
		payload_1 = PAYLD_LN1_HD_3_6_12G;
		payload_2 = PAYLD_LN2_HD_3_6_12G;
		break;
	default:
		payload_1 = 0;
		payload_2 = 0;
		break;
	}

	xlnx_sdi_set_payload_line(sdi, payload_1, payload_2);
}

/**
 * xlnx_sdi_set_mode -  Set mode parameters in SDI Tx
 * @sdi:	pointer Xilinx SDI Tx structure
 * @mode:	SDI Tx display mode
 * @is_frac:	0 - integer 1 - fractional
 * @mux_ptrn:	specifiy the data stream interleaving pattern to be used
 * This function config the SDI st352 payload parameter.
 */
static void xlnx_sdi_set_mode(struct xlnx_sdi *sdi, u32 mode,
			      bool is_frac, u32 mux_ptrn)
{
	u32 data;

	xlnx_sdi_payload_config(sdi, mode);

	data = xlnx_sdi_readl(sdi->base, XSDI_TX_MDL_CTRL);
	data &= ~(XSDI_TX_CTRL_MODE << XSDI_TX_CTRL_MODE_SHIFT);
	data &= ~(XSDI_TX_CTRL_M);
	data &= ~(XSDI_TX_CTRL_MUX << XSDI_TX_CTRL_MUX_SHIFT);
	data &= ~XSDI_TX_CTRL_420_BIT;

	data |= (((mode & XSDI_TX_CTRL_MODE) << XSDI_TX_CTRL_MODE_SHIFT) |
		(is_frac  << XSDI_TX_CTRL_M_SHIFT) |
		((mux_ptrn & XSDI_TX_CTRL_MUX) << XSDI_TX_CTRL_MUX_SHIFT));

	if (sdi->sdi_420_out_val)
		data |= XSDI_TX_CTRL_420_BIT;
	xlnx_sdi_writel(sdi->base, XSDI_TX_MDL_CTRL, data);
}

/**
 * xlnx_sdi_set_config_parameters - Configure SDI Tx registers with parameters
 * given from user application.
 * @sdi: SDI structure having the updated user parameters
 *
 * This function takes the SDI structure having drm_property parameters
 * configured from  user application and writes them into SDI IP registers.
 */
static void xlnx_sdi_set_config_parameters(struct xlnx_sdi *sdi)
{
	int mux_ptrn = -EINVAL;

	switch (sdi->sdi_mod_prop_val) {
	case XSDI_MODE_3GA:
		mux_ptrn = XSDI_TX_MUX_SD_HD_3GA;
		break;
	case XSDI_MODE_3GB:
		mux_ptrn = XSDI_TX_MUX_3GB;
		break;
	case XSDI_MODE_6G:
		if (sdi->sdi_data_strm_prop_val == 4)
			mux_ptrn = XSDI_TX_MUX_4STREAM_6G;
		else if (sdi->sdi_data_strm_prop_val == 8)
			mux_ptrn = XSDI_TX_MUX_8STREAM_6G_12G;
		break;
	case XSDI_MODE_12G:
		if (sdi->sdi_data_strm_prop_val == 8)
			mux_ptrn = XSDI_TX_MUX_8STREAM_6G_12G;
		break;
	default:
		mux_ptrn = 0;
		break;
	}
	if (mux_ptrn == -EINVAL) {
		dev_err(sdi->dev, "%d data stream not supported for %d mode",
			sdi->sdi_data_strm_prop_val, sdi->sdi_mod_prop_val);
		return;
	}
	xlnx_sdi_set_mode(sdi, sdi->sdi_mod_prop_val, sdi->is_frac_prop_val,
			  mux_ptrn);
}

/**
 * xlnx_sdi_atomic_set_property - implementation of drm_connector_funcs
 * set_property invoked by IOCTL call to DRM_IOCTL_MODE_OBJ_SETPROPERTY
 *
 * @connector: pointer Xilinx SDI connector
 * @state: DRM connector state
 * @property: pointer to the drm_property structure
 * @val: SDI parameter value that is configured from user application
 *
 * This function takes a drm_property name and value given from user application
 * and update the SDI structure property varabiles with the values.
 * These values are later used to configure the SDI Rx IP.
 *
 * Return: 0 on success OR -EINVAL if setting property fails
 */
static int
xlnx_sdi_atomic_set_property(struct drm_connector *connector,
			     struct drm_connector_state *state,
			     struct drm_property *property, uint64_t val)
{
	struct xlnx_sdi *sdi = connector_to_sdi(connector);

	if (property == sdi->sdi_mode)
		sdi->sdi_mod_prop_val = (unsigned int)val;
	else if (property == sdi->sdi_data_strm)
		sdi->sdi_data_strm_prop_val = (unsigned int)val;
	else if (property == sdi->sdi_420_in)
		sdi->sdi_420_in_val = val;
	else if (property == sdi->sdi_420_out)
		sdi->sdi_420_out_val = val;
	else if (property == sdi->is_frac_prop)
		sdi->is_frac_prop_val = !!val;
	else if (property == sdi->height_out)
		sdi->height_out_prop_val = (unsigned int)val;
	else if (property == sdi->width_out)
		sdi->width_out_prop_val = (unsigned int)val;
	else if (property == sdi->in_fmt)
		sdi->in_fmt_prop_val = (unsigned int)val;
	else if (property == sdi->out_fmt)
		sdi->out_fmt_prop_val = (unsigned int)val;
	else if (property == sdi->en_st352_c_prop)
		sdi->en_st352_c_val = !!val;
	else if (property == sdi->use_ds2_3ga_prop)
		sdi->use_ds2_3ga_val = !!val;
	else if (property == sdi->c_encoding)
		sdi->c_encoding_prop_val = val;
	else
		return -EINVAL;
	return 0;
}

static int
xlnx_sdi_atomic_get_property(struct drm_connector *connector,
			     const struct drm_connector_state *state,
			     struct drm_property *property, uint64_t *val)
{
	struct xlnx_sdi *sdi = connector_to_sdi(connector);

	if (property == sdi->sdi_mode)
		*val = sdi->sdi_mod_prop_val;
	else if (property == sdi->sdi_data_strm)
		*val =  sdi->sdi_data_strm_prop_val;
	else if (property == sdi->sdi_420_in)
		*val = sdi->sdi_420_in_val;
	else if (property == sdi->sdi_420_out)
		*val = sdi->sdi_420_out_val;
	else if (property == sdi->is_frac_prop)
		*val =  sdi->is_frac_prop_val;
	else if (property == sdi->height_out)
		*val = sdi->height_out_prop_val;
	else if (property == sdi->width_out)
		*val = sdi->width_out_prop_val;
	else if (property == sdi->in_fmt)
		*val = sdi->in_fmt_prop_val;
	else if (property == sdi->out_fmt)
		*val = sdi->out_fmt_prop_val;
	else if (property == sdi->en_st352_c_prop)
		*val =  sdi->en_st352_c_val;
	else if (property == sdi->use_ds2_3ga_prop)
		*val =  sdi->use_ds2_3ga_val;
	else if (property == sdi->c_encoding)
		*val = sdi->c_encoding_prop_val;
	else
		return -EINVAL;

	return 0;
}

/**
 * xlnx_sdi_get_mode_id - Search for a video mode in the supported modes table
 *
 * @mode: mode being searched
 *
 * Return: mode id if mode is found OR -EINVAL otherwise
 */
static int xlnx_sdi_get_mode_id(struct drm_display_mode *mode)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xlnx_sdi_modes); i++)
		if (xlnx_sdi_modes[i].mode.htotal == mode->htotal &&
		    xlnx_sdi_modes[i].mode.vtotal == mode->vtotal &&
		    xlnx_sdi_modes[i].mode.clock == mode->clock &&
		    xlnx_sdi_modes[i].mode.flags == mode->flags)
			return i;
	return -EINVAL;
}

/**
 * xlnx_sdi_drm_add_modes - Adds SDI supported modes
 * @connector: pointer Xilinx SDI connector
 *
 * Return:	Count of modes added
 *
 * This function adds the SDI modes supported and returns its count
 */
static int xlnx_sdi_drm_add_modes(struct drm_connector *connector)
{
	int num_modes = 0;
	u32 i;
	struct drm_display_mode *mode;
	struct drm_device *dev = connector->dev;

	for (i = 0; i < ARRAY_SIZE(xlnx_sdi_modes); i++) {
		const struct drm_display_mode *ptr = &xlnx_sdi_modes[i].mode;

		mode = drm_mode_duplicate(dev, ptr);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}
	return num_modes;
}

static enum drm_connector_status
xlnx_sdi_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void xlnx_sdi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	connector->dev = NULL;
}

static const struct drm_connector_funcs xlnx_sdi_connector_funcs = {
	.detect = xlnx_sdi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = xlnx_sdi_connector_destroy,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_set_property = xlnx_sdi_atomic_set_property,
	.atomic_get_property = xlnx_sdi_atomic_get_property,
};

static struct drm_encoder *
xlnx_sdi_best_encoder(struct drm_connector *connector)
{
	return &(connector_to_sdi(connector)->encoder);
}

static int xlnx_sdi_get_modes(struct drm_connector *connector)
{
	return xlnx_sdi_drm_add_modes(connector);
}

static int xlnx_sdi_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		mode->vdisplay /= 2;

	return MODE_OK;
}

static struct drm_connector_helper_funcs xlnx_sdi_connector_helper_funcs = {
	.get_modes = xlnx_sdi_get_modes,
	.best_encoder = xlnx_sdi_best_encoder,
	.mode_valid = xlnx_sdi_mode_valid,
};

/**
 * xlnx_sdi_drm_connector_create_property -  create SDI connector properties
 *
 * @base_connector: pointer to Xilinx SDI connector
 *
 * This function takes the xilinx SDI connector component and defines
 * the drm_property variables with their default values.
 */
static void
xlnx_sdi_drm_connector_create_property(struct drm_connector *base_connector)
{
	struct drm_device *dev = base_connector->dev;
	struct xlnx_sdi *sdi  = connector_to_sdi(base_connector);

	sdi->is_frac_prop = drm_property_create_bool(dev, 0, "is_frac");
	sdi->sdi_mode = drm_property_create_range(dev, 0,
						  "sdi_mode", 0, 5);
	sdi->sdi_data_strm = drm_property_create_range(dev, 0,
						       "sdi_data_stream", 2, 8);
	sdi->sdi_420_in = drm_property_create_bool(dev, 0, "sdi_420_in");
	sdi->sdi_420_out = drm_property_create_bool(dev, 0, "sdi_420_out");
	sdi->height_out = drm_property_create_range(dev, 0,
						    "height_out", 2, 4096);
	sdi->width_out = drm_property_create_range(dev, 0,
						   "width_out", 2, 4096);
	sdi->in_fmt = drm_property_create_range(dev, 0,
						"in_fmt", 0, 16384);
	sdi->out_fmt = drm_property_create_range(dev, 0,
						 "out_fmt", 0, 16384);
	if (sdi->enable_st352_chroma) {
		sdi->en_st352_c_prop = drm_property_create_bool(dev, 0,
								"en_st352_c");
		sdi->use_ds2_3ga_prop = drm_property_create_bool(dev, 0,
								 "use_ds2_3ga");
	}
	sdi->c_encoding = drm_property_create_bool(dev, 0, "c_encoding");
}

/**
 * xlnx_sdi_drm_connector_attach_property -  attach SDI connector
 * properties
 *
 * @base_connector: pointer to Xilinx SDI connector
 */
static void
xlnx_sdi_drm_connector_attach_property(struct drm_connector *base_connector)
{
	struct xlnx_sdi *sdi = connector_to_sdi(base_connector);
	struct drm_mode_object *obj = &base_connector->base;

	if (sdi->sdi_mode)
		drm_object_attach_property(obj, sdi->sdi_mode, 0);

	if (sdi->sdi_data_strm)
		drm_object_attach_property(obj, sdi->sdi_data_strm, 0);

	if (sdi->sdi_420_in)
		drm_object_attach_property(obj, sdi->sdi_420_in, 0);

	if (sdi->sdi_420_out)
		drm_object_attach_property(obj, sdi->sdi_420_out, 0);

	if (sdi->is_frac_prop)
		drm_object_attach_property(obj, sdi->is_frac_prop, 0);

	if (sdi->height_out)
		drm_object_attach_property(obj, sdi->height_out, 0);

	if (sdi->width_out)
		drm_object_attach_property(obj, sdi->width_out, 0);

	if (sdi->in_fmt)
		drm_object_attach_property(obj, sdi->in_fmt, 0);

	if (sdi->out_fmt)
		drm_object_attach_property(obj, sdi->out_fmt, 0);

	if (sdi->en_st352_c_prop)
		drm_object_attach_property(obj, sdi->en_st352_c_prop, 0);

	if (sdi->use_ds2_3ga_prop)
		drm_object_attach_property(obj, sdi->use_ds2_3ga_prop, 0);

	if (sdi->c_encoding)
		drm_object_attach_property(obj, sdi->c_encoding, 0);

	drm_object_attach_property(obj,
				   base_connector->dev->mode_config.gen_hdr_output_metadata_property, 0);
}

static int xlnx_sdi_create_connector(struct drm_encoder *encoder)
{
	struct xlnx_sdi *sdi = encoder_to_sdi(encoder);
	struct drm_connector *connector = &sdi->connector;
	int ret;

	connector->interlace_allowed = true;
	connector->doublescan_allowed = true;

	ret = drm_connector_init(encoder->dev, connector,
				 &xlnx_sdi_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret) {
		dev_err(sdi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &xlnx_sdi_connector_helper_funcs);
	drm_connector_register(connector);
	drm_connector_attach_encoder(connector, encoder);
	xlnx_sdi_drm_connector_create_property(connector);
	xlnx_sdi_drm_connector_attach_property(connector);

	/* Fill out the supported EOTFs */
	connector->hdr_sink_metadata.hdmi_type1.eotf |=
		BIT(V4L2_EOTF_BT_2100_HLG) |
		BIT(V4L2_EOTF_TRADITIONAL_GAMMA_SDR) |
		BIT(V4L2_EOTF_SMPTE_ST2084);

	return 0;
}

/**
 * xlnx_sdi_set_display_enable - Enables the SDI Tx IP core enable
 * register bit
 * @sdi: SDI structure having the updated user parameters
 *
 * This function takes the SDI strucure and enables the core enable bit
 * of core configuration register.
 */
static void xlnx_sdi_set_display_enable(struct xlnx_sdi *sdi)
{
	u32 data;

	data = xlnx_sdi_readl(sdi->base, XSDI_TX_RST_CTRL);
	data |= XSDI_TX_CTRL_EN;
	xlnx_sdi_writel(sdi->base, XSDI_TX_RST_CTRL, data);
}

/**
 * xlnx_sdi_calc_st352_payld -  calculate the st352 payload
 *
 * @sdi: pointer to SDI Tx structure
 * @mode: DRM display mode
 *
 * This function calculates the st352 payload to be configured.
 * Please refer to SMPTE ST352 documents for it.
 * Return:	return st352 payload
 */
static u32 xlnx_sdi_calc_st352_payld(struct xlnx_sdi *sdi,
				     struct drm_display_mode *mode)
{
	u8 byt1, byt2;
	u16 is_p;
	int id;
	u32 sdi_mode = sdi->sdi_mod_prop_val;
	bool is_frac = sdi->is_frac_prop_val;
	u32 byt3 = ST352_BYTE3;

	id = xlnx_sdi_get_mode_id(mode);
	dev_dbg(sdi->dev, "mode id: %d\n", id);
	if (mode->hdisplay == 2048 || mode->hdisplay == 4096)
		byt3 |= XST352_2048_SHIFT;
	if (sdi->sdi_420_in_val)
		byt3 |= XST352_YUV420_MASK;

	/* byte 2 calculation */
	is_p = !(mode->flags & DRM_MODE_FLAG_INTERLACE);
	byt2 = xlnx_sdi_modes[id].st352_byt2[is_frac];
	if (sdi_mode == XSDI_MODE_3GB ||
	    (mode->flags & DRM_MODE_FLAG_DBLSCAN) || is_p)
		byt2 |= XST352_PROG_PIC;
	if (is_p && mode->vtotal >= 1125)
		byt2 |= XST352_PROG_TRANS;

	/* byte 1 calculation */
	byt1 = xlnx_sdi_modes[id].st352_byt1[sdi_mode];

	return (ST352_BYTE4 << 24 | byt3 << 16 | byt2 << 8 | byt1);
}

static void xlnx_sdi_setup(struct xlnx_sdi *sdi)
{
	u32 reg;

	dev_dbg(sdi->dev, "%s\n", __func__);

	reg = xlnx_sdi_readl(sdi->base, XSDI_TX_MDL_CTRL);
	reg |= XSDI_TX_CTRL_INS_CRC | XSDI_TX_CTRL_INS_ST352 |
		XSDI_TX_CTRL_OVR_ST352 | XSDI_TX_CTRL_INS_SYNC_BIT |
		XSDI_TX_CTRL_INS_EDH;

	if (sdi->enable_anc_data)
		reg |= XSDI_TX_CTRL_USE_ANC_IN;

	if (sdi->enable_st352_chroma) {
		if (sdi->en_st352_c_val) {
			reg |= XSDI_TX_CTRL_INS_ST352_CHROMA;
			if (sdi->use_ds2_3ga_val)
				reg |= XSDI_TX_CTRL_USE_DS2_3GA;
			else
				reg &= ~XSDI_TX_CTRL_USE_DS2_3GA;
		} else {
			reg &= ~XSDI_TX_CTRL_INS_ST352_CHROMA;
			reg &= ~XSDI_TX_CTRL_USE_DS2_3GA;
		}
	}

	xlnx_sdi_writel(sdi->base, XSDI_TX_MDL_CTRL, reg);
	xlnx_sdi_writel(sdi->base, XSDI_TX_IER_STAT, XSDI_IER_EN_MASK);
	xlnx_sdi_writel(sdi->base, XSDI_TX_GLBL_IER, 1);
	xlnx_stc_reset(sdi->base);
}

/**
 * xlnx_sdi_encoder_atomic_mode_set -  drive the SDI timing parameters
 *
 * @encoder: pointer to Xilinx DRM encoder
 * @crtc_state: DRM crtc state
 * @connector_state: DRM connector state
 *
 * This function derives the SDI IP timing parameters from the timing
 * values given to timing module.
 */
static void xlnx_sdi_encoder_atomic_mode_set(struct drm_encoder *encoder,
					     struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *connector_state)
{
	struct xlnx_sdi *sdi = encoder_to_sdi(encoder);
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct videomode vm;
	u32 payload, i;
	u32 sditx_blank, vtc_blank;
	unsigned long clkrate;
	int ret;

	/*
	 * For the transceiver TX, for integer and fractional frame rate, the
	 * PLL ref clock must be a different frequency. Other than SD mode
	 * its 148.5MHz for an integer & 148.5/1.001 for fractional framerate.
	 * Program clocks followed by reset, if picxo is not enabled.
	 */
	if (!sdi->picxo_enabled) {
		if (sdi->is_frac_prop_val &&
		    sdi->sdi_mod_prop_val != XSDI_MODE_SD)
			clkrate = (CLK_RATE * 1000) / 1001;
		else
			clkrate = CLK_RATE;
		ret = clk_set_rate(sdi->sditx_clk, clkrate);
		if (ret)
			dev_err(sdi->dev, "failed to set clk rate = %lu\n",
				clkrate);
		clkrate = clk_get_rate(sdi->sditx_clk);
		dev_info(sdi->dev, "clkrate = %lu is_frac = %d\n", clkrate,
			 sdi->is_frac_prop_val);
		/*
		 * Delay required to get QPLL1 lock as per the si5328
		 * datasheet
		 */
		mdelay(50);
		xlnx_sdi_gt_reset(sdi);
	}

	/* Set timing parameters as per bridge output parameters */
	xlnx_bridge_set_input(sdi->bridge, adjusted_mode->hdisplay,
			      adjusted_mode->vdisplay, sdi->in_fmt_prop_val);
	xlnx_bridge_set_output(sdi->bridge, sdi->width_out_prop_val,
			       sdi->height_out_prop_val, sdi->out_fmt_prop_val);
	xlnx_bridge_enable(sdi->bridge);

	if (sdi->bridge) {
		for (i = 0; i < ARRAY_SIZE(xlnx_sdi_modes); i++) {
			if (xlnx_sdi_modes[i].mode.hdisplay ==
			    sdi->width_out_prop_val &&
			    xlnx_sdi_modes[i].mode.vdisplay ==
			    sdi->height_out_prop_val &&
			    xlnx_sdi_modes[i].mode.vrefresh ==
			    adjusted_mode->vrefresh) {
				memcpy((char *)adjusted_mode +
				       offsetof(struct drm_display_mode,
						clock),
				       &xlnx_sdi_modes[i].mode.clock,
				       SDI_TIMING_PARAMS_SIZE);
				break;
			}
		}
	}

	xlnx_sdi_setup(sdi);
	xlnx_sdi_set_config_parameters(sdi);

	/* set st352 payloads */
	payload = xlnx_sdi_calc_st352_payld(sdi, adjusted_mode);
	dev_dbg(sdi->dev, "payload : %0x\n", payload);

	for (i = 0; i < sdi->sdi_data_strm_prop_val / 2; i++) {
		if (sdi->sdi_mod_prop_val == XSDI_MODE_3GB)
			payload |= (i << 1) << XSDI_CH_SHIFT;
		xlnx_sdi_set_payload_data(sdi, i, payload);
	}

	/* UHDSDI is fixed 2 pixels per clock, horizontal timings div by 2 */
	vm.hactive = adjusted_mode->hdisplay / PIXELS_PER_CLK;
	vm.hfront_porch = (adjusted_mode->hsync_start -
			  adjusted_mode->hdisplay) / PIXELS_PER_CLK;
	vm.hback_porch = (adjusted_mode->htotal -
			 adjusted_mode->hsync_end) / PIXELS_PER_CLK;
	vm.hsync_len = (adjusted_mode->hsync_end -
		       adjusted_mode->hsync_start) / PIXELS_PER_CLK;

	vm.vactive = adjusted_mode->vdisplay;
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vm.vfront_porch = adjusted_mode->vsync_start / 2 -
				  adjusted_mode->vdisplay;
		vm.vback_porch = (adjusted_mode->vtotal -
				  adjusted_mode->vsync_end) / 2;
		vm.vsync_len = (adjusted_mode->vsync_end -
				adjusted_mode->vsync_start) / 2;
	} else {
		vm.vfront_porch = adjusted_mode->vsync_start -
				  adjusted_mode->vdisplay;
		vm.vback_porch = adjusted_mode->vtotal -
				 adjusted_mode->vsync_end;
		vm.vsync_len = adjusted_mode->vsync_end -
			       adjusted_mode->vsync_start;
	}

	vm.flags = 0;
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		vm.flags |= DISPLAY_FLAGS_INTERLACED;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		vm.flags |= DISPLAY_FLAGS_HSYNC_LOW;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		vm.flags |= DISPLAY_FLAGS_VSYNC_LOW;

	do {
		sditx_blank = (adjusted_mode->hsync_start -
			       adjusted_mode->hdisplay) +
			      (adjusted_mode->hsync_end -
			       adjusted_mode->hsync_start) +
			      (adjusted_mode->htotal -
			       adjusted_mode->hsync_end);

		vtc_blank = (vm.hfront_porch + vm.hback_porch +
			     vm.hsync_len) * PIXELS_PER_CLK;

		if (vtc_blank != sditx_blank)
			vm.hfront_porch++;
	} while (vtc_blank < sditx_blank);

	vm.pixelclock = adjusted_mode->clock * 1000;

	/* parameters for sdi audio */
	sdi->video_mode.vdisplay = adjusted_mode->vdisplay;
	sdi->video_mode.hdisplay = adjusted_mode->hdisplay;
	sdi->video_mode.vrefresh = adjusted_mode->vrefresh;
	sdi->video_mode.flags = adjusted_mode->flags;

	xlnx_stc_sig(sdi->base, &vm);
}

static void xlnx_sdi_commit(struct drm_encoder *encoder)
{
	struct xlnx_sdi *sdi = encoder_to_sdi(encoder);
	long ret;

	dev_dbg(sdi->dev, "%s\n", __func__);
	xlnx_sdi_set_display_enable(sdi);
	ret = wait_event_interruptible_timeout(sdi->wait_event,
					       sdi->event_received,
					       usecs_to_jiffies(GT_TIMEOUT));
	if (!ret) {
		dev_err(sdi->dev, "Timeout: GT interrupt not received\n");
		return;
	}
	sdi->event_received = false;
	/* enable sdi bridge, timing controller and Axi4s_vid_out_ctrl */
	xlnx_sdi_en_bridge(sdi);
	xlnx_stc_enable(sdi->base);
	xlnx_sdi_en_axi4s(sdi);
}

static void xlnx_sdi_disable(struct drm_encoder *encoder)
{
	struct xlnx_sdi *sdi = encoder_to_sdi(encoder);

	if (sdi->bridge)
		xlnx_bridge_disable(sdi->bridge);

	xlnx_sdi_set_display_disable(sdi);
	xlnx_stc_disable(sdi->base);
}

static const struct drm_encoder_helper_funcs xlnx_sdi_encoder_helper_funcs = {
	.atomic_mode_set	= xlnx_sdi_encoder_atomic_mode_set,
	.enable			= xlnx_sdi_commit,
	.disable		= xlnx_sdi_disable,
};

static const struct drm_encoder_funcs xlnx_sdi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int xlnx_sdi_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct xlnx_sdi *sdi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &sdi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	/*
	 * TODO: The possible CRTCs are 1 now as per current implementation of
	 * SDI tx drivers. DRM framework can support more than one CRTCs and
	 * SDI driver can be enhanced for that.
	 */
	encoder->possible_crtcs = 1;

	drm_encoder_init(drm_dev, encoder, &xlnx_sdi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	drm_encoder_helper_add(encoder, &xlnx_sdi_encoder_helper_funcs);

	ret = xlnx_sdi_create_connector(encoder);
	if (ret) {
		dev_err(sdi->dev, "fail creating connector, ret = %d\n", ret);
		drm_encoder_cleanup(encoder);
	}
	return ret;
}

static void xlnx_sdi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct xlnx_sdi *sdi = dev_get_drvdata(dev);

	xlnx_sdi_set_display_disable(sdi);
	xlnx_stc_disable(sdi->base);
	drm_encoder_cleanup(&sdi->encoder);
	drm_connector_cleanup(&sdi->connector);
	xlnx_bridge_disable(sdi->bridge);
}

static const struct component_ops xlnx_sdi_component_ops = {
	.bind	= xlnx_sdi_bind,
	.unbind	= xlnx_sdi_unbind,
};

static int xlnx_sdi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct xlnx_sdi *sdi;
	struct device_node *vpss_node;
	int ret, irq;
	struct device_node *ports, *port;
	u32 nports = 0, portmask = 0;
	unsigned long clkrate = 0;
	enum gpiod_flags flags;

	sdi = devm_kzalloc(dev, sizeof(*sdi), GFP_KERNEL);
	if (!sdi)
		return -ENOMEM;

	sdi->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(sdi->base)) {
		dev_err(dev, "failed to remap io region\n");
		return PTR_ERR(sdi->base);
	}
	platform_set_drvdata(pdev, sdi);

	sdi->axi_clk = devm_clk_get(dev, "s_axi_aclk");
	if (IS_ERR(sdi->axi_clk)) {
		ret = PTR_ERR(sdi->axi_clk);
		dev_err(dev, "failed to get s_axi_aclk %d\n", ret);
		return ret;
	}

	sdi->sditx_clk = devm_clk_get(dev, "sdi_tx_clk");
	if (IS_ERR(sdi->sditx_clk)) {
		ret = PTR_ERR(sdi->sditx_clk);
		dev_err(dev, "failed to get sdi_tx_clk %d\n", ret);
		return ret;
	}

	sdi->vidin_clk = devm_clk_get(dev, "video_in_clk");
	if (IS_ERR(sdi->vidin_clk)) {
		ret = PTR_ERR(sdi->vidin_clk);
		dev_err(dev, "failed to get video_in_clk %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(sdi->axi_clk);
	if (ret) {
		dev_err(dev, "failed to enable axi_clk %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(sdi->sditx_clk);
	if (ret) {
		dev_err(dev, "failed to enable sditx_clk %d\n", ret);
		goto err_disable_axi_clk;
	}

	ret = clk_prepare_enable(sdi->vidin_clk);
	if (ret) {
		dev_err(dev, "failed to enable vidin_clk %d\n", ret);
		goto err_disable_sditx_clk;
	}

	sdi->qpll1_enabled = of_property_read_bool(sdi->dev->of_node,
						   "xlnx,qpll1_enabled");

	sdi->picxo_enabled = of_property_read_bool(sdi->dev->of_node,
						   "xlnx,picxo_enabled");
	dev_dbg(dev, "sdi-tx: value of qpll1_en = %d picxo_en = %d\n",
		sdi->qpll1_enabled, sdi->picxo_enabled);

	if (sdi->qpll1_enabled)
		flags = GPIOD_OUT_LOW;
	else
		flags = GPIOD_OUT_HIGH;

	sdi->gt_rst_gpio = devm_gpiod_get_optional(&pdev->dev, "phy-reset",
						   flags);

	if (IS_ERR(sdi->gt_rst_gpio)) {
		ret = PTR_ERR(sdi->gt_rst_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Unable to get phy gpio\n");
		goto err_disable_vidin_clk;
	}

	ret = clk_set_rate(sdi->sditx_clk, CLK_RATE);
	if (ret)
		dev_err(sdi->dev, "failed to set clk rate = %lu\n", CLK_RATE);

	clkrate = clk_get_rate(sdi->sditx_clk);
	dev_dbg(sdi->dev, "clkrate = %lu\n", clkrate);

	/* in case all "port" nodes are grouped under a "ports" node */
	ports = of_get_child_by_name(sdi->dev->of_node, "ports");
	if (!ports) {
		dev_dbg(dev, "Searching for port nodes in device node.\n");
		ports = sdi->dev->of_node;
	}

	for_each_child_of_node(ports, port) {
		struct device_node *endpoint;
		u32 index;

		if (!port->name || of_node_cmp(port->name, "port")) {
			dev_dbg(dev, "port name is null or node name is not port!\n");
			continue;
		}

		endpoint = of_get_next_child(port, NULL);
		if (!endpoint) {
			dev_err(dev, "No remote port at %s\n", port->name);
			of_node_put(endpoint);
			ret = -EINVAL;
			goto err_disable_vidin_clk;
		}

		of_node_put(endpoint);

		ret = of_property_read_u32(port, "reg", &index);
		if (ret) {
			dev_err(dev, "reg property not present - %d\n", ret);
			goto err_disable_vidin_clk;
		}

		portmask |= (1 << index);

		nports++;
	}

	if (nports == 2 && portmask & 0x3) {
		dev_dbg(dev, "enable ancillary port\n");
		sdi->enable_anc_data = true;
	} else if (nports == 1 && portmask & 0x1) {
		dev_dbg(dev, "no ancillary port\n");
		sdi->enable_anc_data = false;
	} else {
		dev_err(dev, "Incorrect dt node!\n");
		ret = -EINVAL;
		goto err_disable_vidin_clk;
	}

	sdi->enable_st352_chroma = of_property_read_bool(sdi->dev->of_node,
							 "xlnx,tx-insert-c-str-st352");

	/* disable interrupt */
	xlnx_sdi_writel(sdi->base, XSDI_TX_GLBL_IER, 0);
	irq = platform_get_irq_byname(pdev, "sdi_tx_irq");
	if (irq < 0) {
		/*
		 * If there is no IRQ with this name, try to get the first
		 * IRQ defined in the device tree.
		 */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			ret = irq;
			goto err_disable_vidin_clk;
		}
	}

	ret = devm_request_threaded_irq(sdi->dev, irq, NULL,
					xlnx_sdi_irq_handler, IRQF_ONESHOT,
					dev_name(sdi->dev), sdi);
	if (ret < 0)
		goto err_disable_vidin_clk;

	/* initialize the wait queue for GT reset event */
	init_waitqueue_head(&sdi->wait_event);

	/* Bridge support */
	vpss_node = of_parse_phandle(sdi->dev->of_node, "xlnx,vpss", 0);
	if (vpss_node) {
		sdi->bridge = of_xlnx_bridge_get(vpss_node);
		if (!sdi->bridge) {
			dev_info(sdi->dev, "Didn't get bridge instance\n");
			ret = -EPROBE_DEFER;
			goto err_disable_vidin_clk;
		}
	}

	/* video mode properties needed by audio driver are shared to audio
	 * driver through a pointer in platform data. This will be used in
	 * audio driver. The solution may be needed to modify/extend to avoid
	 * probable error scenarios
	 */
	pdev->dev.platform_data = &sdi->video_mode;
	/* Initialize to IP default value */
	sdi->prev_eotf = XST352_BYTE2_EOTF_SDRTV;

	ret = component_add(dev, &xlnx_sdi_component_ops);
	if (ret < 0)
		goto err_disable_vidin_clk;

	return ret;

err_disable_vidin_clk:
	clk_disable_unprepare(sdi->vidin_clk);
err_disable_sditx_clk:
	clk_disable_unprepare(sdi->sditx_clk);
err_disable_axi_clk:
	clk_disable_unprepare(sdi->axi_clk);

	return ret;
}

static int xlnx_sdi_remove(struct platform_device *pdev)
{
	struct xlnx_sdi *sdi = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &xlnx_sdi_component_ops);
	clk_disable_unprepare(sdi->vidin_clk);
	clk_disable_unprepare(sdi->sditx_clk);
	clk_disable_unprepare(sdi->axi_clk);

	return 0;
}

static const struct of_device_id xlnx_sdi_of_match[] = {
	{ .compatible = "xlnx,sdi-tx"},
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_sdi_of_match);

static struct platform_driver sdi_tx_driver = {
	.probe = xlnx_sdi_probe,
	.remove = xlnx_sdi_remove,
	.driver = {
		.name = "xlnx-sdi-tx",
		.of_match_table = xlnx_sdi_of_match,
	},
};

module_platform_driver(sdi_tx_driver);

MODULE_AUTHOR("Saurabh Sengar <saurabhs@xilinx.com>");
MODULE_DESCRIPTION("Xilinx FPGA SDI Tx Driver");
MODULE_LICENSE("GPL v2");

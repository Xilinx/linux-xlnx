// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA MIPI DSI Tx Controller driver.
 *
 * Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 * Author : Saurabh Sengar <saurabhs@xilinx.com>
 *        : Siva Rajesh J <siva.rajesh.jarugula@xilinx.com>
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <video/mipi_display.h>
#include <video/videomode.h>

#include "xlnx_bridge.h"

/* DSI Tx IP registers */
#define XDSI_CCR			0x00
#define XDSI_CCR_COREENB		BIT(0)
#define XDSI_CCR_CRREADY		BIT(2)
#define XDSI_PCR			0x04
#define XDSI_PCR_VIDEOMODE(x)		(((x) & 0x3) << 3)
#define XDSI_PCR_VIDEOMODE_MASK		(0x3 << 3)
#define XDSI_PCR_VIDEOMODE_SHIFT	3
#define XDSI_PCR_BLLPTYPE(x)		((x) << 5)
#define XDSI_PCR_BLLPMODE(x)		((x) << 6)
#define XDSI_PCR_EOTPENABLE(x)		((x) << 13)
#define XDSI_GIER			0x20
#define XDSI_ISR			0x24
#define XDSI_IER			0x28
#define XDSI_CMD			0x30
#define XDSI_CMD_QUEUE_PACKET(x)	((x) & GENMASK(23, 0))
#define XDSI_TIME1			0x50
#define XDSI_TIME1_BLLP_BURST(x)	((x) & GENMASK(15, 0))
#define XDSI_TIME1_HSA(x)		(((x) & GENMASK(15, 0)) << 16)
#define XDSI_TIME2			0x54
#define XDSI_TIME2_VACT(x)		((x) & GENMASK(15, 0))
#define XDSI_TIME2_HACT(x)		(((x) & GENMASK(15, 0)) << 16)
#define XDSI_HACT_MULTIPLIER		GENMASK(1, 0)
#define XDSI_TIME3			0x58
#define XDSI_TIME3_HFP(x)		((x) & GENMASK(15, 0))
#define XDSI_TIME3_HBP(x)		(((x) & GENMASK(15, 0)) << 16)
#define XDSI_TIME4			0x5c
#define XDSI_TIME4_VFP(x)		((x) & GENMASK(7, 0))
#define XDSI_TIME4_VBP(x)		(((x) & GENMASK(7, 0)) << 8)
#define XDSI_TIME4_VSA(x)		(((x) & GENMASK(7, 0)) << 16)
#define XDSI_LTIME			0x60
#define XDSI_BLLP_TIME			0x64
/*
 * XDSI_NUM_DATA_T represents number of data types in the
 * enum mipi_dsi_pixel_format in the MIPI DSI part of DRM framework.
 */
#define XDSI_NUM_DATA_T			4
#define XDSI_VIDEO_MODE_SYNC_PULSE	0x0
#define XDSI_VIDEO_MODE_SYNC_EVENT	0x1
#define XDSI_VIDEO_MODE_BURST		0x2

/**
 * struct xlnx_dsi - Core configuration DSI Tx subsystem device structure
 * @encoder: DRM encoder structure
 * @dsi_host: DSI host device
 * @connector: DRM connector structure
 * @panel_node: MIPI DSI device panel node
 * @panel:  DRM panel structure
 * @dev: device structure
 * @iomem: Base address of DSI subsystem
 * @lanes: number of active data lanes supported by DSI controller
 * @mode_flags: DSI operation mode related flags
 * @format: pixel format for video mode of DSI controller
 * @vm: videomode data structure
 * @mul_factor: multiplication factor for HACT timing parameter
 * @eotp_prop: configurable EoTP DSI parameter
 * @bllp_mode_prop: configurable BLLP mode DSI parameter
 * @bllp_type_prop: configurable BLLP type DSI parameter
 * @video_mode_prop: configurable Video mode DSI parameter
 * @bllp_burst_time_prop: Configurable BLLP time for burst mode
 * @cmd_queue_prop: configurable command queue
 * @eotp_prop_val: configurable EoTP DSI parameter value
 * @bllp_mode_prop_val: configurable BLLP mode DSI parameter value
 * @bllp_type_prop_val: configurable BLLP type DSI parameter value
 * @video_mode_prop_val: configurable Video mode DSI parameter value
 * @bllp_burst_time_prop_val: Configurable BLLP time for burst mode value
 * @cmd_queue_prop_val: configurable command queue value
 * @bridge: bridge structure
 * @height_out: configurable bridge output height parameter
 * @height_out_prop_val: configurable bridge output height parameter value
 * @width_out: configurable bridge output width parameter
 * @width_out_prop_val: configurable bridge output width parameter value
 * @in_fmt: configurable bridge input media format
 * @in_fmt_prop_val: configurable media bus format value
 * @out_fmt: configurable bridge output media format
 * @out_fmt_prop_val: configurable media bus format value
 */
struct xlnx_dsi {
	struct drm_encoder encoder;
	struct mipi_dsi_host dsi_host;
	struct drm_connector connector;
	struct device_node *panel_node;
	struct drm_panel *panel;
	struct device *dev;
	void __iomem *iomem;
	u32 lanes;
	u32 mode_flags;
	enum mipi_dsi_pixel_format format;
	struct videomode vm;
	u32 mul_factor;
	struct drm_property *eotp_prop;
	struct drm_property *bllp_mode_prop;
	struct drm_property *bllp_type_prop;
	struct drm_property *video_mode_prop;
	struct drm_property *bllp_burst_time_prop;
	struct drm_property *cmd_queue_prop;
	bool eotp_prop_val;
	bool bllp_mode_prop_val;
	bool bllp_type_prop_val;
	u32 video_mode_prop_val;
	u32 bllp_burst_time_prop_val;
	u32 cmd_queue_prop_val;
	struct xlnx_bridge *bridge;
	struct drm_property *height_out;
	u32 height_out_prop_val;
	struct drm_property *width_out;
	u32 width_out_prop_val;
	struct drm_property *in_fmt;
	u32 in_fmt_prop_val;
	struct drm_property *out_fmt;
	u32 out_fmt_prop_val;
};

#define host_to_dsi(host) container_of(host, struct xlnx_dsi, dsi_host)
#define connector_to_dsi(c) container_of(c, struct xlnx_dsi, connector)
#define encoder_to_dsi(e) container_of(e, struct xlnx_dsi, encoder)

static inline void xlnx_dsi_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static inline u32 xlnx_dsi_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

/**
 * xlnx_dsi_set_config_parameters - Configure DSI Tx registers with parameters
 * given from user application.
 * @dsi: DSI structure having the updated user parameters
 *
 * This function takes the DSI structure having drm_property parameters
 * configured from  user application and writes them into DSI IP registers.
 */
static void xlnx_dsi_set_config_parameters(struct xlnx_dsi *dsi)
{
	u32 reg;

	reg = XDSI_PCR_EOTPENABLE(dsi->eotp_prop_val);
	reg |= XDSI_PCR_VIDEOMODE(dsi->video_mode_prop_val);
	reg |= XDSI_PCR_BLLPTYPE(dsi->bllp_type_prop_val);
	reg |= XDSI_PCR_BLLPMODE(dsi->bllp_mode_prop_val);

	xlnx_dsi_writel(dsi->iomem, XDSI_PCR, reg);
	/*
	 * Configure the burst time if video mode is burst.
	 * HSA of TIME1 register is ignored in this mode.
	 */
	if (dsi->video_mode_prop_val == XDSI_VIDEO_MODE_BURST) {
		reg = XDSI_TIME1_BLLP_BURST(dsi->bllp_burst_time_prop_val);
		xlnx_dsi_writel(dsi->iomem, XDSI_TIME1, reg);
	}

	reg = XDSI_CMD_QUEUE_PACKET(dsi->cmd_queue_prop_val);
	xlnx_dsi_writel(dsi->iomem, XDSI_CMD, reg);

	dev_dbg(dsi->dev, "PCR register value is = %x\n",
		xlnx_dsi_readl(dsi->iomem, XDSI_PCR));
}

/**
 * xlnx_dsi_set_display_mode - Configure DSI timing registers
 * @dsi: DSI structure having the updated user parameters
 *
 * This function writes the timing parameters of DSI IP which are
 * retrieved from panel timing values.
 */
static void xlnx_dsi_set_display_mode(struct xlnx_dsi *dsi)
{
	struct videomode *vm = &dsi->vm;
	u32 reg, video_mode;

	reg = xlnx_dsi_readl(dsi->iomem, XDSI_PCR);
	video_mode = (reg & XDSI_PCR_VIDEOMODE_MASK) >>
		      XDSI_PCR_VIDEOMODE_SHIFT;

	/* configure the HSA value only if non_burst_sync_pluse video mode */
	if (!video_mode &&
	    (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)) {
		reg = XDSI_TIME1_HSA(vm->hsync_len);
		xlnx_dsi_writel(dsi->iomem, XDSI_TIME1, reg);
	}

	reg = XDSI_TIME4_VFP(vm->vfront_porch) |
	      XDSI_TIME4_VBP(vm->vback_porch) |
	      XDSI_TIME4_VSA(vm->vsync_len);
	xlnx_dsi_writel(dsi->iomem, XDSI_TIME4, reg);

	reg = XDSI_TIME3_HFP(vm->hfront_porch) |
	      XDSI_TIME3_HBP(vm->hback_porch);
	xlnx_dsi_writel(dsi->iomem, XDSI_TIME3, reg);

	dev_dbg(dsi->dev, "mul factor for parsed datatype is = %d\n",
		(dsi->mul_factor) / 100);
	/*
	 * The HACT parameter received from panel timing values should be
	 * divisible by 4. The reason for this is, the word count given as
	 * input to DSI controller is HACT * mul_factor. The mul_factor is
	 * 3, 2.25, 2.25, 2 respectively for RGB888, RGB666_L, RGB666_P and
	 * RGB565.
	 * e.g. for RGB666_L color format and 1080p, the word count is
	 * 1920*2.25 = 4320 which is divisible by 4 and it is a valid input
	 * to DSI controller. Based on this 2.25 mul factor, we come up with
	 * the division factor of (XDSI_HACT_MULTIPLIER) as 4 for checking
	 */
	if ((vm->hactive & XDSI_HACT_MULTIPLIER) != 0)
		dev_warn(dsi->dev, "Incorrect HACT will be programmed\n");

	reg = XDSI_TIME2_HACT((vm->hactive) * (dsi->mul_factor) / 100) |
	      XDSI_TIME2_VACT(vm->vactive);
	xlnx_dsi_writel(dsi->iomem, XDSI_TIME2, reg);

	dev_dbg(dsi->dev, "LCD size = %dx%d\n", vm->hactive, vm->vactive);
}

/**
 * xlnx_dsi_set_display_enable - Enables the DSI Tx IP core enable
 * register bit
 * @dsi: DSI structure having the updated user parameters
 *
 * This function takes the DSI strucure and enables the core enable bit
 * of core configuration register.
 */
static void xlnx_dsi_set_display_enable(struct xlnx_dsi *dsi)
{
	u32 reg;

	reg = xlnx_dsi_readl(dsi->iomem, XDSI_CCR);
	reg |= XDSI_CCR_COREENB;

	xlnx_dsi_writel(dsi->iomem, XDSI_CCR, reg);
	dev_dbg(dsi->dev, "MIPI DSI Tx controller is enabled.\n");
}

/**
 * xlnx_dsi_set_display_disable - Disable the DSI Tx IP core enable
 * register bit
 * @dsi: DSI structure having the updated user parameters
 *
 * This function takes the DSI strucure and disables the core enable bit
 * of core configuration register.
 */
static void xlnx_dsi_set_display_disable(struct xlnx_dsi *dsi)
{
	u32 reg;

	reg = xlnx_dsi_readl(dsi->iomem, XDSI_CCR);
	reg &= ~XDSI_CCR_COREENB;

	xlnx_dsi_writel(dsi->iomem, XDSI_CCR, reg);
	dev_dbg(dsi->dev, "DSI Tx is disabled. reset regs to default values\n");
}

/**
 * xlnx_dsi_atomic_set_property - implementation of drm_connector_funcs
 * set_property invoked by IOCTL call to DRM_IOCTL_MODE_OBJ_SETPROPERTY
 *
 * @connector: pointer Xilinx DSI connector
 * @state: DRM connector state
 * @prop: pointer to the drm_property structure
 * @val: DSI parameter value that is configured from user application
 *
 * This function takes a drm_property name and value given from user application
 * and update the DSI structure property varabiles with the values.
 * These values are later used to configure the DSI Rx IP.
 *
 * Return: 0 on success OR -EINVAL if setting property fails
 */
static int xlnx_dsi_atomic_set_property(struct drm_connector *connector,
					struct drm_connector_state *state,
					struct drm_property *prop, u64 val)
{
	struct xlnx_dsi *dsi = connector_to_dsi(connector);

	dev_dbg(dsi->dev, "property name = %s, value = %lld\n",
		prop->name, val);

	if (prop == dsi->eotp_prop)
		dsi->eotp_prop_val = !!val;
	else if (prop == dsi->bllp_mode_prop)
		dsi->bllp_mode_prop_val = !!val;
	else if (prop == dsi->bllp_type_prop)
		dsi->bllp_type_prop_val = !!val;
	else if (prop == dsi->video_mode_prop)
		dsi->video_mode_prop_val = (unsigned int)val;
	else if (prop == dsi->bllp_burst_time_prop)
		dsi->bllp_burst_time_prop_val = (unsigned int)val;
	else if (prop == dsi->cmd_queue_prop)
		dsi->cmd_queue_prop_val = (unsigned int)val;
	else if (prop == dsi->height_out)
		dsi->height_out_prop_val = (u32)val;
	else if (prop == dsi->width_out)
		dsi->width_out_prop_val = (u32)val;
	else if (prop == dsi->in_fmt)
		dsi->in_fmt_prop_val = (u32)val;
	else if (prop == dsi->out_fmt)
		dsi->out_fmt_prop_val = (u32)val;
	else
		return -EINVAL;

	xlnx_dsi_set_config_parameters(dsi);

	return 0;
}

static int
xlnx_dsi_atomic_get_property(struct drm_connector *connector,
			     const struct drm_connector_state *state,
			     struct drm_property *prop, uint64_t *val)
{
	struct xlnx_dsi *dsi = connector_to_dsi(connector);

	if (prop == dsi->eotp_prop)
		*val = dsi->eotp_prop_val;
	else if (prop == dsi->bllp_mode_prop)
		*val = dsi->bllp_mode_prop_val;
	else if (prop == dsi->bllp_type_prop)
		*val = dsi->bllp_type_prop_val;
	else if (prop == dsi->video_mode_prop)
		*val = dsi->video_mode_prop_val;
	else if (prop == dsi->bllp_burst_time_prop)
		*val = dsi->bllp_burst_time_prop_val;
	else if (prop == dsi->cmd_queue_prop)
		*val = dsi->cmd_queue_prop_val;
	else if (prop == dsi->height_out)
		*val = dsi->height_out_prop_val;
	else if (prop == dsi->width_out)
		*val = dsi->width_out_prop_val;
	else if (prop == dsi->in_fmt)
		*val = dsi->in_fmt_prop_val;
	else if (prop == dsi->out_fmt)
		*val = dsi->out_fmt_prop_val;
	else
		return -EINVAL;

	return 0;
}

static int xlnx_dsi_host_attach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *device)
{
	u32 panel_lanes;
	struct xlnx_dsi *dsi = host_to_dsi(host);

	panel_lanes = device->lanes;
	dsi->mode_flags = device->mode_flags;
	dsi->panel_node = device->dev.of_node;

	if (panel_lanes != dsi->lanes) {
		dev_err(dsi->dev, "Mismatch of lanes. panel = %d, DSI = %d\n",
			panel_lanes, dsi->lanes);
		return -EINVAL;
	}

	if (dsi->lanes > 4 || dsi->lanes < 1) {
		dev_err(dsi->dev, "%d lanes : invalid xlnx,dsi-num-lanes\n",
			dsi->lanes);
		return -EINVAL;
	}

	if (device->format != dsi->format) {
		dev_err(dsi->dev, "Mismatch of format. panel = %d, DSI = %d\n",
			device->format, dsi->format);
		return -EINVAL;
	}

	if (dsi->connector.dev)
		drm_helper_hpd_irq_event(dsi->connector.dev);

	return 0;
}

static int xlnx_dsi_host_detach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *device)
{
	struct xlnx_dsi *dsi = host_to_dsi(host);

	dsi->panel_node = NULL;

	if (dsi->connector.dev)
		drm_helper_hpd_irq_event(dsi->connector.dev);

	return 0;
}

static const struct mipi_dsi_host_ops xlnx_dsi_ops = {
	.attach = xlnx_dsi_host_attach,
	.detach = xlnx_dsi_host_detach,
};

static int xlnx_dsi_connector_dpms(struct drm_connector *connector, int mode)
{
	struct xlnx_dsi *dsi = connector_to_dsi(connector);
	int ret;

	dev_dbg(dsi->dev, "connector dpms state: %d\n", mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		ret = drm_panel_prepare(dsi->panel);
		if (ret < 0) {
			dev_err(dsi->dev, "DRM panel not found\n");
			return ret;
		}

		ret = drm_panel_enable(dsi->panel);
		if (ret < 0) {
			drm_panel_unprepare(dsi->panel);
			dev_err(dsi->dev, "DRM panel not enabled\n");
			return ret;
		}
		break;
	default:
		drm_panel_disable(dsi->panel);
		drm_panel_unprepare(dsi->panel);
		break;
	}

	return drm_helper_connector_dpms(connector, mode);
}

static enum drm_connector_status
xlnx_dsi_detect(struct drm_connector *connector, bool force)
{
	struct xlnx_dsi *dsi = connector_to_dsi(connector);

	if (!dsi->panel) {
		dsi->panel = of_drm_find_panel(dsi->panel_node);
		if (dsi->panel)
			drm_panel_attach(dsi->panel, &dsi->connector);
	} else if (!dsi->panel_node) {
		xlnx_dsi_connector_dpms(connector, DRM_MODE_DPMS_OFF);
		drm_panel_detach(dsi->panel);
		dsi->panel = NULL;
	}

	if (dsi->panel)
		return connector_status_connected;

	return connector_status_disconnected;
}

static void xlnx_dsi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	connector->dev = NULL;
}

static const struct drm_connector_funcs xlnx_dsi_connector_funcs = {
	.dpms = xlnx_dsi_connector_dpms,
	.detect = xlnx_dsi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = xlnx_dsi_connector_destroy,
	.atomic_set_property = xlnx_dsi_atomic_set_property,
	.atomic_get_property = xlnx_dsi_atomic_get_property,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.reset			= drm_atomic_helper_connector_reset,
};

static int xlnx_dsi_get_modes(struct drm_connector *connector)
{
	struct xlnx_dsi *dsi = connector_to_dsi(connector);

	if (dsi->panel)
		return dsi->panel->funcs->get_modes(dsi->panel);

	return 0;
}

static struct drm_encoder *
xlnx_dsi_best_encoder(struct drm_connector *connector)
{
	return &(connector_to_dsi(connector)->encoder);
}

static struct drm_connector_helper_funcs xlnx_dsi_connector_helper_funcs = {
	.get_modes = xlnx_dsi_get_modes,
	.best_encoder = xlnx_dsi_best_encoder,
};

/**
 * xlnx_dsi_connector_create_property -  create DSI connector properties
 *
 * @connector: pointer to Xilinx DSI connector
 *
 * This function takes the xilinx DSI connector component and defines
 * the drm_property variables with their default values.
 */
static void xlnx_dsi_connector_create_property(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct xlnx_dsi *dsi  = connector_to_dsi(connector);

	dsi->eotp_prop = drm_property_create_bool(dev, 1, "eotp");
	dsi->video_mode_prop = drm_property_create_range(dev, 0, "video_mode",
							 0, 2);
	dsi->bllp_mode_prop = drm_property_create_bool(dev, 0, "bllp_mode");
	dsi->bllp_type_prop = drm_property_create_bool(dev, 0, "bllp_type");
	dsi->bllp_burst_time_prop =
		drm_property_create_range(dev, 0, "bllp_burst_time", 0, 0xFFFF);
	dsi->cmd_queue_prop = drm_property_create_range(dev, 0, "cmd_queue", 0,
							0xffffff);
	dsi->height_out = drm_property_create_range(dev, 0, "height_out",
						    2, 4096);
	dsi->width_out = drm_property_create_range(dev, 0, "width_out",
						   2, 4096);
	dsi->in_fmt = drm_property_create_range(dev, 0, "in_fmt", 0, 16384);
	dsi->out_fmt = drm_property_create_range(dev, 0, "out_fmt", 0, 16384);
}

/**
 * xlnx_dsi_connector_attach_property -  attach DSI connector
 * properties
 *
 * @connector: pointer to Xilinx DSI connector
 */
static void xlnx_dsi_connector_attach_property(struct drm_connector *connector)
{
	struct xlnx_dsi *dsi = connector_to_dsi(connector);
	struct drm_mode_object *obj = &connector->base;

	if (dsi->eotp_prop)
		drm_object_attach_property(obj, dsi->eotp_prop, 1);

	if (dsi->video_mode_prop)
		drm_object_attach_property(obj, dsi->video_mode_prop, 0);

	if (dsi->bllp_burst_time_prop)
		drm_object_attach_property(&connector->base,
					   dsi->bllp_burst_time_prop, 0);

	if (dsi->bllp_mode_prop)
		drm_object_attach_property(&connector->base,
					   dsi->bllp_mode_prop, 0);

	if (dsi->bllp_type_prop)
		drm_object_attach_property(&connector->base,
					   dsi->bllp_type_prop, 0);

	if (dsi->cmd_queue_prop)
		drm_object_attach_property(&connector->base,
					   dsi->cmd_queue_prop, 0);

	if (dsi->height_out)
		drm_object_attach_property(obj, dsi->height_out, 0);

	if (dsi->width_out)
		drm_object_attach_property(obj, dsi->width_out, 0);

	if (dsi->in_fmt)
		drm_object_attach_property(obj, dsi->in_fmt, 0);

	if (dsi->out_fmt)
		drm_object_attach_property(obj, dsi->out_fmt, 0);
}

static int xlnx_dsi_create_connector(struct drm_encoder *encoder)
{
	struct xlnx_dsi *dsi = encoder_to_dsi(encoder);
	struct drm_connector *connector = &dsi->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(encoder->dev, connector,
				 &xlnx_dsi_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		dev_err(dsi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &xlnx_dsi_connector_helper_funcs);
	drm_connector_register(connector);
	drm_mode_connector_attach_encoder(connector, encoder);
	xlnx_dsi_connector_create_property(connector);
	xlnx_dsi_connector_attach_property(connector);

	return 0;
}

/**
 * xlnx_dsi_atomic_mode_set -  derive the DSI timing parameters
 *
 * @encoder: pointer to Xilinx DRM encoder
 * @crtc_state: Pointer to drm core crtc state
 * @connector_state: DSI connector drm state
 *
 * This function derives the DSI IP timing parameters from the timing
 * values given in the attached panel driver.
 */
static void
xlnx_dsi_atomic_mode_set(struct drm_encoder *encoder,
			 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *connector_state)
{
	struct xlnx_dsi *dsi = encoder_to_dsi(encoder);
	struct videomode *vm = &dsi->vm;
	struct drm_display_mode *m = &crtc_state->adjusted_mode;

	/* Set bridge input and output parameters */
	xlnx_bridge_set_input(dsi->bridge, m->hdisplay, m->vdisplay,
			      dsi->in_fmt_prop_val);
	xlnx_bridge_set_output(dsi->bridge, dsi->width_out_prop_val,
			       dsi->height_out_prop_val,
			       dsi->out_fmt_prop_val);
	xlnx_bridge_enable(dsi->bridge);

	vm->hactive = m->hdisplay;
	vm->vactive = m->vdisplay;
	vm->vfront_porch = m->vsync_start - m->vdisplay;
	vm->vback_porch = m->vtotal - m->vsync_end;
	vm->vsync_len = m->vsync_end - m->vsync_start;
	vm->hfront_porch = m->hsync_start - m->hdisplay;
	vm->hback_porch = m->htotal - m->hsync_end;
	vm->hsync_len = m->hsync_end - m->hsync_start;
	xlnx_dsi_set_display_mode(dsi);
}

static void xlnx_dsi_disable(struct drm_encoder *encoder)
{
	struct xlnx_dsi *dsi = encoder_to_dsi(encoder);

	xlnx_dsi_set_display_disable(dsi);
}

static void xlnx_dsi_enable(struct drm_encoder *encoder)
{
	struct xlnx_dsi *dsi = encoder_to_dsi(encoder);

	xlnx_dsi_set_display_enable(dsi);
}

static const struct drm_encoder_helper_funcs xlnx_dsi_encoder_helper_funcs = {
	.atomic_mode_set = xlnx_dsi_atomic_mode_set,
	.enable = xlnx_dsi_enable,
	.disable = xlnx_dsi_disable,
};

static const struct drm_encoder_funcs xlnx_dsi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int xlnx_dsi_parse_dt(struct xlnx_dsi *dsi)
{
	struct device *dev = dsi->dev;
	struct device_node *node = dev->of_node;
	int ret;
	u32 datatype;
	static const int xdsi_mul_fact[XDSI_NUM_DATA_T] = {300, 225, 225, 200};
	/*
	 * Used as a multiplication factor for HACT based on used
	 * DSI data type.
	 *
	 * e.g. for RGB666_L datatype and 1920x1080 resolution,
	 * the Hact (WC) would be as follows -
	 * 1920 pixels * 18 bits per pixel / 8 bits per byte
	 * = 1920 pixels * 2.25 bytes per pixel = 4320 bytes.
	 *
	 * Data Type - Multiplication factor
	 * RGB888    - 3
	 * RGB666_L  - 2.25
-	 * RGB666_P  - 2.25
	 * RGB565    - 2
	 *
	 * Since the multiplication factor maybe a floating number,
	 * a 100x multiplication factor is used.
	 */
	ret = of_property_read_u32(node, "xlnx,dsi-num-lanes", &dsi->lanes);
	if (ret < 0) {
		dev_err(dsi->dev, "missing xlnx,dsi-num-lanes property\n");
		return ret;
	}
	if (dsi->lanes > 4 || dsi->lanes < 1) {
		dev_err(dsi->dev, "%d lanes : invalid lanes\n", dsi->lanes);
		return -EINVAL;
	}
	ret = of_property_read_u32(node, "xlnx,dsi-data-type", &datatype);
	if (ret < 0) {
		dev_err(dsi->dev, "missing xlnx,dsi-data-type property\n");
		return ret;
	}
	dsi->format = datatype;
	if (datatype > MIPI_DSI_FMT_RGB565) {
		dev_err(dsi->dev, "Invalid xlnx,dsi-data-type string\n");
		return -EINVAL;
	}
	dsi->mul_factor = xdsi_mul_fact[datatype];
	dev_dbg(dsi->dev, "DSI controller num lanes = %d", dsi->lanes);
	dev_dbg(dsi->dev, "DSI controller datatype = %d\n", datatype);

	return 0;
}

static int xlnx_dsi_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct xlnx_dsi *dsi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	/*
	 * TODO: The possible CRTCs are 1 now as per current implementation of
	 * DSI tx drivers. DRM framework can support more than one CRTCs and
	 * DSI driver can be enhanced for that.
	 */
	encoder->possible_crtcs = 1;
	drm_encoder_init(drm_dev, encoder, &xlnx_dsi_encoder_funcs,
			 DRM_MODE_ENCODER_DSI, NULL);
	drm_encoder_helper_add(encoder, &xlnx_dsi_encoder_helper_funcs);
	ret = xlnx_dsi_create_connector(encoder);
	if (ret) {
		dev_err(dsi->dev, "fail creating connector, ret = %d\n", ret);
		drm_encoder_cleanup(encoder);
		return ret;
	}
	ret = mipi_dsi_host_register(&dsi->dsi_host);
	if (ret) {
		xlnx_dsi_connector_destroy(&dsi->connector);
		drm_encoder_cleanup(encoder);
		return ret;
	}
	return 0;
}

static void xlnx_dsi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct xlnx_dsi *dsi = dev_get_drvdata(dev);

	xlnx_dsi_disable(&dsi->encoder);
	mipi_dsi_host_unregister(&dsi->dsi_host);
	xlnx_bridge_disable(dsi->bridge);
}

static const struct component_ops xlnx_dsi_component_ops = {
	.bind	= xlnx_dsi_bind,
	.unbind	= xlnx_dsi_unbind,
};

static int xlnx_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct xlnx_dsi *dsi;
	struct device_node *vpss_node;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dsi_host.ops = &xlnx_dsi_ops;
	dsi->dsi_host.dev = dev;
	dsi->dev = dev;

	ret = xlnx_dsi_parse_dt(dsi);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->iomem = devm_ioremap_resource(dev, res);
	if (IS_ERR(dsi->iomem))
		return PTR_ERR(dsi->iomem);

	platform_set_drvdata(pdev, dsi);

	/* Bridge support */
	vpss_node = of_parse_phandle(dsi->dev->of_node, "xlnx,vpss", 0);
	if (vpss_node) {
		dsi->bridge = of_xlnx_bridge_get(vpss_node);
		if (!dsi->bridge) {
			dev_info(dsi->dev, "Didn't get bridge instance\n");
			return -EPROBE_DEFER;
		}
	}

	return component_add(dev, &xlnx_dsi_component_ops);
}

static int xlnx_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &xlnx_dsi_component_ops);

	return 0;
}

static const struct of_device_id xlnx_dsi_of_match[] = {
	{ .compatible = "xlnx,dsi"},
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_dsi_of_match);

static struct platform_driver dsi_driver = {
	.probe = xlnx_dsi_probe,
	.remove = xlnx_dsi_remove,
	.driver = {
		.name = "xlnx-dsi",
		.of_match_table = xlnx_dsi_of_match,
	},
};

module_platform_driver(dsi_driver);

MODULE_AUTHOR("Siva Rajesh <sivaraj@xilinx.com>");
MODULE_DESCRIPTION("Xilinx FPGA MIPI DSI Tx Driver");
MODULE_LICENSE("GPL v2");

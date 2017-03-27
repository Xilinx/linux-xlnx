/*
 * Xilinx FPGA MIPI DSI Tx Controller driver.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Contacts: Siva Rajesh J <siva.rajesh.jarugula@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drmP.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <video/mipi_display.h>
#include <video/videomode.h>

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
#define XDSI_CMD_QUEUE_PACKET(x)	(((x) & 0xffffff) << 0)
#define XDSI_TIME1			0x50
#define XDSI_TIME1_BLLP_BURST(x)	(((x) & 0xffff) << 0)
#define XDSI_TIME1_HSA(x)		(((x) & 0xffff) << 16)
#define XDSI_TIME2			0x54
#define XDSI_TIME2_VACT(x)		(((x) & 0xffff) << 0)
#define XDSI_TIME2_HACT(x)		(((x) & 0xffff) << 16)
#define XDSI_TIME3			0x58
#define XDSI_TIME3_HFP(x)		(((x) & 0xffff) << 0)
#define XDSI_TIME3_HBP(x)		(((x) & 0xffff) << 16)
#define XDSI_TIME4			0x5c
#define XDSI_TIME4_VFP(x)		(((x) & 0xff) << 0)
#define XDSI_TIME4_VBP(x)		(((x) & 0xff) << 8)
#define XDSI_TIME4_VSA(x)		(((x) & 0xff) << 16)
#define XDSI_LTIME			0x60
#define XDSI_BLLP_TIME			0x64
#define XDSI_NUM_DATA_TYPES		5
#define XDSI_NUM_PIXELS_PER_BEAT	3
#define XDSI_VIDEO_MODE_SYNC_PULSE	0x0
#define XDSI_VIDEO_MODE_SYNC_EVENT	0x1
#define XDSI_VIDEO_MODE_BURST		0x2

/*
 * Used as a multiplication factor for HACT based on used
 * DSI data type and pixels per beat.
 * e.g. for RGB666_L with 2 pixels per beat, (6+6+6)*2 = 36.
 * To make it multiples of 8, 36+4 = 40.
 * So, multiplication factor is = 40/8 which gives 5
 */
static const int
xdsi_mul_factor[XDSI_NUM_DATA_TYPES][XDSI_NUM_PIXELS_PER_BEAT] = {
	{ 3, 6, 12 }, /* RGB888 = {1ppb, 2ppb, 4ppb} */
	{ 3, 5, 9 }, /* RGB666_L = {1ppb, 2ppb, 4ppb} */
	{ 3, 5, 9 }, /* RGB666_P = {1ppb, 2ppb, 4ppb} */
	{ 2, 4, 8 }  /* RGB565 = {1ppb, 2ppb, 4ppb} */
};

/*
 * struct xilinx_dsi - Core configuration DSI Tx subsystem device structure
 * @drm_encoder: DRM encoder structure
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
 */
struct xilinx_dsi {
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
};

#define host_to_dsi(host) container_of(host, struct xilinx_dsi, dsi_host)
#define connector_to_dsi(c) container_of(c, struct xilinx_dsi, connector)
#define encoder_to_dsi(e) container_of(e, struct xilinx_dsi, encoder)

static inline void xilinx_dsi_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

static inline u32 xilinx_dsi_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

/**
 * xilinx_dsi_set_default_drm_properties - Configure DSI DRM
 * properties with their default values
 * @dsi: DSI structure having the updated user parameters
 */
static void
xilinx_dsi_set_default_drm_properties(struct xilinx_dsi *dsi)
{
	drm_object_property_set_value(&dsi->connector.base, dsi->eotp_prop, 1);
	drm_object_property_set_value(&dsi->connector.base,
			dsi->bllp_mode_prop, 0);
	drm_object_property_set_value(&dsi->connector.base,
			dsi->bllp_type_prop, 0);
	drm_object_property_set_value(&dsi->connector.base,
			dsi->video_mode_prop, 0);
	drm_object_property_set_value(&dsi->connector.base,
			dsi->bllp_burst_time_prop, 0);
	drm_object_property_set_value(&dsi->connector.base,
			dsi->cmd_queue_prop, 0);
}

/**
 * xilinx_dsi_set_config_parameters - Configure DSI Tx registers with parameters
 * given from user application.
 * @dsi: DSI structure having the updated user parameters
 *
 * This function takes the DSI structure having drm_property parameters
 * configured from  user application and writes them into DSI IP registers.
 */
static void xilinx_dsi_set_config_parameters(struct xilinx_dsi *dsi)
{
	u32 reg = 0;

	reg |= XDSI_PCR_EOTPENABLE(dsi->eotp_prop_val);
	reg |= XDSI_PCR_VIDEOMODE(dsi->video_mode_prop_val);
	reg |= XDSI_PCR_BLLPTYPE(dsi->bllp_type_prop_val);
	reg |= XDSI_PCR_BLLPMODE(dsi->bllp_mode_prop_val);

	xilinx_dsi_writel(dsi->iomem, XDSI_PCR, reg);

	/* Configure the burst time if video mode is burst.
	 * HSA of TIME1 register is ignored in this mode.
	 */
	if (dsi->video_mode_prop_val == XDSI_VIDEO_MODE_BURST) {
		reg = XDSI_TIME1_BLLP_BURST(dsi->bllp_burst_time_prop_val);
		xilinx_dsi_writel(dsi->iomem, XDSI_TIME1, reg);
	}

	reg = XDSI_CMD_QUEUE_PACKET(dsi->cmd_queue_prop_val);
	xilinx_dsi_writel(dsi->iomem, XDSI_CMD, reg);

	dev_dbg(dsi->dev, "PCR register value is = %x\n",
			xilinx_dsi_readl(dsi->iomem, XDSI_PCR));
}

/**
 * xilinx_dsi_set_display_mode - Configure DSI timing registers
 * @dsi: DSI structure having the updated user parameters
 *
 * This function writes the timing parameters of DSI IP which are
 * retrieved from panel timing values.
 */
static void xilinx_dsi_set_display_mode(struct xilinx_dsi *dsi)
{
	struct videomode *vm = &dsi->vm;
	u32 reg, video_mode;

	reg = xilinx_dsi_readl(dsi->iomem, XDSI_PCR);
	video_mode = ((reg & XDSI_PCR_VIDEOMODE_MASK) >>
				XDSI_PCR_VIDEOMODE_SHIFT);

	/* configure the HSA value only if non_burst_sync_pluse video mode */
	if ((!video_mode) &&
	    (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)) {
		reg = XDSI_TIME1_HSA(vm->hsync_len);
		xilinx_dsi_writel(dsi->iomem, XDSI_TIME1, reg);
	}

	reg = XDSI_TIME4_VFP(vm->vfront_porch) |
		XDSI_TIME4_VBP(vm->vback_porch) |
		XDSI_TIME4_VSA(vm->vsync_len);
	xilinx_dsi_writel(dsi->iomem, XDSI_TIME4, reg);

	reg = XDSI_TIME3_HFP(vm->hfront_porch) |
		XDSI_TIME3_HBP(vm->hback_porch);
	xilinx_dsi_writel(dsi->iomem, XDSI_TIME3, reg);

	dev_dbg(dsi->dev, "mul factor for parsed datatype is = %d\n",
			dsi->mul_factor);

	reg = XDSI_TIME2_HACT((vm->hactive) * dsi->mul_factor) |
		XDSI_TIME2_VACT(vm->vactive);
	xilinx_dsi_writel(dsi->iomem, XDSI_TIME2, reg);

	dev_dbg(dsi->dev, "LCD size = %dx%d\n", vm->hactive, vm->vactive);
}

/**
 * xilinx_dsi_set_display_enable - Enables the DSI Tx IP core enable
 * register bit
 * @dsi: DSI structure having the updated user parameters
 *
 * This function takes the DSI strucure and enables the core enable bit
 * of core configuration register.
 */
static void xilinx_dsi_set_display_enable(struct xilinx_dsi *dsi)
{
	u32 reg;

	reg = xilinx_dsi_readl(dsi->iomem, XDSI_CCR);
	reg |= XDSI_CCR_COREENB;

	xilinx_dsi_writel(dsi->iomem, XDSI_CCR, reg);
	dev_dbg(dsi->dev, "MIPI DSI Tx controller is enabled.\n");
}

/**
 * xilinx_dsi_set_display_disable - Disable the DSI Tx IP core enable
 * register bit
 * @dsi: DSI structure having the updated user parameters
 *
 * This function takes the DSI strucure and disables the core enable bit
 * of core configuration register.
 */
static void xilinx_dsi_set_display_disable(struct xilinx_dsi *dsi)
{
	u32 reg;

	reg = xilinx_dsi_readl(dsi->iomem, XDSI_CCR);
	reg &= ~XDSI_CCR_COREENB;

	xilinx_dsi_writel(dsi->iomem, XDSI_CCR, reg);
	dev_dbg(dsi->dev, "DSI Tx is disabled. reset regs to default values\n");
}


static void xilinx_dsi_encoder_dpms(struct drm_encoder *encoder,
					int mode)
{
	struct xilinx_dsi *dsi = encoder_to_dsi(encoder);

	dev_dbg(dsi->dev, "encoder dpms state: %d\n", mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		xilinx_dsi_set_display_enable(dsi);
		break;
	default:
		xilinx_dsi_set_display_disable(dsi);
		xilinx_dsi_set_default_drm_properties(dsi);
		break;
	}
}

/**
 * xilinx_dsi_connector_set_property - implementation of drm_connector_funcs
 * set_property invoked by IOCTL call to DRM_IOCTL_MODE_OBJ_SETPROPERTY
 *
 * @base_connector: pointer Xilinx DSI connector
 * @property: pointer to the drm_property structure
 * @value: DSI parameter value that is configured from user application
 *
 * This function takes a drm_property name and value given from user application
 * and update the DSI structure property varabiles with the values.
 * These values are later used to configure the DSI Rx IP.
 *
 * Return: 0 on success OR -EINVAL if setting property fails
 */
static int
xilinx_dsi_connector_set_property(struct drm_connector *base_connector,
					struct drm_property *property,
					u64 value)
{
	struct xilinx_dsi *dsi = connector_to_dsi(base_connector);

	dev_dbg(dsi->dev, "property name = %s, value = %lld\n",
			property->name, value);

	if (property == dsi->eotp_prop)
		dsi->eotp_prop_val = !!value;
	else if (property == dsi->bllp_mode_prop)
		dsi->bllp_mode_prop_val = !!value;
	else if (property == dsi->bllp_type_prop)
		dsi->bllp_type_prop_val = !!value;
	else if (property == dsi->video_mode_prop)
		dsi->video_mode_prop_val = (unsigned int)value;
	else if (property == dsi->bllp_burst_time_prop)
		dsi->bllp_burst_time_prop_val = (unsigned int)value;
	else if (property == dsi->cmd_queue_prop)
		dsi->cmd_queue_prop_val = (unsigned int)value;
	else
		return -EINVAL;

	xilinx_dsi_set_config_parameters(dsi);

	return 0;
}

static int xilinx_dsi_host_attach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *device)
{
	u32 panel_lanes;
	struct xilinx_dsi *dsi = host_to_dsi(host);

	panel_lanes = device->lanes;
	dsi->mode_flags = device->mode_flags;
	dsi->panel_node = device->dev.of_node;

	if (panel_lanes != dsi->lanes) {
		dev_err(dsi->dev, "Mismatch of lanes. panel = %d, DSI = %d\n",
			panel_lanes, dsi->lanes);
		return -EINVAL;
	}

	if ((dsi->lanes > 4) || (dsi->lanes < 1)) {
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

static int xilinx_dsi_host_detach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *device)
{
	struct xilinx_dsi *dsi = host_to_dsi(host);

	dsi->panel_node = NULL;

	if (dsi->connector.dev)
		drm_helper_hpd_irq_event(dsi->connector.dev);

	return 0;
}

static const struct mipi_dsi_host_ops xilinx_dsi_ops = {
	.attach = xilinx_dsi_host_attach,
	.detach = xilinx_dsi_host_detach,
};

static int xilinx_dsi_connector_dpms(struct drm_connector *connector,
					int mode)
{
	struct xilinx_dsi *dsi = connector_to_dsi(connector);
	int ret;

	dev_dbg(dsi->dev, "connector dpms state: %d\n", mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		ret = drm_panel_prepare(dsi->panel);
		if (ret < 0)
			return ret;

		ret = drm_panel_enable(dsi->panel);
		if (ret < 0) {
			drm_panel_unprepare(dsi->panel);
			dev_err(dsi->dev, "DRM panel not enabled. power off DSI\n");
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
xilinx_dsi_detect(struct drm_connector *connector, bool force)
{
	struct xilinx_dsi *dsi = connector_to_dsi(connector);

	if (!dsi->panel) {
		dsi->panel = of_drm_find_panel(dsi->panel_node);
		if (dsi->panel)
			drm_panel_attach(dsi->panel, &dsi->connector);
	} else if (!dsi->panel_node) {
		xilinx_dsi_connector_dpms(connector, DRM_MODE_DPMS_OFF);
		drm_panel_detach(dsi->panel);
		dsi->panel = NULL;
	}

	if (dsi->panel)
		return connector_status_connected;

	return connector_status_disconnected;
}

static void xilinx_dsi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	connector->dev = NULL;
}

static const struct drm_connector_funcs xilinx_dsi_connector_funcs = {
	.dpms = xilinx_dsi_connector_dpms,
	.detect = xilinx_dsi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = xilinx_dsi_connector_destroy,
	.set_property = xilinx_dsi_connector_set_property,
};

static int xilinx_dsi_get_modes(struct drm_connector *connector)
{
	struct xilinx_dsi *dsi = connector_to_dsi(connector);

	if (dsi->panel)
		return dsi->panel->funcs->get_modes(dsi->panel);

	return 0;
}

static struct drm_encoder *
xilinx_dsi_best_encoder(struct drm_connector *connector)
{
	return &(connector_to_dsi(connector)->encoder);
}

static struct drm_connector_helper_funcs xilinx_dsi_connector_helper_funcs = {
	.get_modes = xilinx_dsi_get_modes,
	.best_encoder = xilinx_dsi_best_encoder,
};

/**
 * xilinx_drm_dsi_connector_create_property -  create DSI connector properties
 *
 * @base_connector: pointer to Xilinx DSI connector
 *
 * This function takes the xilinx DSI connector component and defines
 * the drm_property variables with their default values.
 */
static void
xilinx_drm_dsi_connector_create_property(struct drm_connector *base_connector)
{
	struct drm_device *dev = base_connector->dev;
	struct xilinx_dsi *dsi  = connector_to_dsi(base_connector);

	dsi->eotp_prop = drm_property_create_bool(dev, 1, "eotp");
	dsi->video_mode_prop = drm_property_create_range(dev, 0,
			"video_mode", 0, 2);
	dsi->bllp_mode_prop = drm_property_create_bool(dev, 0, "bllp_mode");
	dsi->bllp_type_prop = drm_property_create_bool(dev, 0, "bllp_type");
	dsi->bllp_burst_time_prop = drm_property_create_range(dev, 0,
			"bllp_burst_time", 0, 0xFFFF);
	dsi->cmd_queue_prop = drm_property_create_range(dev, 0,
			"cmd_queue", 0, 0xFFFFFF);
}

/**
 * xilinx_drm_dsi_connector_attach_property -  attach DSI connector
 * properties
 *
 * @base_connector: pointer to Xilinx DSI connector
 */
static void
xilinx_drm_dsi_connector_attach_property(struct drm_connector *base_connector)
{
	struct xilinx_dsi *dsi = connector_to_dsi(base_connector);
	struct drm_mode_object *obj = &base_connector->base;

	if (dsi->eotp_prop)
		drm_object_attach_property(obj, dsi->eotp_prop, 1);

	if (dsi->video_mode_prop)
		drm_object_attach_property(obj, dsi->video_mode_prop, 0);

	if (dsi->bllp_burst_time_prop)
		drm_object_attach_property(&base_connector->base,
					dsi->bllp_burst_time_prop, 0);

	if (dsi->bllp_mode_prop)
		drm_object_attach_property(&base_connector->base,
					dsi->bllp_mode_prop, 0);

	if (dsi->bllp_type_prop)
		drm_object_attach_property(&base_connector->base,
					dsi->bllp_type_prop, 0);

	if (dsi->cmd_queue_prop)
		drm_object_attach_property(&base_connector->base,
				dsi->cmd_queue_prop, 0);
}

static int xilinx_dsi_create_connector(struct drm_encoder *encoder)
{
	struct xilinx_dsi *dsi = encoder_to_dsi(encoder);
	struct drm_connector *connector = &dsi->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(encoder->dev, connector,
				 &xilinx_dsi_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		dev_err(dsi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &xilinx_dsi_connector_helper_funcs);
	drm_connector_register(connector);
	drm_mode_connector_attach_encoder(connector, encoder);
	xilinx_drm_dsi_connector_create_property(connector);
	xilinx_drm_dsi_connector_attach_property(connector);

	return 0;
}

static bool xilinx_dsi_mode_fixup(struct drm_encoder *encoder,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	return true;
}

/**
 * xilinx_dsi_mode_set -  derive the DSI timing parameters
 *
 * @encoder: pointer to Xilinx DRM encoder
 * @mode: DRM kernel-internal display mode structure
 * @adjusted_mode: DSI panel timing parameters
 *
 * This function derives the DSI IP timing parameters from the timing
 * values given in the attached panel driver.
 */
static void xilinx_dsi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct xilinx_dsi *dsi = encoder_to_dsi(encoder);
	struct videomode *vm = &dsi->vm;
	struct drm_display_mode *m = adjusted_mode;

	vm->hactive = m->hdisplay;
	vm->vactive = m->vdisplay;
	vm->vfront_porch = m->vsync_start - m->vdisplay;
	vm->vback_porch = m->vtotal - m->vsync_end;
	vm->vsync_len = m->vsync_end - m->vsync_start;
	vm->hfront_porch = m->hsync_start - m->hdisplay;
	vm->hback_porch = m->htotal - m->hsync_end;
	vm->hsync_len = m->hsync_end - m->hsync_start;
	xilinx_dsi_set_display_mode(dsi);
}

static void xilinx_dsi_prepare(struct drm_encoder *encoder)
{
	struct xilinx_dsi *dsi = encoder_to_dsi(encoder);

	dev_dbg(dsi->dev, "%s %d\n", __func__, __LINE__);
	xilinx_dsi_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void xilinx_dsi_commit(struct drm_encoder *encoder)
{
	struct xilinx_dsi *dsi = encoder_to_dsi(encoder);

	dev_dbg(dsi->dev, "config and enable the DSI: %s %d\n",
			 __func__, __LINE__);

	xilinx_dsi_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static const struct drm_encoder_helper_funcs xilinx_dsi_encoder_helper_funcs = {
	.dpms = xilinx_dsi_encoder_dpms,
	.mode_fixup = xilinx_dsi_mode_fixup,
	.mode_set = xilinx_dsi_mode_set,
	.prepare = xilinx_dsi_prepare,
	.commit = xilinx_dsi_commit,
};

static const struct drm_encoder_funcs xilinx_dsi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int xilinx_dsi_parse_dt(struct xilinx_dsi *dsi)
{
	struct device *dev = dsi->dev;
	struct device_node *node = dev->of_node;
	int ret;
	u32 pixels_per_beat, datatype;

	ret = of_property_read_u32(node, "xlnx,dsi-num-lanes",
				&dsi->lanes);
	if (ret < 0) {
		dev_err(dsi->dev, "missing xlnx,dsi-num-lanes property\n");
		return ret;
	}

	if ((dsi->lanes > 4) || (dsi->lanes < 1)) {
		dev_err(dsi->dev, "%d lanes : invalid xlnx,dsi-num-lanes\n",
			dsi->lanes);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,dsi-pixels-perbeat",
					&pixels_per_beat);
	if (ret < 0) {
		dev_err(dsi->dev, "missing xlnx,dsi-pixels-perbeat property\n");
		return ret;
	}

	if ((pixels_per_beat != 1) &&
		(pixels_per_beat != 2) &&
		(pixels_per_beat != 4)) {
		dev_err(dsi->dev, "Wrong dts val xlnx,dsi-pixels-perbeat\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,dsi-data-type", &datatype);

	if (ret < 0) {
		dev_err(dsi->dev, "missing xlnx,dsi-data-type property\n");
		return ret;
	}

	dsi->format = datatype;

	if ((datatype > MIPI_DSI_FMT_RGB565) ||
		(datatype < MIPI_DSI_FMT_RGB888)) {
		dev_err(dsi->dev, "Invalid xlnx,dsi-data-type string\n");
		return -EINVAL;
	}

	dsi->mul_factor = xdsi_mul_factor[datatype][pixels_per_beat >> 1];

	dev_dbg(dsi->dev, "DSI controller num lanes = %d,pixels per beat = %d",
				dsi->lanes, pixels_per_beat);

	dev_dbg(dsi->dev, "DSI controller datatype = %d\n", datatype);

	return 0;
}

static int xilinx_dsi_bind(struct device *dev, struct device *master,
				void *data)
{
	struct xilinx_dsi *dsi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	/*
	 * TODO: The possible CRTCs are 1 now as per current implementation of
	 * DSI tx drivers. DRM framework can support more than one CRTCs and
	 * DSI driver can be enhanced for that.
	 */
	encoder->possible_crtcs = 1;

	drm_encoder_init(drm_dev, encoder, &xilinx_dsi_encoder_funcs,
			 DRM_MODE_ENCODER_DSI, NULL);

	drm_encoder_helper_add(encoder, &xilinx_dsi_encoder_helper_funcs);

	ret = xilinx_dsi_create_connector(encoder);
	if (ret) {
		dev_err(dsi->dev, "fail creating connector, ret = %d\n", ret);
		drm_encoder_cleanup(encoder);
		return ret;
	}

	ret = mipi_dsi_host_register(&dsi->dsi_host);
	if (ret) {
		xilinx_dsi_connector_destroy(&dsi->connector);
		drm_encoder_cleanup(encoder);
		return ret;
	}

	return ret;
}

static void xilinx_dsi_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct xilinx_dsi *dsi = dev_get_drvdata(dev);

	xilinx_dsi_encoder_dpms(&dsi->encoder, DRM_MODE_DPMS_OFF);
	mipi_dsi_host_unregister(&dsi->dsi_host);
}

static const struct component_ops xilinx_dsi_component_ops = {
	.bind	= xilinx_dsi_bind,
	.unbind	= xilinx_dsi_unbind,
};

static int xilinx_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct xilinx_dsi *dsi;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dsi_host.ops = &xilinx_dsi_ops;
	dsi->dsi_host.dev = dev;
	dsi->dev = dev;

	ret = xilinx_dsi_parse_dt(dsi);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->iomem = devm_ioremap_resource(dev, res);
	dev_dbg(dsi->dev, "dsi virtual address = %p %s %d\n",
			dsi->iomem, __func__, __LINE__);

	if (IS_ERR(dsi->iomem)) {
		dev_err(dev, "failed to remap io region\n");
		return PTR_ERR(dsi->iomem);
	}

	platform_set_drvdata(pdev, dsi);

	return component_add(dev, &xilinx_dsi_component_ops);
}

static int xilinx_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &xilinx_dsi_component_ops);

	return 0;
}

static const struct of_device_id xilinx_dsi_of_match[] = {
	{ .compatible = "xlnx,mipi-dsi-tx-subsystem"},
	{ }
};
MODULE_DEVICE_TABLE(of, xilinx_dsi_of_match);

static struct platform_driver dsi_driver = {
	.probe = xilinx_dsi_probe,
	.remove = xilinx_dsi_remove,
	.driver = {
		.name = "xilinx-mipi-dsi",
		.owner = THIS_MODULE,
		.of_match_table = xilinx_dsi_of_match,
	},
};

module_platform_driver(dsi_driver);

MODULE_AUTHOR("Siva Rajesh <sivaraj@xilinx.com>");
MODULE_DESCRIPTION("Xilinx FPGA MIPI DSI Tx Driver");
MODULE_LICENSE("GPL v2");

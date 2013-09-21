/*
 * Xylon logiCVC frame buffer platform driver
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * This driver was primarily based on skeletonfb.c and other fb video drivers.
 * 2013 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/xylonfb_platform.h>
#include "../core/xylonfb.h"


static void xylonfb_get_platform_layer_params(
	struct xylonfb_platform_layer_params *lparams,
	struct xylonfb_layer_fix_data *lfdata, int id)
{
	driver_devel("%s\n", __func__);

	lfdata->offset = lparams->offset;
	lfdata->buffer_offset = lparams->buffer_offset;
	lfdata->layer_type = lparams->type;
	lfdata->bpp = lparams->bpp;
	lfdata->bpp_virt = lparams->bpp;
	lfdata->alpha_mode = lparams->alpha_mode;
	if (lfdata->layer_type == LOGICVC_ALPHA_LAYER)
		lfdata->alpha_mode = LOGICVC_LAYER_ALPHA;

	switch (lfdata->bpp) {
	case 8:
		if (lfdata->alpha_mode == LOGICVC_PIXEL_ALPHA)
			lfdata->bpp = 16;
		break;
	case 16:
		if (lfdata->alpha_mode == LOGICVC_PIXEL_ALPHA)
			lfdata->bpp = 32;
		break;
	}

	lfdata->layer_fix_info = id;
}

static int xylonfb_platform_probe(struct platform_device *pdev)
{
	struct xylonfb_init_data init_data;
	struct xylonfb_platform_data *pdata;
	int i;

	driver_devel("%s\n", __func__);

	memset(&init_data, 0, sizeof(struct xylonfb_init_data));

	init_data.pdev = pdev;

	pdata = (struct xylonfb_platform_data *)pdev->dev.platform_data;
	init_data.vmem_base_addr = pdata->vmem_base_addr;
	init_data.vmem_high_addr = pdata->vmem_high_addr;
	init_data.pixclk_src_id = pdata->pixclk_src_id;
	init_data.vmode_data.ctrl_reg = pdata->ctrl_reg;
	strcpy(init_data.vmode_data.fb_vmode_name, pdata->vmode);
	init_data.vmode_data.fb_vmode.refresh = 60;
	init_data.layers = pdata->num_layers;
	init_data.active_layer = pdata->active_layer;
	init_data.bg_layer_bpp = pdata->bg_layer_bpp;
	init_data.bg_layer_alpha_mode = pdata->bg_layer_alpha_mode;
	init_data.display_interface_type = pdata->display_interface_type;
	init_data.flags = pdata->flags;
	init_data.vmode_params_set = false;

	for (i = 0; i < init_data.layers; i++) {
		xylonfb_get_platform_layer_params(
			&pdata->layer_params[i], &init_data.lfdata[i], i);
		init_data.lfdata[i].width = pdata->row_stride;
		init_data.layer_ctrl_flags[i] = pdata->layer_params[i].ctrl_flags;
	}

	return xylonfb_init_driver(&init_data);
}

static int xylonfb_platform_remove(struct platform_device *pdev)
{
	driver_devel("%s\n", __func__);

	return xylonfb_deinit_driver(pdev);
}


void xylonfb_platform_release(struct device *dev)
{
	driver_devel("%s\n", __func__);

	return;
}


/* logiCVC parameters for Xylon Zynq-ZC702 2D3D referent design */
static struct xylonfb_platform_layer_params
	logicvc_0_layer_params[] = {
	{
		.offset = 7290,
		.buffer_offset = 1080,
		.type = LOGICVC_RGB_LAYER,
		.bpp = 32,
		.alpha_mode = LOGICVC_PIXEL_ALPHA,
		.ctrl_flags = 0,
	},
	{
		.offset = 4050,
		.buffer_offset = 1080,
		.type = LOGICVC_RGB_LAYER,
		.bpp = 32,
		.alpha_mode = LOGICVC_LAYER_ALPHA,
		.ctrl_flags = 0,
	},
	{
		.offset = 0,
		.buffer_offset = 1080,
		.type = LOGICVC_RGB_LAYER,
		.bpp = 32,
		.alpha_mode = LOGICVC_LAYER_ALPHA,
		.ctrl_flags = 0,
	},
	{
		.offset = 12960,
		.buffer_offset = 1080,
		.type = LOGICVC_RGB_LAYER,
		.bpp = 8,
		.alpha_mode = LOGICVC_CLUT_32BPP_ALPHA,
		.ctrl_flags = 0,
	},
};

static struct xylonfb_platform_data logicvc_0_platform_data = {
	.layer_params = logicvc_0_layer_params,
	.vmode = "1024x768",
	.ctrl_reg = (CTRL_REG_INIT | LOGICVC_PIX_ACT_HIGH),
	.vmem_base_addr = 0x30000000,
	.vmem_high_addr = 0x3FFFFFFF,
	.pixclk_src_id = 3,
	.row_stride = 2048,
	.num_layers = ARRAY_SIZE(logicvc_0_layer_params),
	.active_layer = 3,
	.bg_layer_bpp = 32,
	.bg_layer_alpha_mode = LOGICVC_LAYER_ALPHA,
	.display_interface_type =
		(LOGICVC_DI_PARALLEL << 4) | (LOGICVC_DCS_YUV422),
	/*
		Available flags:
		LOGICVC_READABLE_REGS
		XYLONFB_FLAG_EDID_VMODE
		XYLONFB_FLAG_EDID_PRINT
	*/
	.flags = 0,
};

static struct resource logicvc_0_resource[] = {
	{
		.start = 0x40030000,
		.end = (0x40030000 + LOGICVC_REGISTERS_RANGE),
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 90,
		.end = 90,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device logicvc_0_device = {
	.name = DEVICE_NAME,
	.id = 0,
	.dev = {
		.platform_data = &logicvc_0_platform_data,
		.release = xylonfb_platform_release,
	},
	.resource = logicvc_0_resource,
	.num_resources = ARRAY_SIZE(logicvc_0_resource),
};


static struct platform_driver xylonfb_driver = {
	.probe = xylonfb_platform_probe,
	.remove = xylonfb_platform_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DEVICE_NAME,
	},
};


static int xylonfb_platform_init(void)
{
#ifndef MODULE
	char *option = NULL;
#endif
	int err;

	driver_devel("%s\n", __func__);

#ifndef MODULE
	/*
	 *  For kernel boot options (in 'video=xxxfb:<options>' format)
	 */
	if (fb_get_options(DRIVER_NAME, &option))
		return -ENODEV;
	/* Set internal module parameters */
	xylonfb_get_params(option);
#endif
	err = platform_device_register(&logicvc_0_device);
	if (err) {
		pr_err("Error xylonfb device registration\n");
		return err;
	}
	err = platform_driver_register(&xylonfb_driver);
	if (err) {
		pr_err("Error xylonfb driver registration\n");
		platform_device_unregister(&logicvc_0_device);
		return err;
	}

	return 0;
}

static void __exit xylonfb_platform_exit(void)
{
	platform_driver_unregister(&xylonfb_driver);
	platform_device_unregister(&logicvc_0_device);
}


#ifndef MODULE
late_initcall(xylonfb_platform_init);
#else
module_init(xylonfb_platform_init);
module_exit(xylonfb_platform_exit);
#endif

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);

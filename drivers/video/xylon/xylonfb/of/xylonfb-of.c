/*
 * Xylon logiCVC frame buffer Open Firmware driver
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * 2012 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/of.h>
#include "../core/xylonfb.h"


static void set_ctrl_reg(struct xylonfb_init_data *init_data,
	unsigned long pix_data_invert, unsigned long pix_clk_act_high)
{
	u32 sync = init_data->vmode_data.fb_vmode.sync;
	u32 ctrl = CTRL_REG_INIT;

	if (sync & (1<<0)) {	//FB_SYNC_HOR_HIGH_ACT
		ctrl &= (~(1<<1));
	}
	if (sync & (1<<1)) {	// FB_SYNC_VERT_HIGH_ACT
		ctrl &= (~(1<<3));
	}
	if (pix_data_invert) {
		ctrl |= LOGICVC_PIX_DATA_INVERT;
	}
	if (pix_clk_act_high) {
		ctrl |= LOGICVC_PIX_ACT_HIGH;
	}

	init_data->vmode_data.ctrl_reg = ctrl;
}

static int xylonfb_parse_vram_info(struct device_node *np,
	unsigned long *vmem_base_addr, unsigned long *vmem_high_addr)
{
	u32 const *prop;
	int size;

	prop = of_get_property(np, "xlnx,vmem-baseaddr", &size);
	if (!prop) {
		pr_err("Error xylonfb getting VRAM address begin\n");
		return -EINVAL;
	}
	*vmem_base_addr = be32_to_cpup(prop);

	prop = of_get_property(np, "xlnx,vmem-highaddr", &size);
	if (!prop) {
		pr_err("Error xylonfb getting VRAM address end\n");
		return -EINVAL;
	}
	*vmem_high_addr = be32_to_cpup(prop);

	return 0;
}

static int xylonfb_parse_layer_info(struct device_node *np,
	struct xylonfb_init_data *init_data)
{
	u32 const *prop;
	unsigned int layers, bg_bpp, bg_alpha_mode;
	int size;
	char bg_layer_name[25];

	prop = of_get_property(np, "xlnx,num-of-layers", &size);
	if (!prop) {
		pr_err("Error getting number of layers\n");
		return -EINVAL;
	}
	layers = be32_to_cpup(prop);

	prop = of_get_property(np, "xlnx,use-background", &size);
	if (!prop) {
		pr_err("Error getting use background\n");
		return -EINVAL;
	}
	if (be32_to_cpup(prop) == 1) {
		layers--;

		sprintf(bg_layer_name, "xlnx,layer-%d-data-width", layers);
		prop = of_get_property(np, bg_layer_name, &size);
		if (!prop)
			bg_bpp = 16;
		else
			bg_bpp = be32_to_cpup(prop);
		if (bg_bpp == 24)
			bg_bpp = 32;

		sprintf(bg_layer_name, "xlnx,layer-%d-alpha-mode", layers);
		prop = of_get_property(np, bg_layer_name, &size);
		if (!prop) {
			bg_alpha_mode = LOGICVC_LAYER_ALPHA;
		} else {
			bg_alpha_mode = be32_to_cpup(prop);
		}
	} else {
		bg_bpp = 0;
		bg_alpha_mode = 0;
		pr_debug("xylonfb no BG layer\n");
	}

	init_data->layers = (unsigned char)layers;
	init_data->bg_layer_bpp = (unsigned char)bg_bpp;
	init_data->bg_layer_alpha_mode = (unsigned char)bg_alpha_mode;

	return 0;
}

static int xylonfb_parse_vmode_info(struct device_node *np,
	struct xylonfb_init_data *init_data)
{
	struct device_node *dn, *vmode_dn;
	u32 const *prop;
	char *c;
	unsigned long pix_data_invert, pix_clk_act_high;
	int size, tmp;

	init_data->active_layer = 0;
	init_data->vmode_params_set = false;

	dn = of_find_node_by_name(NULL, "xylon-video-params");
	if (dn == NULL) {
		pr_err("Error getting video mode parameters\n");
		return -ENOENT;
	}

	pix_data_invert = 0;
	prop = of_get_property(dn, "xlnx,pixel-data-invert", &size);
	if (!prop)
		pr_err("Error getting pixel data invert\n");
	else
		pix_data_invert = be32_to_cpup(prop);
	pix_clk_act_high = 0;
	prop = of_get_property(dn, "xlnx,pixel-clock-active-high", &size);
	if (!prop)
		pr_err("Error getting pixel active edge\n");
	else
		pix_clk_act_high = be32_to_cpup(prop);

	prop = of_get_property(dn, "xlnx,pixel-component-format", &size);
	if (prop) {
		if (!strcmp("ABGR", (char *)prop)) {
			prop = of_get_property(dn, "xlnx,pixel-component-layer", &size);
			if (prop) {
				while(size > 0) {
					tmp = be32_to_cpup(prop);
					init_data->layer_ctrl[tmp] = LOGICVC_SWAP_RB;
					prop++;
					size -= sizeof(prop);
				}
			}
		}
	}

	prop = of_get_property(dn, "active-layer", &size);
	if (prop) {
		tmp = be32_to_cpup(prop);
		init_data->active_layer = (unsigned char)tmp;
	} else {
		pr_info("xylonfb setting default layer to %d\n",
			init_data->active_layer);
	}

	prop = of_get_property(dn, "videomode", &size);
	if (prop) {
		if (strlen((char *)prop) <= VMODE_NAME_SZ) {
			strcpy(init_data->vmode_data.fb_vmode_name, (char *)prop);
			vmode_dn =
				of_find_node_by_name(dn, init_data->vmode_data.fb_vmode_name);
			c = strchr((char *)prop, '_');
			if (c)
				*c = 0;
			strcpy(init_data->vmode_data.fb_vmode_name, (char *)prop);
		} else {
			vmode_dn = NULL;
			pr_err("Error videomode name to long\n");
		}
		if (vmode_dn) {
			prop = of_get_property(vmode_dn, "refresh", &size);
			if (!prop)
				pr_err("Error getting refresh rate\n");
			else
				init_data->vmode_data.fb_vmode.refresh = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "xres", &size);
			if (!prop)
				pr_err("Error getting xres\n");
			else
				init_data->vmode_data.fb_vmode.xres = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "yres", &size);
			if (!prop)
				pr_err("Error getting yres\n");
			else
				init_data->vmode_data.fb_vmode.yres = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "pixclock-khz", &size);
			if (!prop)
				pr_err("Error getting pixclock-khz\n");
			else
				init_data->vmode_data.fb_vmode.pixclock =
					KHZ2PICOS(be32_to_cpup(prop));

			prop = of_get_property(vmode_dn, "left-margin", &size);
			if (!prop)
				pr_err("Error getting left-margin\n");
			else
				init_data->vmode_data.fb_vmode.left_margin = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "right-margin", &size);
			if (!prop)
				pr_err("Error getting right-margin\n");
			else
				init_data->vmode_data.fb_vmode.right_margin = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "upper-margin", &size);
			if (!prop)
				pr_err("Error getting upper-margin\n");
			else
				init_data->vmode_data.fb_vmode.upper_margin = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "lower-margin", &size);
			if (!prop)
				pr_err("Error getting lower-margin\n");
			else
				init_data->vmode_data.fb_vmode.lower_margin = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "hsync-len", &size);
			if (!prop)
				pr_err("Error getting hsync-len\n");
			else
				init_data->vmode_data.fb_vmode.hsync_len = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "vsync-len", &size);
			if (!prop)
				pr_err("Error getting vsync-len\n");
			else
				init_data->vmode_data.fb_vmode.vsync_len = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "sync", &size);
			if (!prop)
				pr_err("Error getting sync\n");
			else
				init_data->vmode_data.fb_vmode.sync = be32_to_cpup(prop);

			prop = of_get_property(vmode_dn, "vmode", &size);
			if (!prop)
				pr_err("Error getting vmode\n");
			else
				init_data->vmode_data.fb_vmode.vmode = be32_to_cpup(prop);

			init_data->vmode_params_set = true;
		} else {
			init_data->vmode_data.fb_vmode.refresh = 60;
		}
	} else {
		pr_info("xylonfb using default driver video mode\n");
	}

	set_ctrl_reg(init_data, pix_data_invert, pix_clk_act_high);

	return 0;
}

static int xylonfb_parse_layer_params(struct device_node *np,
	int id, struct layer_fix_data *lfdata)
{
	u32 const *prop;
	int size;
	char layer_property_name[25];

	sprintf(layer_property_name, "layer-%d-offset", id);
	prop = of_get_property(np, layer_property_name, &size);
	if (!prop) {
		pr_err("Error getting layer offset\n");
		return -EINVAL;
	} else {
		lfdata->offset = be32_to_cpup(prop);
	}

	sprintf(layer_property_name, "buffer-%d-offset", id);
	prop = of_get_property(np, layer_property_name, &size);
	if (!prop) {
		pr_err("Error getting buffer offset\n");
		return -EINVAL;
	} else {
		lfdata->buffer_offset = be32_to_cpup(prop);
	}

	prop = of_get_property(np, "row-stride", &size);
	if (!prop)
		lfdata->width = 1024;
	else
		lfdata->width = be32_to_cpup(prop);

	sprintf(layer_property_name, "layer-%d-alpha-mode", id);
	prop = of_get_property(np, layer_property_name, &size);
	if (!prop) {
		pr_err("Error getting layer alpha mode\n");
		return -EINVAL;
	} else {
		lfdata->alpha_mode = be32_to_cpup(prop);
	}

	sprintf(layer_property_name, "layer-%d-data-width", id);
	prop = of_get_property(np, layer_property_name, &size);
	if (!prop)
		lfdata->bpp = 16;
	else
		lfdata->bpp = be32_to_cpup(prop);
	if (lfdata->bpp == 24)
		lfdata->bpp = 32;

	lfdata->bpp_virt = lfdata->bpp;

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

	return 0;
}


static int xylonfb_of_probe(struct platform_device *pdev)
{
	struct xylonfb_init_data init_data;
	int i, rc;

	memset(&init_data, 0, sizeof(struct xylonfb_init_data));

	init_data.pdev = pdev;

	rc = xylonfb_parse_vram_info(pdev->dev.of_node,
		&init_data.vmem_base_addr, &init_data.vmem_high_addr);
	if (rc)
		return rc;
	rc = xylonfb_parse_layer_info(pdev->dev.of_node, &init_data);
	if (rc)
		return rc;
	/* if Device-Tree contains video mode options do not use
	   kernel command line video mode options */
	xylonfb_parse_vmode_info(pdev->dev.of_node, &init_data);

	for (i = 0; i < init_data.layers; i++) {
		rc = xylonfb_parse_layer_params(pdev->dev.of_node, i,
			&init_data.lfdata[i]);
		if (rc)
			return rc;
	}

	return xylonfb_init_driver(&init_data);
}

static int xylonfb_of_remove(struct platform_device *pdev)
{
	return xylonfb_deinit_driver(pdev);
}


static struct of_device_id xylonfb_of_match[] __devinitdata = {
	{ .compatible = "xylon,logicvc-2.05.c" },
	{ .compatible = "xlnx,logicvc-2.05.c" },
	{/* end of table */},
};
MODULE_DEVICE_TABLE(of, xylonfb_of_match);


static struct platform_driver xylonfb_of_driver = {
	.probe = xylonfb_of_probe,
	.remove = xylonfb_of_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DEVICE_NAME,
		.of_match_table = xylonfb_of_match,
	},
};


static int __init xylonfb_of_init(void)
{
#ifndef MODULE
	char *option = NULL;
	/*
	 *  For kernel boot options (in 'video=xxxfb:<options>' format)
	 */
	if (fb_get_options(DRIVER_NAME, &option))
		return -ENODEV;
	/* Set internal module parameters */
	xylonfb_get_params(option);
#endif
	if (platform_driver_register(&xylonfb_of_driver)) {
		pr_err("Error xylonfb driver registration\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit xylonfb_of_exit(void)
{
	platform_driver_unregister(&xylonfb_of_driver);
}


#ifndef MODULE
late_initcall(xylonfb_of_init);
#else
module_init(xylonfb_of_init);
module_exit(xylonfb_of_exit);
#endif

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Versal2 USB3.1 SSP+/DP Alt Combo PHY driver
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 */

#include <dt-bindings/phy/phy.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/usb/typec_mux.h>

/* USB3.1 SSP+/DP Alt PHY TCA Register Definitions */
#define USB3PHY_TCA_GCFG			0x10
#define USB3PHY_TCA_SYS_CFG			0x18

#define USB3PHY_TCA_SYS_CFG_FLIP_SHIFT		2

/* USB3.1 SSP+/DP Alt PHY CRPARA Register Definitions */
#define SUP_DIG_MPLLA_MPLL_PWR_CTL_STAT		0x188
#define MPLL_LOCK				BIT(12)

struct amd_versal2_udphy {
	struct device *dev;
	struct typec_switch_dev *sw;
	struct mutex mutex; /* mutex to protect access to individual PHYs */

	void __iomem *base_crpara;
	void __iomem *base_tca;

	struct clk *ref_clk;

	bool flip;

	struct phy *phy_usb;
};

static inline u32 amd_versal2_udphy_crpara_read(struct amd_versal2_udphy *udphy, u32 reg)
{
	return readl(udphy->base_crpara + reg);
}

static inline u32 amd_versal2_udphy_tca_read(struct amd_versal2_udphy *udphy, u32 reg)
{
	return readl(udphy->base_tca + reg);
}

static inline void amd_versal2_udphy_tca_write(struct amd_versal2_udphy *udphy, u32 reg, u32 val)
{
	writel(val, udphy->base_tca + reg);
}

/*
 * MPLLA is the primary PLL for USB operation (both Gen1 and Gen2).
 * Once MPLLA is locked, the PCS can derive pipe_laneX_pclk from MPLLA outputs
 * and the USB PHY supplies a stable PIPE clock to the MAC.
 */
static int amd_versal2_udphy_check_mplla_lock(struct amd_versal2_udphy *udphy)
{
	u32 val;

	val = amd_versal2_udphy_crpara_read(udphy, SUP_DIG_MPLLA_MPLL_PWR_CTL_STAT);
	if (!(val & MPLL_LOCK)) {
		dev_err(udphy->dev, "MPLLA PLL not locked (reg=0x%08x)\n", val);
		return -ETIMEDOUT;
	}

	return 0;
}

static void amd_versal2_udphy_set_typec_default_mapping(struct amd_versal2_udphy *udphy)
{
	u32 val;

	amd_versal2_udphy_tca_write(udphy, USB3PHY_TCA_GCFG, 0x0);
	val = amd_versal2_udphy_tca_read(udphy, USB3PHY_TCA_SYS_CFG);
	val &= ~BIT(USB3PHY_TCA_SYS_CFG_FLIP_SHIFT);
	val |= (udphy->flip << USB3PHY_TCA_SYS_CFG_FLIP_SHIFT);
	amd_versal2_udphy_tca_write(udphy, USB3PHY_TCA_SYS_CFG, val);
}

static int amd_versal2_udphy_orien_sw_set(struct typec_switch_dev *sw,
					  enum typec_orientation orien)
{
	struct amd_versal2_udphy *udphy = typec_switch_get_drvdata(sw);

	dev_dbg(udphy->dev, "set orientation %d\n", orien);

	if (orien == TYPEC_ORIENTATION_NONE)
		return 0;

	guard(mutex)(&udphy->mutex);

	udphy->flip = (orien == TYPEC_ORIENTATION_REVERSE);
	amd_versal2_udphy_set_typec_default_mapping(udphy);

	return 0;
}

static void amd_versal2_udphy_orien_switch_unregister(void *data)
{
	struct amd_versal2_udphy *udphy = data;

	typec_switch_unregister(udphy->sw);
}

static int amd_versal2_udphy_setup_orien_switch(struct amd_versal2_udphy *udphy)
{
	struct typec_switch_desc sw_desc = { };

	sw_desc.drvdata = udphy;
	sw_desc.fwnode = dev_fwnode(udphy->dev);
	sw_desc.name = fwnode_get_name(dev_fwnode(udphy->dev));
	sw_desc.set = amd_versal2_udphy_orien_sw_set;

	udphy->sw = typec_switch_register(udphy->dev, &sw_desc);
	if (IS_ERR(udphy->sw))
		return dev_err_probe(udphy->dev, PTR_ERR(udphy->sw),
				     "failed to register typec orientation switch\n");

	return devm_add_action_or_reset(udphy->dev,
					amd_versal2_udphy_orien_switch_unregister, udphy);
}

static int amd_versal2_udphy_usb3_phy_init(struct phy *phy)
{
	struct amd_versal2_udphy *udphy = phy_get_drvdata(phy);

	return amd_versal2_udphy_check_mplla_lock(udphy);
}

static const struct phy_ops amd_versal2_usb3_phy_ops = {
	.init		= amd_versal2_udphy_usb3_phy_init,
};

static struct phy *amd_versal2_udphy_phy_xlate(struct device *dev,
					       const struct of_phandle_args *args)
{
	struct amd_versal2_udphy *udphy = dev_get_drvdata(dev);

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	switch (args->args[0]) {
	case PHY_TYPE_USB3:
		return udphy->phy_usb;
	case PHY_TYPE_DP:
		return ERR_PTR(-EOPNOTSUPP);
	}

	return ERR_PTR(-EINVAL);
}

static int amd_versal2_udphy_probe(struct platform_device *pdev)
{
	struct amd_versal2_udphy *usbdp_phy;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct reset_control *rst;
	int ret;

	usbdp_phy = devm_kzalloc(dev, sizeof(*usbdp_phy), GFP_KERNEL);
	if (!usbdp_phy)
		return -ENOMEM;

	usbdp_phy->dev = dev;
	platform_set_drvdata(pdev, usbdp_phy);

	ret = devm_mutex_init(dev, &usbdp_phy->mutex);
	if (ret)
		return ret;

	usbdp_phy->base_crpara = devm_platform_ioremap_resource_byname(pdev, "crpara");
	if (IS_ERR(usbdp_phy->base_crpara))
		return dev_err_probe(dev, PTR_ERR(usbdp_phy->base_crpara),
				     "invalid crpara resource\n");

	usbdp_phy->base_tca = devm_platform_ioremap_resource_byname(pdev, "tca");
	if (IS_ERR(usbdp_phy->base_tca))
		return dev_err_probe(dev, PTR_ERR(usbdp_phy->base_tca),
				     "invalid tca resource\n");

	usbdp_phy->ref_clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(usbdp_phy->ref_clk))
		return dev_err_probe(dev, PTR_ERR(usbdp_phy->ref_clk),
				     "failed to get ref clock\n");

	rst = devm_reset_control_get_optional_exclusive_deasserted(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst),
				     "failed to acquire deasserted usb3 reset\n");

	if (device_property_present(dev, "orientation-switch")) {
		ret = amd_versal2_udphy_setup_orien_switch(usbdp_phy);
		if (ret)
			return ret;
	}

	usbdp_phy->phy_usb = devm_phy_create(dev, dev->of_node, &amd_versal2_usb3_phy_ops);
	if (IS_ERR(usbdp_phy->phy_usb))
		return dev_err_probe(dev, PTR_ERR(usbdp_phy->phy_usb),
				     "failed to create usb3 phy\n");
	phy_set_drvdata(usbdp_phy->phy_usb, usbdp_phy);

	phy_provider = devm_of_phy_provider_register(dev, amd_versal2_udphy_phy_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "failed to register phy provider\n");

	return 0;
}

static const struct of_device_id amd_versal2_udphy_dt_match[] = {
	{
		.compatible = "amd,versal2-usbc31-dptx-phy",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, amd_versal2_udphy_dt_match);

static struct platform_driver amd_versal2_udphy_driver = {
	.probe		= amd_versal2_udphy_probe,
	.driver		= {
		.name	= "amd-versal2-usbdp-phy",
		.of_match_table = amd_versal2_udphy_dt_match,
	},
};
module_platform_driver(amd_versal2_udphy_driver);

MODULE_AUTHOR("Radhey Shyam Pandey <radhey.shyam.pandey@amd.com>");
MODULE_DESCRIPTION("AMD Versal2 USB3.1 SSP+/DP Alt Combo PHY driver");
MODULE_LICENSE("GPL");

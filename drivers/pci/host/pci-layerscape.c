/*
 * PCIe host controller driver for Freescale Layerscape SoCs
 *
 * Copyright (C) 2014 Freescale Semiconductor.
 *
 * Author: Minghuan Lian <Minghuan.Lian@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pcie-designware.h"

/* PEX1/2 Misc Ports Status Register */
#define SCFG_PEXMSCPORTSR(pex_idx)	(0x94 + (pex_idx) * 4)
#define LTSSM_STATE_SHIFT	20
#define LTSSM_STATE_MASK	0x3f
#define LTSSM_PCIE_L0		0x11 /* L0 state */

/* PEX Internal Configuration Registers */
#define PCIE_STRFMR1		0x71c /* Symbol Timer & Filter Mask Register1 */
#define PCIE_DBI_RO_WR_EN	0x8bc /* DBI Read-Only Write Enable Register */

/* PEX LUT registers */
#define PCIE_LUT_DBG		0x7FC /* PEX LUT Debug Register */

struct ls_pcie_drvdata {
	u32 lut_offset;
	u32 ltssm_shift;
	struct pcie_host_ops *ops;
};

struct ls_pcie {
	struct pcie_port pp;		/* pp.dbi_base is DT regs */
	void __iomem *lut;
	struct regmap *scfg;
	const struct ls_pcie_drvdata *drvdata;
	int index;
};

#define to_ls_pcie(x)	container_of(x, struct ls_pcie, pp)

static bool ls_pcie_is_bridge(struct ls_pcie *pcie)
{
	u32 header_type;

	header_type = ioread8(pcie->pp.dbi_base + PCI_HEADER_TYPE);
	header_type &= 0x7f;

	return header_type == PCI_HEADER_TYPE_BRIDGE;
}

/* Clear multi-function bit */
static void ls_pcie_clear_multifunction(struct ls_pcie *pcie)
{
	iowrite8(PCI_HEADER_TYPE_BRIDGE, pcie->pp.dbi_base + PCI_HEADER_TYPE);
}

/* Fix class value */
static void ls_pcie_fix_class(struct ls_pcie *pcie)
{
	iowrite16(PCI_CLASS_BRIDGE_PCI, pcie->pp.dbi_base + PCI_CLASS_DEVICE);
}

/* Drop MSG TLP except for Vendor MSG */
static void ls_pcie_drop_msg_tlp(struct ls_pcie *pcie)
{
	u32 val;

	val = ioread32(pcie->pp.dbi_base + PCIE_STRFMR1);
	val &= 0xDFFFFFFF;
	iowrite32(val, pcie->pp.dbi_base + PCIE_STRFMR1);
}

static int ls1021_pcie_link_up(struct pcie_port *pp)
{
	u32 state;
	struct ls_pcie *pcie = to_ls_pcie(pp);

	if (!pcie->scfg)
		return 0;

	regmap_read(pcie->scfg, SCFG_PEXMSCPORTSR(pcie->index), &state);
	state = (state >> LTSSM_STATE_SHIFT) & LTSSM_STATE_MASK;

	if (state < LTSSM_PCIE_L0)
		return 0;

	return 1;
}

static void ls1021_pcie_host_init(struct pcie_port *pp)
{
	struct device *dev = pp->dev;
	struct ls_pcie *pcie = to_ls_pcie(pp);
	u32 index[2];

	pcie->scfg = syscon_regmap_lookup_by_phandle(dev->of_node,
						     "fsl,pcie-scfg");
	if (IS_ERR(pcie->scfg)) {
		dev_err(dev, "No syscfg phandle specified\n");
		pcie->scfg = NULL;
		return;
	}

	if (of_property_read_u32_array(dev->of_node,
				       "fsl,pcie-scfg", index, 2)) {
		pcie->scfg = NULL;
		return;
	}
	pcie->index = index[1];

	dw_pcie_setup_rc(pp);

	ls_pcie_drop_msg_tlp(pcie);
}

static int ls_pcie_link_up(struct pcie_port *pp)
{
	struct ls_pcie *pcie = to_ls_pcie(pp);
	u32 state;

	state = (ioread32(pcie->lut + PCIE_LUT_DBG) >>
		 pcie->drvdata->ltssm_shift) &
		 LTSSM_STATE_MASK;

	if (state < LTSSM_PCIE_L0)
		return 0;

	return 1;
}

static void ls_pcie_host_init(struct pcie_port *pp)
{
	struct ls_pcie *pcie = to_ls_pcie(pp);

	iowrite32(1, pcie->pp.dbi_base + PCIE_DBI_RO_WR_EN);
	ls_pcie_fix_class(pcie);
	ls_pcie_clear_multifunction(pcie);
	ls_pcie_drop_msg_tlp(pcie);
	iowrite32(0, pcie->pp.dbi_base + PCIE_DBI_RO_WR_EN);
}

static int ls_pcie_msi_host_init(struct pcie_port *pp,
				 struct msi_controller *chip)
{
	struct device *dev = pp->dev;
	struct device_node *np = dev->of_node;
	struct device_node *msi_node;

	/*
	 * The MSI domain is set by the generic of_msi_configure().  This
	 * .msi_host_init() function keeps us from doing the default MSI
	 * domain setup in dw_pcie_host_init() and also enforces the
	 * requirement that "msi-parent" exists.
	 */
	msi_node = of_parse_phandle(np, "msi-parent", 0);
	if (!msi_node) {
		dev_err(dev, "failed to find msi-parent\n");
		return -EINVAL;
	}

	return 0;
}

static struct pcie_host_ops ls1021_pcie_host_ops = {
	.link_up = ls1021_pcie_link_up,
	.host_init = ls1021_pcie_host_init,
	.msi_host_init = ls_pcie_msi_host_init,
};

static struct pcie_host_ops ls_pcie_host_ops = {
	.link_up = ls_pcie_link_up,
	.host_init = ls_pcie_host_init,
	.msi_host_init = ls_pcie_msi_host_init,
};

static struct ls_pcie_drvdata ls1021_drvdata = {
	.ops = &ls1021_pcie_host_ops,
};

static struct ls_pcie_drvdata ls1043_drvdata = {
	.lut_offset = 0x10000,
	.ltssm_shift = 24,
	.ops = &ls_pcie_host_ops,
};

static struct ls_pcie_drvdata ls2080_drvdata = {
	.lut_offset = 0x80000,
	.ltssm_shift = 0,
	.ops = &ls_pcie_host_ops,
};

static const struct of_device_id ls_pcie_of_match[] = {
	{ .compatible = "fsl,ls1021a-pcie", .data = &ls1021_drvdata },
	{ .compatible = "fsl,ls1043a-pcie", .data = &ls1043_drvdata },
	{ .compatible = "fsl,ls2080a-pcie", .data = &ls2080_drvdata },
	{ .compatible = "fsl,ls2085a-pcie", .data = &ls2080_drvdata },
	{ },
};

static int __init ls_add_pcie_port(struct ls_pcie *pcie)
{
	struct pcie_port *pp = &pcie->pp;
	struct device *dev = pp->dev;
	int ret;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int __init ls_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct ls_pcie *pcie;
	struct pcie_port *pp;
	struct resource *dbi_base;
	int ret;

	match = of_match_device(ls_pcie_of_match, dev);
	if (!match)
		return -ENODEV;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pp = &pcie->pp;
	pp->dev = dev;
	pcie->drvdata = match->data;
	pp->ops = pcie->drvdata->ops;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pcie->pp.dbi_base = devm_ioremap_resource(dev, dbi_base);
	if (IS_ERR(pcie->pp.dbi_base)) {
		dev_err(dev, "missing *regs* space\n");
		return PTR_ERR(pcie->pp.dbi_base);
	}

	pcie->lut = pcie->pp.dbi_base + pcie->drvdata->lut_offset;

	if (!ls_pcie_is_bridge(pcie))
		return -ENODEV;

	ret = ls_add_pcie_port(pcie);
	if (ret < 0)
		return ret;

	return 0;
}

static struct platform_driver ls_pcie_driver = {
	.driver = {
		.name = "layerscape-pcie",
		.of_match_table = ls_pcie_of_match,
	},
};
builtin_platform_driver_probe(ls_pcie_driver, ls_pcie_probe);

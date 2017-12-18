/*
 * Qualcomm PCIe root complex driver
 *
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 * Copyright 2015 Linaro Limited.
 *
 * Author: Stanimir Varbanov <svarbanov@mm-sol.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "pcie-designware.h"

#define PCIE20_PARF_PHY_CTRL			0x40
#define PCIE20_PARF_PHY_REFCLK			0x4C
#define PCIE20_PARF_DBI_BASE_ADDR		0x168
#define PCIE20_PARF_SLV_ADDR_SPACE_SIZE		0x16c
#define PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT	0x178

#define PCIE20_ELBI_SYS_CTRL			0x04
#define PCIE20_ELBI_SYS_CTRL_LT_ENABLE		BIT(0)

#define PCIE20_CAP				0x70

#define PERST_DELAY_US				1000

struct qcom_pcie_resources_v0 {
	struct clk *iface_clk;
	struct clk *core_clk;
	struct clk *phy_clk;
	struct reset_control *pci_reset;
	struct reset_control *axi_reset;
	struct reset_control *ahb_reset;
	struct reset_control *por_reset;
	struct reset_control *phy_reset;
	struct regulator *vdda;
	struct regulator *vdda_phy;
	struct regulator *vdda_refclk;
};

struct qcom_pcie_resources_v1 {
	struct clk *iface;
	struct clk *aux;
	struct clk *master_bus;
	struct clk *slave_bus;
	struct reset_control *core;
	struct regulator *vdda;
};

union qcom_pcie_resources {
	struct qcom_pcie_resources_v0 v0;
	struct qcom_pcie_resources_v1 v1;
};

struct qcom_pcie;

struct qcom_pcie_ops {
	int (*get_resources)(struct qcom_pcie *pcie);
	int (*init)(struct qcom_pcie *pcie);
	void (*deinit)(struct qcom_pcie *pcie);
};

struct qcom_pcie {
	struct pcie_port pp;			/* pp.dbi_base is DT dbi */
	void __iomem *parf;			/* DT parf */
	void __iomem *elbi;			/* DT elbi */
	union qcom_pcie_resources res;
	struct phy *phy;
	struct gpio_desc *reset;
	struct qcom_pcie_ops *ops;
};

#define to_qcom_pcie(x)		container_of(x, struct qcom_pcie, pp)

static void qcom_ep_reset_assert(struct qcom_pcie *pcie)
{
	gpiod_set_value(pcie->reset, 1);
	usleep_range(PERST_DELAY_US, PERST_DELAY_US + 500);
}

static void qcom_ep_reset_deassert(struct qcom_pcie *pcie)
{
	gpiod_set_value(pcie->reset, 0);
	usleep_range(PERST_DELAY_US, PERST_DELAY_US + 500);
}

static irqreturn_t qcom_pcie_msi_irq_handler(int irq, void *arg)
{
	struct pcie_port *pp = arg;

	return dw_handle_msi_irq(pp);
}

static int qcom_pcie_establish_link(struct qcom_pcie *pcie)
{
	u32 val;

	if (dw_pcie_link_up(&pcie->pp))
		return 0;

	/* enable link training */
	val = readl(pcie->elbi + PCIE20_ELBI_SYS_CTRL);
	val |= PCIE20_ELBI_SYS_CTRL_LT_ENABLE;
	writel(val, pcie->elbi + PCIE20_ELBI_SYS_CTRL);

	return dw_pcie_wait_for_link(&pcie->pp);
}

static int qcom_pcie_get_resources_v0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_v0 *res = &pcie->res.v0;
	struct device *dev = pcie->pp.dev;

	res->vdda = devm_regulator_get(dev, "vdda");
	if (IS_ERR(res->vdda))
		return PTR_ERR(res->vdda);

	res->vdda_phy = devm_regulator_get(dev, "vdda_phy");
	if (IS_ERR(res->vdda_phy))
		return PTR_ERR(res->vdda_phy);

	res->vdda_refclk = devm_regulator_get(dev, "vdda_refclk");
	if (IS_ERR(res->vdda_refclk))
		return PTR_ERR(res->vdda_refclk);

	res->iface_clk = devm_clk_get(dev, "iface");
	if (IS_ERR(res->iface_clk))
		return PTR_ERR(res->iface_clk);

	res->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(res->core_clk))
		return PTR_ERR(res->core_clk);

	res->phy_clk = devm_clk_get(dev, "phy");
	if (IS_ERR(res->phy_clk))
		return PTR_ERR(res->phy_clk);

	res->pci_reset = devm_reset_control_get(dev, "pci");
	if (IS_ERR(res->pci_reset))
		return PTR_ERR(res->pci_reset);

	res->axi_reset = devm_reset_control_get(dev, "axi");
	if (IS_ERR(res->axi_reset))
		return PTR_ERR(res->axi_reset);

	res->ahb_reset = devm_reset_control_get(dev, "ahb");
	if (IS_ERR(res->ahb_reset))
		return PTR_ERR(res->ahb_reset);

	res->por_reset = devm_reset_control_get(dev, "por");
	if (IS_ERR(res->por_reset))
		return PTR_ERR(res->por_reset);

	res->phy_reset = devm_reset_control_get(dev, "phy");
	if (IS_ERR(res->phy_reset))
		return PTR_ERR(res->phy_reset);

	return 0;
}

static int qcom_pcie_get_resources_v1(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_v1 *res = &pcie->res.v1;
	struct device *dev = pcie->pp.dev;

	res->vdda = devm_regulator_get(dev, "vdda");
	if (IS_ERR(res->vdda))
		return PTR_ERR(res->vdda);

	res->iface = devm_clk_get(dev, "iface");
	if (IS_ERR(res->iface))
		return PTR_ERR(res->iface);

	res->aux = devm_clk_get(dev, "aux");
	if (IS_ERR(res->aux))
		return PTR_ERR(res->aux);

	res->master_bus = devm_clk_get(dev, "master_bus");
	if (IS_ERR(res->master_bus))
		return PTR_ERR(res->master_bus);

	res->slave_bus = devm_clk_get(dev, "slave_bus");
	if (IS_ERR(res->slave_bus))
		return PTR_ERR(res->slave_bus);

	res->core = devm_reset_control_get(dev, "core");
	if (IS_ERR(res->core))
		return PTR_ERR(res->core);

	return 0;
}

static void qcom_pcie_deinit_v0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_v0 *res = &pcie->res.v0;

	reset_control_assert(res->pci_reset);
	reset_control_assert(res->axi_reset);
	reset_control_assert(res->ahb_reset);
	reset_control_assert(res->por_reset);
	reset_control_assert(res->pci_reset);
	clk_disable_unprepare(res->iface_clk);
	clk_disable_unprepare(res->core_clk);
	clk_disable_unprepare(res->phy_clk);
	regulator_disable(res->vdda);
	regulator_disable(res->vdda_phy);
	regulator_disable(res->vdda_refclk);
}

static int qcom_pcie_init_v0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_v0 *res = &pcie->res.v0;
	struct device *dev = pcie->pp.dev;
	u32 val;
	int ret;

	ret = regulator_enable(res->vdda);
	if (ret) {
		dev_err(dev, "cannot enable vdda regulator\n");
		return ret;
	}

	ret = regulator_enable(res->vdda_refclk);
	if (ret) {
		dev_err(dev, "cannot enable vdda_refclk regulator\n");
		goto err_refclk;
	}

	ret = regulator_enable(res->vdda_phy);
	if (ret) {
		dev_err(dev, "cannot enable vdda_phy regulator\n");
		goto err_vdda_phy;
	}

	ret = reset_control_assert(res->ahb_reset);
	if (ret) {
		dev_err(dev, "cannot assert ahb reset\n");
		goto err_assert_ahb;
	}

	ret = clk_prepare_enable(res->iface_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable iface clock\n");
		goto err_assert_ahb;
	}

	ret = clk_prepare_enable(res->phy_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable phy clock\n");
		goto err_clk_phy;
	}

	ret = clk_prepare_enable(res->core_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable core clock\n");
		goto err_clk_core;
	}

	ret = reset_control_deassert(res->ahb_reset);
	if (ret) {
		dev_err(dev, "cannot deassert ahb reset\n");
		goto err_deassert_ahb;
	}

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	/* enable external reference clock */
	val = readl(pcie->parf + PCIE20_PARF_PHY_REFCLK);
	val |= BIT(16);
	writel(val, pcie->parf + PCIE20_PARF_PHY_REFCLK);

	ret = reset_control_deassert(res->phy_reset);
	if (ret) {
		dev_err(dev, "cannot deassert phy reset\n");
		return ret;
	}

	ret = reset_control_deassert(res->pci_reset);
	if (ret) {
		dev_err(dev, "cannot deassert pci reset\n");
		return ret;
	}

	ret = reset_control_deassert(res->por_reset);
	if (ret) {
		dev_err(dev, "cannot deassert por reset\n");
		return ret;
	}

	ret = reset_control_deassert(res->axi_reset);
	if (ret) {
		dev_err(dev, "cannot deassert axi reset\n");
		return ret;
	}

	/* wait for clock acquisition */
	usleep_range(1000, 1500);

	return 0;

err_deassert_ahb:
	clk_disable_unprepare(res->core_clk);
err_clk_core:
	clk_disable_unprepare(res->phy_clk);
err_clk_phy:
	clk_disable_unprepare(res->iface_clk);
err_assert_ahb:
	regulator_disable(res->vdda_phy);
err_vdda_phy:
	regulator_disable(res->vdda_refclk);
err_refclk:
	regulator_disable(res->vdda);

	return ret;
}

static void qcom_pcie_deinit_v1(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_v1 *res = &pcie->res.v1;

	reset_control_assert(res->core);
	clk_disable_unprepare(res->slave_bus);
	clk_disable_unprepare(res->master_bus);
	clk_disable_unprepare(res->iface);
	clk_disable_unprepare(res->aux);
	regulator_disable(res->vdda);
}

static int qcom_pcie_init_v1(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_v1 *res = &pcie->res.v1;
	struct device *dev = pcie->pp.dev;
	int ret;

	ret = reset_control_deassert(res->core);
	if (ret) {
		dev_err(dev, "cannot deassert core reset\n");
		return ret;
	}

	ret = clk_prepare_enable(res->aux);
	if (ret) {
		dev_err(dev, "cannot prepare/enable aux clock\n");
		goto err_res;
	}

	ret = clk_prepare_enable(res->iface);
	if (ret) {
		dev_err(dev, "cannot prepare/enable iface clock\n");
		goto err_aux;
	}

	ret = clk_prepare_enable(res->master_bus);
	if (ret) {
		dev_err(dev, "cannot prepare/enable master_bus clock\n");
		goto err_iface;
	}

	ret = clk_prepare_enable(res->slave_bus);
	if (ret) {
		dev_err(dev, "cannot prepare/enable slave_bus clock\n");
		goto err_master;
	}

	ret = regulator_enable(res->vdda);
	if (ret) {
		dev_err(dev, "cannot enable vdda regulator\n");
		goto err_slave;
	}

	/* change DBI base address */
	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		u32 val = readl(pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT);

		val |= BIT(31);
		writel(val, pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT);
	}

	return 0;
err_slave:
	clk_disable_unprepare(res->slave_bus);
err_master:
	clk_disable_unprepare(res->master_bus);
err_iface:
	clk_disable_unprepare(res->iface);
err_aux:
	clk_disable_unprepare(res->aux);
err_res:
	reset_control_assert(res->core);

	return ret;
}

static int qcom_pcie_link_up(struct pcie_port *pp)
{
	struct qcom_pcie *pcie = to_qcom_pcie(pp);
	u16 val = readw(pcie->pp.dbi_base + PCIE20_CAP + PCI_EXP_LNKSTA);

	return !!(val & PCI_EXP_LNKSTA_DLLLA);
}

static void qcom_pcie_host_init(struct pcie_port *pp)
{
	struct qcom_pcie *pcie = to_qcom_pcie(pp);
	int ret;

	qcom_ep_reset_assert(pcie);

	ret = pcie->ops->init(pcie);
	if (ret)
		goto err_deinit;

	ret = phy_power_on(pcie->phy);
	if (ret)
		goto err_deinit;

	dw_pcie_setup_rc(pp);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(pp);

	qcom_ep_reset_deassert(pcie);

	ret = qcom_pcie_establish_link(pcie);
	if (ret)
		goto err;

	return;
err:
	qcom_ep_reset_assert(pcie);
	phy_power_off(pcie->phy);
err_deinit:
	pcie->ops->deinit(pcie);
}

static int qcom_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
				 u32 *val)
{
	/* the device class is not reported correctly from the register */
	if (where == PCI_CLASS_REVISION && size == 4) {
		*val = readl(pp->dbi_base + PCI_CLASS_REVISION);
		*val &= 0xff;	/* keep revision id */
		*val |= PCI_CLASS_BRIDGE_PCI << 16;
		return PCIBIOS_SUCCESSFUL;
	}

	return dw_pcie_cfg_read(pp->dbi_base + where, size, val);
}

static struct pcie_host_ops qcom_pcie_dw_ops = {
	.link_up = qcom_pcie_link_up,
	.host_init = qcom_pcie_host_init,
	.rd_own_conf = qcom_pcie_rd_own_conf,
};

static const struct qcom_pcie_ops ops_v0 = {
	.get_resources = qcom_pcie_get_resources_v0,
	.init = qcom_pcie_init_v0,
	.deinit = qcom_pcie_deinit_v0,
};

static const struct qcom_pcie_ops ops_v1 = {
	.get_resources = qcom_pcie_get_resources_v1,
	.init = qcom_pcie_init_v1,
	.deinit = qcom_pcie_deinit_v1,
};

static int qcom_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct qcom_pcie *pcie;
	struct pcie_port *pp;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pp = &pcie->pp;
	pcie->ops = (struct qcom_pcie_ops *)of_device_get_match_data(dev);

	pcie->reset = devm_gpiod_get_optional(dev, "perst", GPIOD_OUT_LOW);
	if (IS_ERR(pcie->reset))
		return PTR_ERR(pcie->reset);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "parf");
	pcie->parf = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->parf))
		return PTR_ERR(pcie->parf);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pp->dbi_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pp->dbi_base))
		return PTR_ERR(pp->dbi_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "elbi");
	pcie->elbi = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->elbi))
		return PTR_ERR(pcie->elbi);

	pcie->phy = devm_phy_optional_get(dev, "pciephy");
	if (IS_ERR(pcie->phy))
		return PTR_ERR(pcie->phy);

	pp->dev = dev;
	ret = pcie->ops->get_resources(pcie);
	if (ret)
		return ret;

	pp->root_bus_nr = -1;
	pp->ops = &qcom_pcie_dw_ops;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq_byname(pdev, "msi");
		if (pp->msi_irq < 0)
			return pp->msi_irq;

		ret = devm_request_irq(dev, pp->msi_irq,
				       qcom_pcie_msi_irq_handler,
				       IRQF_SHARED, "qcom-pcie-msi", pp);
		if (ret) {
			dev_err(dev, "cannot request msi irq\n");
			return ret;
		}
	}

	ret = phy_init(pcie->phy);
	if (ret)
		return ret;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "cannot initialize host\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id qcom_pcie_match[] = {
	{ .compatible = "qcom,pcie-ipq8064", .data = &ops_v0 },
	{ .compatible = "qcom,pcie-apq8064", .data = &ops_v0 },
	{ .compatible = "qcom,pcie-apq8084", .data = &ops_v1 },
	{ }
};

static struct platform_driver qcom_pcie_driver = {
	.probe = qcom_pcie_probe,
	.driver = {
		.name = "qcom-pcie",
		.suppress_bind_attrs = true,
		.of_match_table = qcom_pcie_match,
	},
};
builtin_platform_driver(qcom_pcie_driver);

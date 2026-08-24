// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for AMD MDB PCIe Bridge
 *
 * Copyright (C) 2024-2025, Advanced Micro Devices, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>

#include "../../pci.h"
#include "pcie-designware.h"

/*
 * On CPM6 the per-controller PCIe MISC_EVENT registers live in a dedicated
 * region ("intr"), separate from the CPM SLCR region ("slcr") that holds the
 * MERGED and PS severity registers they feed into. Each has a sticky W1C
 * STATUS, a read-only MASK, and write-1 ENABLE/DISABLE register.
 */
#define AMD_CPM6_MISC_EVENT_STATUS		0x514
#define AMD_CPM6_MISC_EVENT_MASK		0x518
#define AMD_CPM6_MISC_EVENT_ENABLE		0x51C
#define AMD_CPM6_MISC_EVENT_DISABLE		0x520

#define AMD_MDB_TLP_IR_STATUS_MISC		0x4C0
#define AMD_MDB_TLP_IR_MASK_MISC		0x4C4
#define AMD_MDB_TLP_IR_ENABLE_MISC		0x4C8
#define AMD_MDB_TLP_IR_DISABLE_MISC		0x4CC

#define AMD_MDB_TLP_PCIE_INTX_MASK	GENMASK(23, 16)

#define AMD_MDB_PCIE_INTR_INTX_ASSERT(x)	BIT((x) * 2)

#define AMD_CPM6_MERGED_STATUS			0x648
#define AMD_CPM6_MERGED_ENABLE			0x650
#define AMD_CPM6_MERGED_DISABLE			0x654

/* MERGED input bits for the MISC_EVENT sources this driver handles. */
#define AMD_CPM6_MERGED_MISC_EVENT_HOST0	14
#define AMD_CPM6_MERGED_MISC_EVENT_HOST1	17

/*
 * The PS_MISC severity register feeds the misc/OR GIC line. The MERGED
 * aggregator appears as bit 21 within it.
 */
#define AMD_CPM6_PS_MISC_IR_STATUS		0x340
#define AMD_CPM6_PS_IR_MERGED			BIT(21)

/* MDB5 interrupt register definitions. */
#define AMD_MDB_PCIE_INTR_CMPL_TIMEOUT		15
#define AMD_MDB_PCIE_INTR_INTX			16
#define AMD_MDB_PCIE_INTR_PM_PME_RCVD		24
#define AMD_MDB_PCIE_INTR_PME_TO_ACK_RCVD	25
#define AMD_MDB_PCIE_INTR_MISC_CORRECTABLE	26
#define AMD_MDB_PCIE_INTR_NONFATAL		27
#define AMD_MDB_PCIE_INTR_FATAL			28

#define IMR(x) BIT(AMD_MDB_PCIE_INTR_ ##x)
#define AMD_MDB_PCIE_IMR_ALL_MASK			\
	(						\
		IMR(CMPL_TIMEOUT)	|		\
		IMR(PM_PME_RCVD)	|		\
		IMR(PME_TO_ACK_RCVD)	|		\
		IMR(MISC_CORRECTABLE)	|		\
		IMR(NONFATAL)		|		\
		IMR(FATAL)		|		\
		AMD_MDB_TLP_PCIE_INTX_MASK		\
	)

/* CPM6 hwirq mapping (hwirq == MISC_EVENT status bit). */
#define AMD_CPM6_PCIE_INTR_INTX			22

enum amd_mdb_pcie_version {
	MDB5,
	CPM6,
	CPM6_HOST1,
};

struct amd_mdb_pcie_variant {
	enum	amd_mdb_pcie_version version;
	u32	misc_status_reg;
	u32	misc_mask_reg;
	u32	misc_enable_reg;
	u32	misc_disable_reg;
	u32	misc_mask_all;
	u32	intx_hwirq;
	u32	intx_mask;
};

/**
 * struct amd_mdb_pcie - PCIe port information
 * @pci: DesignWare PCIe controller structure
 * @slcr: MDB System Level Control and Status Register (SLCR) base
 * @intr_base: Per-controller interrupt register base. On CPM6 this maps the
 *             "intr" region holding the MISC_EVENT registers; on MDB5
 *             the interrupt registers live in the SLCR block, so it aliases
 *             @slcr.
 * @variant: Interrupt layout data for the matched platform compatible
 * @intx_domain: INTx IRQ domain pointer
 * @mdb_domain: MDB IRQ domain pointer
 * @perst_gpio: GPIO descriptor for PERST# signal handling
 * @intx_irq: INTx IRQ interrupt number
 * @intx_refmask: CPM6 mask of unmasked INTx lines; gates the shared aggregate
 */
struct amd_mdb_pcie {
	struct dw_pcie			pci;
	void __iomem			*slcr;
	void __iomem			*intr_base;
	const struct amd_mdb_pcie_variant	*variant;
	struct irq_domain		*intx_domain;
	struct irq_domain		*mdb_domain;
	struct gpio_desc		*perst_gpio;
	int				intx_irq;
	u32				intx_refmask;
};

static u32 amd_mdb_pcie_merged_host_mask(struct amd_mdb_pcie *pcie)
{
	return pcie->variant->version == CPM6 ?
	       BIT(AMD_CPM6_MERGED_MISC_EVENT_HOST0) :
	       BIT(AMD_CPM6_MERGED_MISC_EVENT_HOST1);
}

static void amd_mdb_pcie_clear_aggregators(struct amd_mdb_pcie *pcie)
{
	if (pcie->variant->version == MDB5)
		return;

	/* Clear this host's serviced MISC_EVENT contribution from MERGED. */
	writel_relaxed(amd_mdb_pcie_merged_host_mask(pcie),
		       pcie->slcr + AMD_CPM6_MERGED_STATUS);

	/*
	 * Clear MERGED in the PS_MISC severity register so the misc GIC line
	 * de-asserts.
	 */
	writel_relaxed(AMD_CPM6_PS_IR_MERGED,
		       pcie->slcr + AMD_CPM6_PS_MISC_IR_STATUS);
}

static const struct dw_pcie_host_ops amd_mdb_pcie_host_ops = {
};

static void amd_mdb_intx_irq_mask(struct irq_data *data)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(data);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	if (pcie->variant->version == MDB5) {
		val = FIELD_PREP(AMD_MDB_TLP_PCIE_INTX_MASK,
				 AMD_MDB_PCIE_INTR_INTX_ASSERT(data->hwirq));
	} else {
		/* CPM6 shares one INTx enable; drop it on the last mask. */
		pcie->intx_refmask &= ~BIT(data->hwirq);
		val = pcie->intx_refmask ? 0 : pcie->variant->intx_mask;
	}
	/* Writing '1' disables the interrupt; writing '0' has no effect. */
	if (val)
		writel_relaxed(val, pcie->intr_base + pcie->variant->misc_disable_reg);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void amd_mdb_intx_irq_unmask(struct irq_data *data)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(data);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	if (pcie->variant->version == MDB5) {
		val = FIELD_PREP(AMD_MDB_TLP_PCIE_INTX_MASK,
				 AMD_MDB_PCIE_INTR_INTX_ASSERT(data->hwirq));
	} else {
		/* CPM6 shares one INTx enable; raise it on the first unmask. */
		val = pcie->intx_refmask ? 0 : pcie->variant->intx_mask;
		pcie->intx_refmask |= BIT(data->hwirq);
	}
	/* Writing '1' enables the interrupt; writing '0' has no effect. */
	if (val)
		writel_relaxed(val, pcie->intr_base + pcie->variant->misc_enable_reg);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static struct irq_chip amd_mdb_intx_irq_chip = {
	.name		= "AMD MDB INTx",
	.irq_mask	= amd_mdb_intx_irq_mask,
	.irq_unmask	= amd_mdb_intx_irq_unmask,
};

/**
 * amd_mdb_pcie_intx_map - Set the handler for the INTx and mark IRQ as valid
 * @domain: IRQ domain
 * @irq: Virtual IRQ number
 * @hwirq: Hardware interrupt number
 *
 * Return: Always returns '0'.
 */
static int amd_mdb_pcie_intx_map(struct irq_domain *domain,
				 unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &amd_mdb_intx_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

/* INTx IRQ domain operations. */
static const struct irq_domain_ops amd_intx_domain_ops = {
	.map = amd_mdb_pcie_intx_map,
};

static irqreturn_t dw_pcie_rp_intx(int irq, void *args)
{
	struct amd_mdb_pcie *pcie = args;
	unsigned long val;
	int i, int_status;

	val = readl_relaxed(pcie->intr_base + pcie->variant->misc_status_reg);

	if (pcie->variant->version == MDB5) {
		int_status = FIELD_GET(AMD_MDB_TLP_PCIE_INTX_MASK, val);
		for (i = 0; i < PCI_NUM_INTX; i++) {
			if (int_status & AMD_MDB_PCIE_INTR_INTX_ASSERT(i))
				generic_handle_domain_irq(pcie->intx_domain, i);
		}
	} else {
		/* CPM6 exposes only an aggregate INTx indication */
		if (!(val & pcie->variant->intx_mask))
			return IRQ_NONE;
		for (i = 0; i < PCI_NUM_INTX; i++)
			generic_handle_domain_irq(pcie->intx_domain, i);
	}

	return IRQ_HANDLED;
}

#define _IC(x, s)[AMD_MDB_PCIE_INTR_ ## x] = { __stringify(x), s }

static const struct {
	const char	*sym;
	const char	*str;
} intr_cause[32] = {
	_IC(CMPL_TIMEOUT,	"Completion timeout"),
	_IC(PM_PME_RCVD,	"PM_PME message received"),
	_IC(PME_TO_ACK_RCVD,	"PME_TO_ACK message received"),
	_IC(MISC_CORRECTABLE,	"Correctable error message"),
	_IC(NONFATAL,		"Non fatal error message"),
	_IC(FATAL,		"Fatal error message"),
};

static void amd_mdb_event_irq_mask(struct irq_data *d)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;

	raw_spin_lock_irqsave(&port->lock, flags);
	writel_relaxed(BIT(d->hwirq),
		       pcie->intr_base + pcie->variant->misc_disable_reg);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void amd_mdb_event_irq_unmask(struct irq_data *d)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;

	raw_spin_lock_irqsave(&port->lock, flags);
	writel_relaxed(BIT(d->hwirq),
		       pcie->intr_base + pcie->variant->misc_enable_reg);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static struct irq_chip amd_mdb_event_irq_chip = {
	.name		= "AMD MDB RC-Event",
	.irq_mask	= amd_mdb_event_irq_mask,
	.irq_unmask	= amd_mdb_event_irq_unmask,
};

static int amd_mdb_pcie_event_map(struct irq_domain *domain,
				  unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &amd_mdb_event_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

static const struct irq_domain_ops event_domain_ops = {
	.map = amd_mdb_pcie_event_map,
};

static irqreturn_t amd_mdb_pcie_event(int irq, void *args)
{
	struct amd_mdb_pcie *pcie = args;
	unsigned long val;
	u32 ev_raw;
	int i;

	ev_raw = readl_relaxed(pcie->intr_base + pcie->variant->misc_status_reg);
	val = ev_raw;
	val &= ~readl_relaxed(pcie->intr_base + pcie->variant->misc_mask_reg);

	if (pcie->variant->version == MDB5) {
		for_each_set_bit(i, &val, 32)
			generic_handle_domain_irq(pcie->mdb_domain, i);
		writel_relaxed(val, pcie->intr_base + pcie->variant->misc_status_reg);
		return IRQ_HANDLED;
	}

	val &= pcie->variant->misc_mask_all;

	for_each_set_bit(i, &val, 32)
		generic_handle_domain_irq(pcie->mdb_domain, i);

	/* Clear handled + any unhandled sticky bits to avoid IRQ storms. */
	writel_relaxed(ev_raw, pcie->intr_base + pcie->variant->misc_status_reg);

	/* Sticky aggregation bits; clear each pass or the IRQ re-fires */
	amd_mdb_pcie_clear_aggregators(pcie);

	return IRQ_HANDLED;
}

static void amd_mdb_pcie_free_irq_domains(struct amd_mdb_pcie *pcie)
{
	if (pcie->intx_domain) {
		irq_domain_remove(pcie->intx_domain);
		pcie->intx_domain = NULL;
	}

	if (pcie->mdb_domain) {
		int i, irq;

		for (i = 0; i < ARRAY_SIZE(intr_cause); i++) {
			if (!intr_cause[i].str)
				continue;
			irq = irq_find_mapping(pcie->mdb_domain, i);
			if (irq) {
				disable_irq(irq);
				free_irq(irq, pcie);
				irq_dispose_mapping(irq);
			}
		}
		irq = irq_find_mapping(pcie->mdb_domain, AMD_MDB_PCIE_INTR_INTX);
		if (irq) {
			disable_irq(irq);
			free_irq(irq, pcie);
			irq_dispose_mapping(irq);
		}

		irq_domain_remove(pcie->mdb_domain);
		pcie->mdb_domain = NULL;
	}
}

static void amd_mdb_pcie_disable_interrupts(struct amd_mdb_pcie *pcie)
{
	u32 misc_mask_all = pcie->variant->misc_mask_all;
	u32 val;

	/* Mask all leaf TLP interrupts. */
	writel_relaxed(misc_mask_all,
		       pcie->intr_base + pcie->variant->misc_disable_reg);

	/* Clear any pending leaf TLP interrupts. */
	val = readl_relaxed(pcie->intr_base + pcie->variant->misc_status_reg) &
	      misc_mask_all;
	writel_relaxed(val, pcie->intr_base + pcie->variant->misc_status_reg);

	if (pcie->variant->version == MDB5)
		return;

	/*
	 * Mask this host's MISC_EVENT input in the shared MERGED aggregator so
	 * a stale source cannot drive the GIC line the peer host controller
	 * also shares.
	 */
	writel_relaxed(amd_mdb_pcie_merged_host_mask(pcie),
		       pcie->slcr + AMD_CPM6_MERGED_DISABLE);
}

static void amd_mdb_pcie_init_port(struct amd_mdb_pcie *pcie)
{
	u32 misc_mask_all;
	u32 val;

	misc_mask_all = pcie->variant->misc_mask_all;

	/* Disable all TLP interrupts. */
	writel_relaxed(misc_mask_all,
		       pcie->intr_base + pcie->variant->misc_disable_reg);

	/* Clear pending TLP interrupts. */
	val = readl_relaxed(pcie->intr_base + pcie->variant->misc_status_reg) &
	      misc_mask_all;
	writel_relaxed(val, pcie->intr_base + pcie->variant->misc_status_reg);

	/* Enable all TLP interrupts. */
	writel_relaxed(misc_mask_all,
		       pcie->intr_base + pcie->variant->misc_enable_reg);

	/*
	 * On CPM6 unmask this host's MISC_EVENT input in the shared MERGED
	 * aggregator so it reaches the GIC.
	 */
	if (pcie->variant->version != MDB5)
		writel_relaxed(amd_mdb_pcie_merged_host_mask(pcie),
			       pcie->slcr + AMD_CPM6_MERGED_ENABLE);
}

/**
 * amd_mdb_pcie_init_irq_domains - Initialize IRQ domain
 * @pcie: PCIe port information
 * @pdev: Platform device
 *
 * Return: Returns '0' on success and error value on failure.
 */
static int amd_mdb_pcie_init_irq_domains(struct amd_mdb_pcie *pcie,
					 struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;
	int err;

	pcie_intc_node = of_get_child_by_name(node, "interrupt-controller");
	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	pcie->mdb_domain = irq_domain_create_linear(of_fwnode_handle(pcie_intc_node), 32,
						    &event_domain_ops, pcie);
	if (!pcie->mdb_domain) {
		err = -ENOMEM;
		dev_err(dev, "Failed to add MDB domain\n");
		goto out;
	}

	irq_domain_update_bus_token(pcie->mdb_domain, DOMAIN_BUS_NEXUS);

	pcie->intx_domain = irq_domain_create_linear(of_fwnode_handle(pcie_intc_node),
						     PCI_NUM_INTX, &amd_intx_domain_ops, pcie);
	if (!pcie->intx_domain) {
		err = -ENOMEM;
		dev_err(dev, "Failed to add INTx domain\n");
		goto mdb_out;
	}

	of_node_put(pcie_intc_node);
	irq_domain_update_bus_token(pcie->intx_domain, DOMAIN_BUS_WIRED);

	raw_spin_lock_init(&pp->lock);

	return 0;
mdb_out:
	amd_mdb_pcie_free_irq_domains(pcie);
out:
	of_node_put(pcie_intc_node);
	return err;
}

static irqreturn_t amd_mdb_pcie_intr_handler(int irq, void *args)
{
	struct amd_mdb_pcie *pcie = args;
	struct device *dev;
	struct irq_data *d;

	dev = pcie->pci.dev;

	/*
	 * In the future, error reporting will be hooked to the AER subsystem.
	 * Currently, the driver prints a warning message to the user.
	 */
	d = irq_domain_get_irq_data(pcie->mdb_domain, irq);
	if (intr_cause[d->hwirq].str)
		dev_warn(dev, "%s\n", intr_cause[d->hwirq].str);
	else
		dev_warn_once(dev, "Unknown IRQ %ld\n", d->hwirq);

	return IRQ_HANDLED;
}

static int amd_mdb_setup_irq(struct amd_mdb_pcie *pcie,
			     struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	unsigned long event_flags = IRQF_NO_THREAD;
	int i, irq, err;

	/*
	 * Keep the hardware interrupts masked until every handler is
	 * registered below, so an early assertion cannot storm the shared
	 * CPM6 GIC line before there is anything to service it.
	 */
	amd_mdb_pcie_disable_interrupts(pcie);

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0)
		return pp->irq;

	/*
	 * MDB5 reports the error messages on the event domain. On CPM6 those
	 * are left to the native AER service, so only INTx is serviced on the
	 * event domain there.
	 */
	if (pcie->variant->version == MDB5) {
		for (i = 0; i < ARRAY_SIZE(intr_cause); i++) {
			if (!intr_cause[i].str)
				continue;

			irq = irq_create_mapping(pcie->mdb_domain, i);
			if (!irq) {
				dev_err(dev, "Failed to map MDB domain interrupt\n");
				return -ENOMEM;
			}

			err = devm_request_irq(dev, irq, amd_mdb_pcie_intr_handler,
					       IRQF_NO_THREAD, intr_cause[i].sym, pcie);
			if (err) {
				dev_err(dev, "Failed to request IRQ %d, err=%d\n",
					irq, err);
				return err;
			}
		}
	}

	pcie->intx_irq = irq_create_mapping(pcie->mdb_domain,
				    pcie->variant->intx_hwirq);
	if (!pcie->intx_irq) {
		dev_err(dev, "Failed to map INTx interrupt\n");
		return -ENXIO;
	}

	err = devm_request_irq(dev, pcie->intx_irq, dw_pcie_rp_intx,
			       IRQF_NO_THREAD, NULL, pcie);
	if (err) {
		dev_err(dev, "Failed to request INTx IRQ %d, err=%d\n",
			irq, err);
		return err;
	}

	/*
	 * On CPM6 the misc GIC line is shared between both host controllers,
	 * so the event IRQ must allow sharing.
	 */
	if (pcie->variant->version != MDB5)
		event_flags |= IRQF_SHARED;

	/* Plug the main event handler. */
	err = devm_request_irq(dev, pp->irq, amd_mdb_pcie_event, event_flags,
			       "amd_mdb pcie_irq", pcie);
	if (err) {
		dev_err(dev, "Failed to request event IRQ %d, err=%d\n",
			pp->irq, err);
		return err;
	}

	/* Arm the hardware only now that all handlers are in place. */
	amd_mdb_pcie_init_port(pcie);

	return 0;
}

static int amd_mdb_parse_pcie_port(struct amd_mdb_pcie *pcie)
{
	struct device *dev = pcie->pci.dev;
	struct device_node *pcie_port_node __maybe_unused;

	/*
	 * This platform currently supports only one Root Port, so the loop
	 * will execute only once.
	 * TODO: Enhance the driver to handle multiple Root Ports in the future.
	 */
	for_each_child_of_node_with_prefix(dev->of_node, pcie_port_node, "pcie") {
		pcie->perst_gpio = devm_fwnode_gpiod_get(dev, of_fwnode_handle(pcie_port_node),
							 "reset", GPIOD_OUT_HIGH, NULL);
		if (IS_ERR(pcie->perst_gpio))
			return dev_err_probe(dev, PTR_ERR(pcie->perst_gpio),
					     "Failed to request reset GPIO\n");
		return 0;
	}

	return -ENODEV;
}

static int amd_mdb_add_pcie_port(struct amd_mdb_pcie *pcie,
				 struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int err;

	if (pcie->variant->version == MDB5) {
		/*
		 * On MDB5 all interrupt registers live in the SLCR block, so
		 * the interrupt-register base simply aliases @slcr.
		 */
		pcie->slcr = devm_platform_ioremap_resource_byname(pdev, "slcr");
		if (IS_ERR(pcie->slcr))
			return PTR_ERR(pcie->slcr);
		pcie->intr_base = pcie->slcr;
	} else {
		struct resource *res;

		/*
		 * CPM6 moves the per-controller MISC_EVENT registers
		 * into a separate "intr" region. The SLCR block, which holds
		 * the shared MERGED/PS_MISC aggregators, is shared by both CPM6
		 * host controllers, so map it without requesting exclusive
		 * ownership; otherwise the second controller fails to probe.
		 */
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "slcr");
		if (!res)
			return -EINVAL;
		pcie->slcr = devm_ioremap(dev, res->start, resource_size(res));
		if (!pcie->slcr)
			return -ENOMEM;

		pcie->intr_base = devm_platform_ioremap_resource_byname(pdev, "intr");
		if (IS_ERR(pcie->intr_base))
			return PTR_ERR(pcie->intr_base);
	}

	err = amd_mdb_pcie_init_irq_domains(pcie, pdev);
	if (err)
		return err;

	err = amd_mdb_setup_irq(pcie, pdev);
	if (err) {
		dev_err(dev, "Failed to set up interrupts, err=%d\n", err);
		goto out;
	}

	pp->ops = &amd_mdb_pcie_host_ops;

	if (pcie->perst_gpio) {
		mdelay(PCIE_T_PVPERL_MS);
		gpiod_set_value_cansleep(pcie->perst_gpio, 0);
		mdelay(PCIE_RESET_CONFIG_WAIT_MS);
	}

	err = dw_pcie_host_init(pp);
	if (err) {
		dev_err(dev, "Failed to initialize host, err=%d\n", err);
		goto out;
	}

	return 0;

out:
	/*
	 * Mask the hardware interrupts before tearing down so a stale source
	 * cannot storm the shared CPM6 GIC line once this host is unwound.
	 */
	amd_mdb_pcie_disable_interrupts(pcie);
	amd_mdb_pcie_free_irq_domains(pcie);
	return err;
}

static int amd_mdb_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct amd_mdb_pcie *pcie;
	struct dw_pcie *pci;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = &pcie->pci;
	pci->dev = dev;
	pcie->variant = of_device_get_match_data(dev);
	if (!pcie->variant)
		return -EINVAL;

	platform_set_drvdata(pdev, pcie);

	ret = amd_mdb_parse_pcie_port(pcie);
	/*
	 * If amd_mdb_parse_pcie_port returns -ENODEV, it indicates that the
	 * PCIe Bridge node was not found in the device tree. This is not
	 * considered a fatal error and will trigger a fallback where the
	 * reset GPIO is acquired directly from the PCIe Host Bridge node.
	 */
	if (ret) {
		if (ret != -ENODEV)
			return ret;

		pcie->perst_gpio = devm_gpiod_get_optional(dev, "reset",
							   GPIOD_OUT_HIGH);
		if (IS_ERR(pcie->perst_gpio))
			return dev_err_probe(dev, PTR_ERR(pcie->perst_gpio),
					     "Failed to request reset GPIO\n");
	}

	return amd_mdb_add_pcie_port(pcie, pdev);
}

static void amd_mdb_pcie_shutdown(struct platform_device *pdev)
{
	struct amd_mdb_pcie *pcie = platform_get_drvdata(pdev);

	gpiod_set_value_cansleep(pcie->perst_gpio, 1);
}

static const struct amd_mdb_pcie_variant cpm6_host = {
	.version = CPM6,
	.misc_status_reg = AMD_CPM6_MISC_EVENT_STATUS,
	.misc_mask_reg = AMD_CPM6_MISC_EVENT_MASK,
	.misc_enable_reg = AMD_CPM6_MISC_EVENT_ENABLE,
	.misc_disable_reg = AMD_CPM6_MISC_EVENT_DISABLE,
	.misc_mask_all = BIT(AMD_CPM6_PCIE_INTR_INTX),
	.intx_hwirq = AMD_CPM6_PCIE_INTR_INTX,
	.intx_mask = BIT(AMD_CPM6_PCIE_INTR_INTX),
};

static const struct amd_mdb_pcie_variant cpm6_host1 = {
	.version = CPM6_HOST1,
	.misc_status_reg = AMD_CPM6_MISC_EVENT_STATUS,
	.misc_mask_reg = AMD_CPM6_MISC_EVENT_MASK,
	.misc_enable_reg = AMD_CPM6_MISC_EVENT_ENABLE,
	.misc_disable_reg = AMD_CPM6_MISC_EVENT_DISABLE,
	.misc_mask_all = BIT(AMD_CPM6_PCIE_INTR_INTX),
	.intx_hwirq = AMD_CPM6_PCIE_INTR_INTX,
	.intx_mask = BIT(AMD_CPM6_PCIE_INTR_INTX),
};

static const struct amd_mdb_pcie_variant mdb5_host = {
	.version = MDB5,
	.misc_status_reg = AMD_MDB_TLP_IR_STATUS_MISC,
	.misc_mask_reg = AMD_MDB_TLP_IR_MASK_MISC,
	.misc_enable_reg = AMD_MDB_TLP_IR_ENABLE_MISC,
	.misc_disable_reg = AMD_MDB_TLP_IR_DISABLE_MISC,
	.misc_mask_all = AMD_MDB_PCIE_IMR_ALL_MASK,
	.intx_hwirq = AMD_MDB_PCIE_INTR_INTX,
	.intx_mask = AMD_MDB_TLP_PCIE_INTX_MASK,
};

static const struct of_device_id amd_mdb_pcie_of_match[] = {
	{
		.compatible = "amd,versal2-mdb-host",
		.data = &mdb5_host,
	},
	{
		.compatible = "amd,versal2-cpm6-host",
		.data = &cpm6_host,
	},
	{
		.compatible = "amd,versal2-cpm6-host1",
		.data = &cpm6_host1,
	},
	{},
};

static struct platform_driver amd_mdb_pcie_driver = {
	.driver = {
		.name	= "amd-mdb-pcie",
		.of_match_table = amd_mdb_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = amd_mdb_pcie_probe,
	.shutdown = amd_mdb_pcie_shutdown,
};

builtin_platform_driver(amd_mdb_pcie_driver);

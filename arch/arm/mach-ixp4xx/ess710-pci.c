/*
 * arch/arm/mach-ixp4xx/ixdp425-pci.c 
 *
 * ESS710 board-level PCI initialization
 * Copyright (C) 2004 SnapGear - A CyberGuard Company
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <asm/mach/pci.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

void __init ess710_pci_preinit(void)
{
	printk("PCI: reset bus...\n");
	gpio_line_set(13, 0);
	gpio_line_config(13, IXP4XX_GPIO_OUT);
	gpio_line_set(13, 0);
	mdelay(50);
	gpio_line_set(13, 1);
	mdelay(50);

	gpio_line_config(6, IXP4XX_GPIO_IN);
	set_irq_type(IRQ_IXP4XX_GPIO6, IRQT_LOW); /* INTA */
	gpio_line_config(7, IXP4XX_GPIO_IN);
	set_irq_type(IRQ_IXP4XX_GPIO7, IRQT_LOW); /* INTB */
	gpio_line_config(8, IXP4XX_GPIO_IN);
	set_irq_type(IRQ_IXP4XX_GPIO8, IRQT_LOW); /* INTC */

	ixp4xx_pci_preinit();
}

static int __init ess710_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 16)
		return IRQ_ESS710_PCI_INTA;
	else if (slot == 15)
		return IRQ_ESS710_PCI_INTB;
	else if (slot == 14)
		return IRQ_ESS710_PCI_INTC;
	else if (slot == 13)
		return IRQ_ESS710_PCI_INTC;
	return -1;
}

struct hw_pci ess710_pci __initdata = {
	.nr_controllers = 1,
	.preinit	= ess710_pci_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= ess710_map_irq,
};

int __init ess710_pci_init(void)
{
	if (machine_is_ess710())
		pci_common_init(&ess710_pci);
	return 0;
}

subsys_initcall(ess710_pci_init);


/*
 * arch/arm/mach-ixp4xx/sg720-pci.c 
 *
 * SG590/SG720 board-level PCI initialization
 * Copyright (C) 2004-2006 SnapGear - A division of Secure Computing
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

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>

void __init sg720_pci_preinit(void)
{
	/*
	 * Check the stepping of the IXP465 CPU. If it is not A0 or A1 then
	 * enable the MPI port for direct memory bus access. Much faster.
	 * This is broken on older steppings, so don't enable.
	 */
	if (cpu_is_ixp46x()) {
		unsigned long processor_id;
		asm("mrc p15, 0, %0, cr0, cr0, 0;" : "=r"(processor_id) :);
		if ((processor_id & 0xf) >= 2) {
			printk("MPI: enabling fast memory bus...\n");
			*IXP4XX_EXP_CFG1 |= 0x80000000;
		} else {
			printk("MPI: disabling fast memory bus...\n");
			*IXP4XX_EXP_CFG1 &= ~0x80000000;
		}
	}

	printk("PCI: reset bus...\n");
	gpio_line_set(13, 0);
	gpio_line_config(13, IXP4XX_GPIO_OUT);
	gpio_line_set(13, 0);
	mdelay(50);
	gpio_line_set(13, 1);
	mdelay(50);

	gpio_line_config(8, IXP4XX_GPIO_IN);
	set_irq_type(IRQ_IXP4XX_GPIO8, IRQT_LOW); /* INTA */
	gpio_line_config(9, IXP4XX_GPIO_IN);
	set_irq_type(IRQ_IXP4XX_GPIO9, IRQT_LOW); /* INTB */

	ixp4xx_pci_preinit();
}

static int __init sg720_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
#if defined(CONFIG_MACH_SG590)
	if (slot == 12)
		return IRQ_SG590_PCI_INTA;
	else if (slot == 13)
		return IRQ_SG590_PCI_INTA;
#elif defined(CONFIG_MACH_SG720)
	if (slot == 12)
		return IRQ_SG720_PCI_INTB;
	else if (slot == 13)
		return IRQ_SG720_PCI_INTB;
	else if (slot == 14)
		return IRQ_SG720_PCI_INTA;
	else if (slot == 15)
		return IRQ_SG720_PCI_INTA;
#endif
	return -1;
}

struct hw_pci sg720_pci __initdata = {
	.nr_controllers = 1,
	.preinit	= sg720_pci_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= sg720_map_irq,
};

int __init sg720_pci_init(void)
{
	if (machine_is_sg720() || machine_is_sg590())
		pci_common_init(&sg720_pci);
	return 0;
}

subsys_initcall(sg720_pci_init);


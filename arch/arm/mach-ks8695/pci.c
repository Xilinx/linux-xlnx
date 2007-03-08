/*
 *  Copyright (c) 2003, Micrel Semiconductors
 *  Copyright (C) 2006, Greg Ungerer <gerg@snapgear.com>
 *
 *  Written 2003 by LIQUN RUAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/mach/pci.h>
#include <asm/hardware.h>
#include <asm/arch/ks8695-regs.h>


static u32 pcicmd(unsigned int bus, unsigned int devfn, int where)
{
	where &= 0xfffffffc;
	return (0x80000000 | (bus << 16) | (devfn << 8) | where);
}

static void local_write_config(unsigned int bus, unsigned int devfn, int where, u32 value)
{
	__raw_writel(pcicmd(bus, devfn, where), KS8695_REG(KS8695_PBCA));
	__raw_writel(value, KS8695_REG(KS8695_PBCD));
}


static int ks8695_pci_read_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	u32 v;


	__raw_writel(pcicmd(bus->number, devfn, where), KS8695_REG(KS8695_PBCA));
	v = __raw_readl(KS8695_REG(KS8695_PBCD));

	if (size == 1)
		*value = (u8) (v >> ((where & 0x3) * 8));
	else if (size == 2)
		*value = (u16) (v >> ((where & 0x2) * 8));
	else
		*value = v;

	return PCIBIOS_SUCCESSFUL;
}

static u32 bytemasks[] = {
	0xffffff00, 0xffff00ff, 0xff0ffff, 0x00ffffff,
};
static u32 wordmasks[] = {
	0xffff0000, 0x00000000, 0x0000ffff,
};

static int ks8695_pci_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	u32 cmd, v;
	int nr;

	v = value;
	cmd = pcicmd(bus->number, devfn, where);
	__raw_writel(cmd, KS8695_REG(KS8695_PBCA));

	if (size == 1) {
		nr = where & 0x3;
		v = __raw_readl(KS8695_REG(KS8695_PBCD));
		v = (v & bytemasks[nr]) | ((value & 0xff) << (nr * 8));
	} else if (size == 2) {
		nr = where & 0x2;
		v = __raw_readl(KS8695_REG(KS8695_PBCD));
		v = (v & wordmasks[nr]) | ((value & 0xffff) << (nr * 8));
	}

	__raw_writel(v, KS8695_REG(KS8695_PBCD));

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops ks8695_pci_ops = {
	.read	= ks8695_pci_read_config,
	.write	= ks8695_pci_write_config,
};

static struct pci_bus *ks8695_pci_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_bus(sys->busnr, &ks8695_pci_ops, sys);
}

static struct resource pci_mem = {
	.name	= "PCI memory space",
	.start	= KS8695P_PCI_MEM_BASE + 0x04000000,
	.end	= KS8695P_PCI_MEM_BASE + KS8695P_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM,
};

static struct resource pci_io = {
	.name	= "PCI IO space",
	.start	= KS8695P_PCI_IO_BASE,
	.end	= KS8695P_PCI_IO_BASE + KS8695P_PCI_IO_SIZE - 1,
	.flags	= IORESOURCE_IO,
};

static int __init ks8695_pci_setup(int nr, struct pci_sys_data *sys)
{
	if (nr > 0)
		return 0;

	/* Assign and enable processor bridge */
	local_write_config(0, 0, PCI_BASE_ADDRESS_0, KS8695P_PCI_MEM_BASE);
	local_write_config(0, 0, PCI_COMMAND,
		PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);

	request_resource(&iomem_resource, &pci_mem);
	request_resource(&ioport_resource, &pci_io);

	sys->resource[0] = &pci_io;
	sys->resource[1] = &pci_mem;
	sys->resource[2] = NULL;

	return 1;
}

/*
 * EXT0 is used as PCI bus interrupt source.
 * level detection (active low)
 */
static void __init ks8695_pci_configure_interrupt(void)
{
	u32 v;

	v = __raw_readl(KS8695_REG(KS8695_GPIO_MODE));
	v |= 0x00000001;
	__raw_writel(v, KS8695_REG(KS8695_GPIO_MODE));

	v = __raw_readl(KS8695_REG(KS8695_GPIO_CTRL));
	v &= 0xfffffff8;
	v |= 0x8;
	__raw_writel(v, KS8695_REG(KS8695_GPIO_CTRL));

	v = __raw_readl(KS8695_REG(KS8695_GPIO_MODE));
	v &= ~0x00000001;
	__raw_writel(v, KS8695_REG(KS8695_GPIO_MODE));
}

static void __init ks8695_pci_preinit(void)
{
#if defined(CONFIG_MACH_CM4008) || defined(CONFIG_MACH_CM41xx)
	/* Reset the PCI bus - (GPIO line is hooked up to bus reset) */
	u32 msk;
	msk = __raw_readl(KS8695_REG(KS8695_GPIO_MODE));
	__raw_writel(msk | 0x2, KS8695_REG(KS8695_GPIO_MODE));

	msk = __raw_readl(KS8695_REG(KS8695_GPIO_DATA));
	__raw_writel(msk & ~0x2, KS8695_REG(KS8695_GPIO_DATA));
	udelay(1000);
	__raw_writel(msk | 0x2, KS8695_REG(KS8695_GPIO_DATA));
	udelay(1000);
#endif

	/* stage 1 initialization, subid, subdevice = 0x0001 */
	__raw_writel(0x00010001, KS8695_REG(KS8695_CRCSID));

	/* stage 2 initialization */
	/* prefetch limits with 16 words, retru enable */
	__raw_writel(0x40000000, KS8695_REG(KS8695_PBCS));

	/* configure memory mapping */
	__raw_writel(KS8695P_PCIBG_MEM_BASE, KS8695_REG(KS8695_PMBA));
	__raw_writel(KS8695P_PCI_MEM_MASK, KS8695_REG(KS8695_PMBAM));
	__raw_writel(KS8695P_PCI_MEM_BASE, KS8695_REG(KS8695_PMBAT));

	/* configure IO mapping */
	__raw_writel(KS8695P_PCIBG_IO_BASE, KS8695_REG(KS8695_PIOBA));
	__raw_writel(KS8695P_PCI_IO_MASK, KS8695_REG(KS8695_PIOBAM));
	__raw_writel(KS8695P_PCI_IO_BASE, KS8695_REG(KS8695_PIOBAT));

	ks8695_pci_configure_interrupt();
}

static int __init ks8695_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return 2;
}

struct hw_pci ks8695_pci __initdata = {
	.nr_controllers	= 1,
	.preinit	= ks8695_pci_preinit,
	.swizzle	= pci_std_swizzle,
	.setup		= ks8695_pci_setup,
	.scan		= ks8695_pci_scan_bus,
	.map_irq	= ks8695_pci_map_irq,
};

static int __init ks8695_pci_init(void)
{
	pci_common_init(&ks8695_pci);
	return 0;
}

subsys_initcall(ks8695_pci_init);


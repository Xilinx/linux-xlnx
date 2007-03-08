/* arch/sh/kernel/pci.c
 * $Id: altpci.c,v 1.1 2006/07/05 06:23:17 gerg Exp $
 *
 * Copyright (c) 2002 M. R. Brown  <mrbrown@linux-sh.org>
 * 
 * 
 * These functions are collected here to reduce duplication of common
 * code amongst the many platform-specific PCI support code files.
 * 
 * These routines require the following board-specific routines:
 * void pcibios_fixup_irqs();
 *
 * See include/asm-sh/pci.h for more information.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

/*
 * Direct access to PCI hardware...
 */
#define pcicfg_space (na_pci_compiler_0_PCI_Bus_Access)  // avalon space
#define pciio  (pcicfg_space+0x100000)    // pci io device base in avalon space
#define pcimm  (pcicfg_space+0x200000)    // pci mem device base in avalon space
  // idsel of ad11=dev0,ad12=dev1  , using type 0 config request
#define pcicfg(dev,fun,reg) (pcicfg_space | ((dev)<<11) | ((fun)<<8) | (reg))    // cfg space

// FIX ME for your board, dram device for external pci masters access
static int __init alt_pci_init(void)
{
  unsigned dev,fun;
  // setup dram bar
  dev=0; fun=0;
  outl(nasys_program_mem,pcicfg(dev,fun,0x10));  // mem space
  outw(0x0006,pcicfg(dev,fun,0x04));   // enable master, mem space
  return 0;
}

subsys_initcall(alt_pci_init);

#define PCICFG(bus, devfn, where)  (pcicfg_space | (bus->number << 16) | (devfn << 8) | (where & ~3))
#define ALT_PCI_IO_BASE	        (pciio)
#define ALT_PCI_IO_SIZE	        0x100000
#define ALT_PCI_MEMORY_BASE	(pcimm)	
#define ALT_PCI_MEM_SIZE	0x100000

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */

// FIX ME for your board, number of pci bus, and number of devices
static inline int pci_range_ck(struct pci_bus *bus, unsigned int devfn)
{
  if (bus->number > 0 || PCI_SLOT(devfn) == 0 || PCI_SLOT(devfn) > 2)
		return -1;

	return 0;
}

static int alt_pci_read(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 *val)
{
	u32 data;

	if (pci_range_ck(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	// local_irq_save(flags);
	data = inl(PCICFG(bus, devfn, where));
	// local_irq_restore(flags);

	switch (size) {
	case 1:
		*val = (data >> ((where & 3) << 3)) & 0xff;
		break;
	case 2:
		*val = (data >> ((where & 2) << 3)) & 0xffff;
		break;
	case 4:
		*val = data;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

/* 
 * we'll do a read,
 * mask,write operation.
 * We'll allow an odd byte offset, though it should be illegal.
 */ 
static int alt_pci_write(struct pci_bus *bus, unsigned int devfn,
			    int where, int size, u32 val)
{
	int shift;
	u32 data;

	if (pci_range_ck(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	// local_irq_save(flags);
	data = inl(PCICFG(bus, devfn, where));
	// local_irq_restore(flags);

	switch (size) {
	case 1:
		shift = (where & 3) << 3;
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
		break;
	case 2:
		shift = (where & 2) << 3;
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
		break;
	case 4:
		data = val;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	outl(data, PCICFG(bus, devfn, where));

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops alt_pci_ops = {
	.read 		= alt_pci_read,
	.write		= alt_pci_write,
};

static struct resource alt_io_resource = {
	.name   = "ALTPCI IO",
	.start  = ALT_PCI_IO_BASE,
	.end    = ALT_PCI_IO_BASE + ALT_PCI_IO_SIZE - 1,
	.flags  = IORESOURCE_IO
};

static struct resource alt_mem_resource = {
	.name   = "ALTPCI mem",
	.start  = ALT_PCI_MEMORY_BASE,
	.end    = ALT_PCI_MEMORY_BASE + ALT_PCI_MEM_SIZE - 1,
	.flags  = IORESOURCE_MEM
};

extern struct pci_ops alt_pci_ops;

struct pci_channel board_pci_channels[] = {
	{ &alt_pci_ops, &alt_io_resource, &alt_mem_resource, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};

char *pcibios_setup(char *option)
{
	/* Nothing for us to handle. */
	return(option);
}

void pcibios_fixup_bus(struct pci_bus *b)
{
}

/* 
 * 	IRQ functions 
 */
static u8 __init altpci_no_swizzle(struct pci_dev *dev, u8 *pin)
{
	/* no swizzling */
	return PCI_SLOT(dev->devfn);
}

// FIX ME for your board, nios2 irqn mapping
int __init pcibios_map_platform_irq(u8 slot, u8 pin)
{
  int irq = na_irqn_0_irq + ((slot-1)*4) + (pin-1);
  // printk("map slot %d pin %d irq %d\n",slot,pin,irq);
  return irq;
}

static int altpci_pci_lookup_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = -1;

	/* now lookup the actual IRQ on a platform specific basis (pci-'platform'.c) */
	irq = pcibios_map_platform_irq(slot,pin);
	if( irq < 0 ) {
	  // printk("PCI: Error mapping IRQ on device %s\n", pci_name(dev));
	  return irq;
	}

	// printk("Setting IRQ for slot %s to %d\n", pci_name(dev), irq);

	return irq;
}

void __init pcibios_fixup_irqs(void)
{
	pci_fixup_irqs(altpci_no_swizzle, altpci_pci_lookup_irq);
}


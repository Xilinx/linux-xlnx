#ifndef _ASM_X86_PCI_H
#define _ASM_X86_PCI_H

#include <linux/mm.h> /* for struct page */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include <asm/io.h>
#include <asm/x86_init.h>

#ifdef __KERNEL__

struct pci_sysdata {
	int		domain;		/* PCI domain */
	int		node;		/* NUMA node */
#ifdef CONFIG_ACPI
	struct acpi_device *companion;	/* ACPI companion device */
#endif
#ifdef CONFIG_X86_64
	void		*iommu;		/* IOMMU private data */
#endif
#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
	void		*fwnode;	/* IRQ domain for MSI assignment */
#endif
#if IS_ENABLED(CONFIG_VMD)
	bool vmd_domain;		/* True if in Intel VMD domain */
#endif
};

extern int pci_routeirq;
extern int noioapicquirk;
extern int noioapicreroute;

#ifdef CONFIG_PCI

#ifdef CONFIG_PCI_DOMAINS
static inline int pci_domain_nr(struct pci_bus *bus)
{
	struct pci_sysdata *sd = bus->sysdata;

	return sd->domain;
}

static inline int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}
#endif

#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
static inline void *_pci_root_bus_fwnode(struct pci_bus *bus)
{
	struct pci_sysdata *sd = bus->sysdata;

	return sd->fwnode;
}

#define pci_root_bus_fwnode	_pci_root_bus_fwnode
#endif

static inline bool is_vmd(struct pci_bus *bus)
{
#if IS_ENABLED(CONFIG_VMD)
	struct pci_sysdata *sd = bus->sysdata;

	return sd->vmd_domain;
#else
	return false;
#endif
}

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

extern unsigned int pcibios_assign_all_busses(void);
extern int pci_legacy_init(void);
# ifdef CONFIG_ACPI
#  define x86_default_pci_init pci_acpi_init
# else
#  define x86_default_pci_init pci_legacy_init
# endif
#else
# define pcibios_assign_all_busses()	0
# define x86_default_pci_init		NULL
#endif

extern unsigned long pci_mem_start;
#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		(pci_mem_start)

#define PCIBIOS_MIN_CARDBUS_IO	0x4000

extern int pcibios_enabled;
void pcibios_config_init(void);
void pcibios_scan_root(int bus);

void pcibios_set_master(struct pci_dev *dev);
struct irq_routing_table *pcibios_get_irq_routing_table(void);
int pcibios_set_irq_routing(struct pci_dev *dev, int pin, int irq);


#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state,
			       int write_combine);


#ifdef CONFIG_PCI
extern void early_quirks(void);
#else
static inline void early_quirks(void) { }
#endif

extern void pci_iommu_alloc(void);

#ifdef CONFIG_PCI_MSI
/* implemented in arch/x86/kernel/apic/io_apic. */
struct msi_desc;
int native_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
void native_teardown_msi_irq(unsigned int irq);
void native_restore_msi_irqs(struct pci_dev *dev);
#else
#define native_setup_msi_irqs		NULL
#define native_teardown_msi_irq		NULL
#endif

#define PCI_DMA_BUS_IS_PHYS (dma_ops->is_phys)

#endif  /* __KERNEL__ */

#ifdef CONFIG_X86_64
#include <asm/pci_64.h>
#endif

/* generic pci stuff */
#include <asm-generic/pci.h>

#ifdef CONFIG_NUMA
/* Returns the node based on pci bus */
static inline int __pcibus_to_node(const struct pci_bus *bus)
{
	const struct pci_sysdata *sd = bus->sysdata;

	return sd->node;
}

static inline const struct cpumask *
cpumask_of_pcibus(const struct pci_bus *bus)
{
	int node;

	node = __pcibus_to_node(bus);
	return (node == -1) ? cpu_online_mask :
			      cpumask_of_node(node);
}
#endif

struct pci_setup_rom {
	struct setup_data data;
	uint16_t vendor;
	uint16_t devid;
	uint64_t pcilen;
	unsigned long segment;
	unsigned long bus;
	unsigned long device;
	unsigned long function;
	uint8_t romdata[0];
};

#endif /* _ASM_X86_PCI_H */

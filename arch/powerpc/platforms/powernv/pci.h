#ifndef __POWERNV_PCI_H
#define __POWERNV_PCI_H

struct pci_dn;

enum pnv_phb_type {
	PNV_PHB_P5IOC2	= 0,
	PNV_PHB_IODA1	= 1,
	PNV_PHB_IODA2	= 2,
};

/* Precise PHB model for error management */
enum pnv_phb_model {
	PNV_PHB_MODEL_UNKNOWN,
	PNV_PHB_MODEL_P5IOC2,
	PNV_PHB_MODEL_P7IOC,
	PNV_PHB_MODEL_PHB3,
};

#define PNV_PCI_DIAG_BUF_SIZE	8192
#define PNV_IODA_PE_DEV		(1 << 0)	/* PE has single PCI device	*/
#define PNV_IODA_PE_BUS		(1 << 1)	/* PE has primary PCI bus	*/
#define PNV_IODA_PE_BUS_ALL	(1 << 2)	/* PE has subordinate buses	*/

/* Data associated with a PE, including IOMMU tracking etc.. */
struct pnv_phb;
struct pnv_ioda_pe {
	unsigned long		flags;
	struct pnv_phb		*phb;

	/* A PE can be associated with a single device or an
	 * entire bus (& children). In the former case, pdev
	 * is populated, in the later case, pbus is.
	 */
	struct pci_dev		*pdev;
	struct pci_bus		*pbus;

	/* Effective RID (device RID for a device PE and base bus
	 * RID with devfn 0 for a bus PE)
	 */
	unsigned int		rid;

	/* PE number */
	unsigned int		pe_number;

	/* "Weight" assigned to the PE for the sake of DMA resource
	 * allocations
	 */
	unsigned int		dma_weight;

	/* "Base" iommu table, ie, 4K TCEs, 32-bit DMA */
	int			tce32_seg;
	int			tce32_segcount;
	struct iommu_table	tce32_table;
	phys_addr_t		tce_inval_reg_phys;

	/* XXX TODO: Add support for additional 64-bit iommus */

	/* MSIs. MVE index is identical for for 32 and 64 bit MSI
	 * and -1 if not supported. (It's actually identical to the
	 * PE number)
	 */
	int			mve_number;

	/* Link in list of PE#s */
	struct list_head	dma_link;
	struct list_head	list;
};

/* IOC dependent EEH operations */
#ifdef CONFIG_EEH
struct pnv_eeh_ops {
	int (*post_init)(struct pci_controller *hose);
	int (*set_option)(struct eeh_pe *pe, int option);
	int (*get_state)(struct eeh_pe *pe);
	int (*reset)(struct eeh_pe *pe, int option);
	int (*get_log)(struct eeh_pe *pe, int severity,
		       char *drv_log, unsigned long len);
	int (*configure_bridge)(struct eeh_pe *pe);
	int (*next_error)(struct eeh_pe **pe);
};

#define PNV_EEH_STATE_ENABLED	(1 << 0)	/* EEH enabled	*/
#define PNV_EEH_STATE_REMOVED	(1 << 1)	/* PHB removed	*/

#endif /* CONFIG_EEH */

struct pnv_phb {
	struct pci_controller	*hose;
	enum pnv_phb_type	type;
	enum pnv_phb_model	model;
	u64			hub_id;
	u64			opal_id;
	void __iomem		*regs;
	int			initialized;
	spinlock_t		lock;

#ifdef CONFIG_EEH
	struct pnv_eeh_ops	*eeh_ops;
	int			eeh_state;
#endif

#ifdef CONFIG_DEBUG_FS
	struct dentry		*dbgfs;
#endif

#ifdef CONFIG_PCI_MSI
	unsigned int		msi_base;
	unsigned int		msi32_support;
	struct msi_bitmap	msi_bmp;
#endif
	int (*msi_setup)(struct pnv_phb *phb, struct pci_dev *dev,
			 unsigned int hwirq, unsigned int virq,
			 unsigned int is_64, struct msi_msg *msg);
	void (*dma_dev_setup)(struct pnv_phb *phb, struct pci_dev *pdev);
	void (*fixup_phb)(struct pci_controller *hose);
	u32 (*bdfn_to_pe)(struct pnv_phb *phb, struct pci_bus *bus, u32 devfn);
	void (*shutdown)(struct pnv_phb *phb);

	union {
		struct {
			struct iommu_table iommu_table;
		} p5ioc2;

		struct {
			/* Global bridge info */
			unsigned int		total_pe;
			unsigned int		reserved_pe;
			unsigned int		m32_size;
			unsigned int		m32_segsize;
			unsigned int		m32_pci_base;
			unsigned int		io_size;
			unsigned int		io_segsize;
			unsigned int		io_pci_base;

			/* PE allocation bitmap */
			unsigned long		*pe_alloc;

			/* M32 & IO segment maps */
			unsigned int		*m32_segmap;
			unsigned int		*io_segmap;
			struct pnv_ioda_pe	*pe_array;

			/* IRQ chip */
			int			irq_chip_init;
			struct irq_chip		irq_chip;

			/* Sorted list of used PE's based
			 * on the sequence of creation
			 */
			struct list_head	pe_list;

			/* Reverse map of PEs, will have to extend if
			 * we are to support more than 256 PEs, indexed
			 * bus { bus, devfn }
			 */
			unsigned char		pe_rmap[0x10000];

			/* 32-bit TCE tables allocation */
			unsigned long		tce32_count;

			/* Total "weight" for the sake of DMA resources
			 * allocation
			 */
			unsigned int		dma_weight;
			unsigned int		dma_pe_count;

			/* Sorted list of used PE's, sorted at
			 * boot for resource allocation purposes
			 */
			struct list_head	pe_dma_list;
		} ioda;
	};

	/* PHB and hub status structure */
	union {
		unsigned char			blob[PNV_PCI_DIAG_BUF_SIZE];
		struct OpalIoP7IOCPhbErrorData	p7ioc;
		struct OpalIoP7IOCErrorData 	hub_diag;
	} diag;

};

extern struct pci_ops pnv_pci_ops;
#ifdef CONFIG_EEH
extern struct pnv_eeh_ops ioda_eeh_ops;
#endif

int pnv_pci_cfg_read(struct device_node *dn,
		     int where, int size, u32 *val);
int pnv_pci_cfg_write(struct device_node *dn,
		      int where, int size, u32 val);
extern void pnv_pci_setup_iommu_table(struct iommu_table *tbl,
				      void *tce_mem, u64 tce_size,
				      u64 dma_offset);
extern void pnv_pci_init_p5ioc2_hub(struct device_node *np);
extern void pnv_pci_init_ioda_hub(struct device_node *np);
extern void pnv_pci_init_ioda2_phb(struct device_node *np);
extern void pnv_pci_ioda_tce_invalidate(struct iommu_table *tbl,
					__be64 *startp, __be64 *endp, bool rm);

#endif /* __POWERNV_PCI_H */

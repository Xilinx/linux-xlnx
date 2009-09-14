#ifndef __ASM_MICROBLAZE_PCI_H
#define __ASM_MICROBLAZE_PCI_H
#ifdef __KERNEL__

/* PCI support is still under development. This file provides the definition
 * for PCI_DMA_BUS_IS_PHYS, which enables SCSI support. This is to support
 * USB Mass Storage devices, such as thumb drives.
 */

#include <linux/io.h>

/* The PCI address space does equal the physical memory
 * address space (no IOMMU).  The IDE and SCSI device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (1)

#endif	/* __KERNEL__ */
#endif	/* __ASM_MICROBLAZE_PCI_H */

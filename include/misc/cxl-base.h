/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _MISC_CXL_BASE_H
#define _MISC_CXL_BASE_H

#include <misc/cxl.h>

#ifdef CONFIG_CXL_BASE

#define CXL_IRQ_RANGES 4

struct cxl_irq_ranges {
	irq_hw_number_t offset[CXL_IRQ_RANGES];
	irq_hw_number_t range[CXL_IRQ_RANGES];
};

extern atomic_t cxl_use_count;

static inline bool cxl_ctx_in_use(void)
{
       return (atomic_read(&cxl_use_count) != 0);
}

static inline void cxl_ctx_get(void)
{
       atomic_inc(&cxl_use_count);
}

static inline void cxl_ctx_put(void)
{
       atomic_dec(&cxl_use_count);
}

struct cxl_afu *cxl_afu_get(struct cxl_afu *afu);
void cxl_afu_put(struct cxl_afu *afu);
void cxl_slbia(struct mm_struct *mm);
bool cxl_pci_associate_default_context(struct pci_dev *dev, struct cxl_afu *afu);
void cxl_pci_disable_device(struct pci_dev *dev);
int cxl_cx4_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type);
void cxl_cx4_teardown_msi_irqs(struct pci_dev *pdev);

#else /* CONFIG_CXL_BASE */

static inline bool cxl_ctx_in_use(void) { return false; }
static inline struct cxl_afu *cxl_afu_get(struct cxl_afu *afu) { return NULL; }
static inline void cxl_afu_put(struct cxl_afu *afu) {}
static inline void cxl_slbia(struct mm_struct *mm) {}
static inline bool cxl_pci_associate_default_context(struct pci_dev *dev, struct cxl_afu *afu) { return false; }
static inline void cxl_pci_disable_device(struct pci_dev *dev) {}
static inline int cxl_cx4_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type) { return -ENODEV; }
static inline void cxl_cx4_teardown_msi_irqs(struct pci_dev *pdev) {}

#endif /* CONFIG_CXL_BASE */

#endif

#ifndef _PARISC_DMA_MAPPING_H
#define _PARISC_DMA_MAPPING_H

#include <asm/cacheflush.h>

/*
** We need to support 4 different coherent dma models with one binary:
**
**     I/O MMU        consistent method           dma_sync behavior
**  =============   ======================       =======================
**  a) PA-7x00LC    uncachable host memory          flush/purge
**  b) U2/Uturn      cachable host memory              NOP
**  c) Ike/Astro     cachable host memory              NOP
**  d) EPIC/SAGA     memory on EPIC/SAGA         flush/reset DMA channel
**
** PA-7[13]00LC processors have a GSC bus interface and no I/O MMU.
**
** Systems (eg PCX-T workstations) that don't fall into the above
** categories will need to modify the needed drivers to perform
** flush/purge and allocate "regular" cacheable pages for everything.
*/

#ifdef CONFIG_PA11
extern struct dma_map_ops pcxl_dma_ops;
extern struct dma_map_ops pcx_dma_ops;
#endif

extern struct dma_map_ops *hppa_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return hppa_dma_ops;
}

static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
	if (hppa_dma_ops->sync_single_for_cpu)
		flush_kernel_dcache_range((unsigned long)vaddr, size);
}

static inline void *
parisc_walk_tree(struct device *dev)
{
	struct device *otherdev;
	if(likely(dev->platform_data != NULL))
		return dev->platform_data;
	/* OK, just traverse the bus to find it */
	for(otherdev = dev->parent; otherdev;
	    otherdev = otherdev->parent) {
		if(otherdev->platform_data) {
			dev->platform_data = otherdev->platform_data;
			break;
		}
	}
	BUG_ON(!dev->platform_data);
	return dev->platform_data;
}
		
#define GET_IOC(dev) (HBA_DATA(parisc_walk_tree(dev))->iommu)
	

#ifdef CONFIG_IOMMU_CCIO
struct parisc_device;
struct ioc;
void * ccio_get_iommu(const struct parisc_device *dev);
int ccio_request_resource(const struct parisc_device *dev,
		struct resource *res);
int ccio_allocate_resource(const struct parisc_device *dev,
		struct resource *res, unsigned long size,
		unsigned long min, unsigned long max, unsigned long align);
#else /* !CONFIG_IOMMU_CCIO */
#define ccio_get_iommu(dev) NULL
#define ccio_request_resource(dev, res) insert_resource(&iomem_resource, res)
#define ccio_allocate_resource(dev, res, size, min, max, align) \
		allocate_resource(&iomem_resource, res, size, min, max, \
				align, NULL, NULL)
#endif /* !CONFIG_IOMMU_CCIO */

#ifdef CONFIG_IOMMU_SBA
struct parisc_device;
void * sba_get_iommu(struct parisc_device *dev);
#endif

#endif

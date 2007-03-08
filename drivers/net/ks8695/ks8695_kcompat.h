/*****************************************************************************
 *****************************************************************************

 Copyright (c) 1999 - 2001, Intel Corporation 

 All rights reserved.

 Redistribution and use in source and binary forms, with or without 
 modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, 
     this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation 
     and/or other materials provided with the distribution.

  3. Neither the name of Intel Corporation nor the names of its contributors 
     may be used to endorse or promote products derived from this software 
     without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

 *****************************************************************************
 *****************************************************************************/

/* Macros to make drivers compatible with 2.2, 2.4 Linux kernels
 *
 * In order to make a single network driver work with all 2.2, 2.4 kernels
 * these compatibility macros can be used.
 * They are backwards compatible implementations of the latest APIs.
 * The idea is that these macros will let you use the newest driver with old
 * kernels, but can be removed when working with the latest and greatest.
 */

#ifndef __KS8695_LINUX_KERNEL_COMPAT_H
#define __KS8695_LINUX_KERNEL_COMPAT_H

#include <linux/version.h>

/* a good type to have */
/* in Linux a long is always the same length as a pointer */
typedef unsigned long int uintptr_t;

/*****************************************************************************
 **
 **  PCI Bus Changes
 **
 *****************************************************************************/

/* Accessing the BAR registers from the PCI device structure
 * Changed from base_address[bar] to resource[bar].start in 2.3.13
 * The pci_resource_start inline function was introduced in 2.3.43 
 */
#if   ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13) )
#define pci_resource_start(dev, bar) \
		(((dev)->base_address[(bar)] & PCI_BASE_ADDRESS_SPACE_IO) ? \
		 ((dev)->base_address[(bar)] & PCI_BASE_ADDRESS_IO_MASK) : \
		 ((dev)->base_address[(bar)] & PCI_BASE_ADDRESS_MEM_MASK))
#elif ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43) )
#define pci_resource_start(dev, bar) \
		(((dev)->resource[(bar)] & PCI_BASE_ADDRESS_SPACE_IO) ? \
		 ((dev)->resource[(bar)] & PCI_BASE_ADDRESS_IO_MASK) : \
		 ((dev)->resource[(bar)] & PCI_BASE_ADDRESS_MEM_MASK))
#endif

/* Starting with 2.3.23 drivers are supposed to call pci_enable_device
 * to make sure I/O and memory regions have been mapped and potentially 
 * bring the device out of a low power state
 */
#if   ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,23) )
#define pci_enable_device(dev) (0)
#endif

/* Dynamic DMA mapping
 * Instead of using virt_to_bus, bus mastering PCI drivers should use the DMA 
 * mapping API to get bus addresses.  This lets some platforms use dynamic 
 * mapping to use PCI devices that do not support DAC in a 64-bit address space
 */
#if   ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,41) )
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <asm/io.h>

#if (( LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18) ) || \
     ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) ) )
typedef unsigned long dma_addr_t;
#endif

#define PCI_DMA_TODEVICE   1
#define PCI_DMA_FROMDEVICE 2

extern inline void *pci_alloc_consistent (struct pci_dev *dev, 
                                          size_t size, 
                                          dma_addr_t *dma_handle) {
    void *vaddr = kmalloc(size, GFP_KERNEL);
    if(vaddr != NULL) {
        *dma_handle = virt_to_phys(vaddr);
    }
    return vaddr; 
}

extern inline int pci_dma_supported(struct pci_dev *hwdev, dma_addr_t mask)
{ return 1; }

extern inline void pci_free_consistent(struct pci_dev *hwdev, size_t size, 
                                       void *cpu_addr, dma_addr_t dma_handle)
{ kfree(cpu_addr); return; }

static inline dma_addr_t pci_map_single(struct pci_dev *hwdev, void *ptr,
                                        size_t size, int direction)
{ return virt_to_phys(ptr); }

static inline void pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
                                    size_t size, int direction)
{ return; }
/* Ug, this is ks8695 specific */
#define pci_resource_len(a,b)        (128 * 1024)

static inline int request_mem_region(uintptr_t addr, ...) { return 1; }
static inline int release_mem_region(uintptr_t addr, ...) { return 0; }
#endif


/*****************************************************************************
 **
 **  Network Device API Changes
 **
 *****************************************************************************/

/* In 2.3.14 the device structure was renamed to net_device 
 */
#if   ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,14) )
#define net_device device
#endif

/* 'Softnet' network stack changes merged in 2.3.43 
 * these are 2.2 compatible defines for the new network interface API
 * 2.3.47 added some more inline functions for softnet to remove explicit 
 * bit tests in drivers
 */
#if   ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43) )
#define netif_start_queue(dev)   clear_bit  (0, &(dev)->tbusy)
#define netif_stop_queue(dev)    set_bit    (0, &(dev)->tbusy)
#define netif_wake_queue(dev)    { clear_bit(0, &(dev)->tbusy); \
                                                mark_bh(NET_BH); }
#define netif_running(dev)       test_bit(0, &(dev)->start)
#define netif_queue_stopped(dev) test_bit(0, &(dev)->tbusy)
#elif ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,47) )
#define netif_running(dev)       test_bit(LINK_STATE_START, &(dev)->state)
#define netif_queue_stopped(dev) test_bit(LINK_STATE_XOFF,  &(dev)->state)
#endif

/* Softnet changes also affected how SKBs are handled
 * Special calls need to be made now while in an interrupt handler
 */
#if   ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43) )
#define dev_kfree_skb_irq(skb) dev_kfree_skb(skb)
#endif

/*****************************************************************************
 **
 **  General Module / Driver / Kernel API Changes
 **
 *****************************************************************************/

/* New module_init macro added in 2.3.13 - replaces init_module entry point
 * If MODULE is defined, it expands to an init_module definition
 * If the driver is staticly linked to the kernel, it creates the proper 
 * function pointer for the initialization routine to be called
 * (no more Space.c)
 * module_exit does the same thing for cleanup_module
 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13) )
#define module_init(fn) int  init_module   (void) { return fn(); }
#define module_exit(fn) void cleanup_module(void) { return fn(); }
#endif

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0) )
#ifndef __KS8695_MAIN__
#define __NO_VERSION__
#endif
#endif

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,47) )
#define PCI_ANY_ID (~0U)

struct pci_device_id {
    unsigned int vendor, device;
    unsigned int subvendor, subdevice;
    unsigned int class, classmask;
    unsigned long driver_data;
};

#define MODULE_DEVICE_TABLE(bus, dev_table)

struct pci_driver {
	char *name;
	struct pci_device_id *id_table;
	int (*probe)(struct pci_dev *dev, const struct pci_device_id *id);
	void (*remove)(struct pci_dev *dev);
	void (*suspend)(struct pci_dev *dev);
	void (*resume)(struct pci_dev *dev);
	/* track devices on Linux 2.2, used by module_init and unregister_driver */
	/* not to be used by the driver directly */
	/* assumes single function device with function #0 to simplify */
	uint32_t pcimap[256];
};

static inline int pci_module_init(struct pci_driver *drv)
{
	struct pci_dev *pdev;
	struct pci_device_id *pciid;
	uint16_t subvendor, subdevice;
	int board_count = 0;

	/* walk the global pci device list looking for matches */
	for (pdev = pci_devices; pdev != NULL; pdev = pdev->next) {
		pciid = &drv->id_table[0];
		pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &subvendor);
		pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &subdevice);
		
		while (pciid->vendor != 0) {
			if(((pciid->vendor == pdev->vendor) ||
				(pciid->vendor == PCI_ANY_ID)) &&

			   ((pciid->device == pdev->device) ||
				(pciid->device == PCI_ANY_ID)) &&

			   ((pciid->subvendor == subvendor) ||
				(pciid->subvendor == PCI_ANY_ID)) &&

			   ((pciid->subdevice == subdevice) ||
				(pciid->subdevice == PCI_ANY_ID))) {

				if(drv->probe(pdev, pciid) == 0) {
					board_count++;

					/* keep track of pci devices found */
					set_bit((pdev->devfn >> 3),
							&(drv->pcimap[pdev->bus->number]));
				}
				break;
			}
			pciid++;
		}
	}

	return (board_count > 0) ? 0 : -ENODEV;
}

static inline void pci_unregister_driver(struct pci_driver *drv)
{
	int i, bit;
	struct pci_dev *pdev;

	/* search the pci device bitmap and release them all */
	for(i = 0; i < 256; i++) {
		/* ffs = find first set bit */
		for(bit = ffs(drv->pcimap[i]); bit > 0; bit = ffs(drv->pcimap[i])) {
			bit--;
			pdev = pci_find_slot(i, (bit << 3));
			drv->remove(pdev);
			clear_bit(bit, &drv->pcimap[i]);
		}
	}
}
#endif

/* Taslets */

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43) )

#include <linux/interrupt.h>
#define tasklet_struct tq_struct

static inline void tasklet_init(struct tasklet_struct *t,
				void (*func)(unsigned long), unsigned long data)
{
	t->next = NULL;
	t->sync = 0;
	t->routine = (void *)(void *)func;
	t->data = (void *)data;
}

static inline void tasklet_schedule(struct tasklet_struct *t)
{
	queue_task(t, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	return;
}

static inline void tasklet_disable(struct tasklet_struct *t)
{
	return;
}

static inline void tasklet_enable(struct tasklet_struct *t)
{
	return;
}

#endif

#endif /* __KS8695_LINUX_KERNEL_COMPAT_H */


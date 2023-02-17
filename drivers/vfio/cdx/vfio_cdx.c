// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/cdx/cdx_bus.h>
#include <linux/delay.h>
#include <linux/io-64-nonatomic-hi-lo.h>

#include "vfio_cdx_private.h"

static struct cdx_driver vfio_cdx_driver;

enum {
	CDX_ID_F_VFIO_DRIVER_OVERRIDE = 1,
};

static int vfio_cdx_init_device(struct vfio_device *core_vdev)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	struct cdx_device *cdx_dev = to_cdx_device(core_vdev->dev);

	vdev->cdx_dev = cdx_dev;
	vdev->dev = &cdx_dev->dev;

	return 0;
}

static void vfio_cdx_release_device(struct vfio_device *core_vdev)
{
	vfio_free_device(core_vdev);
}

/**
 * CDX_DRIVER_OVERRIDE_DEVICE_VFIO - macro used to describe a VFIO
 *                                   "driver_override" CDX device.
 * @vend: the 16 bit CDX Vendor ID
 * @dev: the 16 bit CDX Device ID
 *
 * This macro is used to create a struct cdx_device_id that matches a
 * specific device. driver_override will be set to
 * CDX_ID_F_VFIO_DRIVER_OVERRIDE.
 */
#define CDX_DRIVER_OVERRIDE_DEVICE_VFIO(vend, dev) \
	CDX_DEVICE_DRIVER_OVERRIDE(vend, dev, CDX_ID_F_VFIO_DRIVER_OVERRIDE)

static int vfio_cdx_open_device(struct vfio_device *core_vdev)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	struct cdx_device *cdx_dev = vdev->cdx_dev;
	int count = cdx_dev->res_count;
	int i;

	vdev->regions = kcalloc(count, sizeof(struct vfio_cdx_region),
				GFP_KERNEL);
	if (!vdev->regions)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct resource *res = &cdx_dev->res[i];

		vdev->regions[i].addr = res->start;
		vdev->regions[i].size = resource_size(res);
		vdev->regions[i].type = res->flags;
		/*
		 * Only regions addressed with PAGE granularity may be
		 * MMAP'ed securely.
		 */
		if (!(vdev->regions[i].addr & ~PAGE_MASK) &&
		    !(vdev->regions[i].size & ~PAGE_MASK))
			vdev->regions[i].flags |=
					VFIO_REGION_INFO_FLAG_MMAP;
		vdev->regions[i].flags |= VFIO_REGION_INFO_FLAG_READ;
		if (!(cdx_dev->res[i].flags & IORESOURCE_READONLY))
			vdev->regions[i].flags |= VFIO_REGION_INFO_FLAG_WRITE;
	}

	return 0;
}

static void vfio_cdx_regions_cleanup(struct vfio_cdx_device *vdev)
{
	kfree(vdev->regions);
}

static int vfio_cdx_reset_device(struct vfio_cdx_device *vdev)
{
	return cdx_dev_reset(&vdev->cdx_dev->dev);
}

static void vfio_cdx_close_device(struct vfio_device *core_vdev)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	int ret;

	vfio_cdx_regions_cleanup(vdev);

	/* reset the device before cleaning up the interrupts */
	ret = vfio_cdx_reset_device(vdev);
	if (WARN_ON(ret))
		dev_warn(core_vdev->dev,
			 "VFIO_CDX: reset device has failed (%d)\n", ret);

	vfio_cdx_irqs_cleanup(vdev);
}

static long vfio_cdx_ioctl(struct vfio_device *core_vdev,
			   unsigned int cmd, unsigned long arg)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	struct cdx_device *cdx_dev = vdev->cdx_dev;
	unsigned long minsz;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.flags = VFIO_DEVICE_FLAGS_RESET;

		info.num_regions = cdx_dev->res_count;
		info.num_irqs = 1;

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info info;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index >= cdx_dev->res_count)
			return -EINVAL;

		/* map offset to the physical address  */
		info.offset = VFIO_CDX_INDEX_TO_OFFSET(info.index);
		info.size = vdev->regions[info.index].size;
		info.flags = vdev->regions[info.index].flags;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;
		return 0;
	}
	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);
		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index >= 1)
			return -EINVAL;

		info.flags = VFIO_IRQ_INFO_EVENTFD;
		info.count = cdx_dev->num_msi;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;
		return 0;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
		struct vfio_irq_set hdr;
		u8 *data = NULL;
		int ret = 0;
		size_t data_size = 0;

		minsz = offsetofend(struct vfio_irq_set, count);

		if (copy_from_user(&hdr, (void __user *)arg, minsz))
			return -EFAULT;

		ret = vfio_set_irqs_validate_and_prepare(&hdr, cdx_dev->num_msi,
							 1, &data_size);
		if (ret)
			return ret;

		if (data_size) {
			data = memdup_user((void __user *)(arg + minsz),
					   data_size);
			if (IS_ERR(data))
				return PTR_ERR(data);
		}

		ret = vfio_cdx_set_irqs_ioctl(vdev, hdr.flags, hdr.index,
					      hdr.start, hdr.count, data);
		kfree(data);

		return ret;
	}
	case VFIO_DEVICE_RESET:
	{
		return vfio_cdx_reset_device(vdev);
	}
	default:
		return -ENOTTY;
	}
}

static int vfio_cdx_mmap_mmio(struct vfio_cdx_region region,
			      struct vm_area_struct *vma)
{
	u64 size = vma->vm_end - vma->vm_start;
	u64 pgoff, base;

	pgoff = vma->vm_pgoff &
		((1U << (VFIO_CDX_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	base = pgoff << PAGE_SHIFT;

	if (region.size < PAGE_SIZE || base + size > region.size)
		return -EINVAL;

	vma->vm_pgoff = (region.addr >> PAGE_SHIFT) + pgoff;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       size, vma->vm_page_prot);
}

static int vfio_cdx_mmap(struct vfio_device *core_vdev,
			 struct vm_area_struct *vma)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	struct cdx_device *cdx_dev = vdev->cdx_dev;
	unsigned int index;

	index = vma->vm_pgoff >> (VFIO_CDX_OFFSET_SHIFT - PAGE_SHIFT);

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if (vma->vm_start & ~PAGE_MASK)
		return -EINVAL;
	if (vma->vm_end & ~PAGE_MASK)
		return -EINVAL;
	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;
	if (index >= cdx_dev->res_count)
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_MMAP))
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_READ) &&
	    (vma->vm_flags & VM_READ))
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_WRITE) &&
	    (vma->vm_flags & VM_WRITE))
		return -EINVAL;

	vma->vm_private_data = cdx_dev;

	return vfio_cdx_mmap_mmio(vdev->regions[index], vma);
}

static const struct vfio_device_ops vfio_cdx_ops = {
	.name		= "vfio-cdx",
	.init		= vfio_cdx_init_device,
	.release	= vfio_cdx_release_device,
	.open_device	= vfio_cdx_open_device,
	.close_device	= vfio_cdx_close_device,
	.ioctl		= vfio_cdx_ioctl,
	.mmap		= vfio_cdx_mmap,
};

static int vfio_cdx_probe(struct cdx_device *cdx_dev)
{
	struct vfio_cdx_device *vdev = NULL;
	struct device *dev = &cdx_dev->dev;
	int ret;

	vdev = vfio_alloc_device(vfio_cdx_device, vdev, dev,
				 &vfio_cdx_ops);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	ret = vfio_register_group_dev(&vdev->vdev);
	if (ret) {
		dev_err(dev, "VFIO_CDX: Failed to add to vfio group\n");
		goto out_uninit;
	}

	dev_set_drvdata(dev, vdev);
	return 0;

out_uninit:
	vfio_put_device(&vdev->vdev);
	return ret;
}

static int vfio_cdx_remove(struct cdx_device *cdx_dev)
{
	struct device *dev = &cdx_dev->dev;
	struct vfio_cdx_device *vdev;

	vdev = dev_get_drvdata(dev);
	vfio_unregister_group_dev(&vdev->vdev);
	vfio_put_device(&vdev->vdev);

	return 0;
}

static const struct cdx_device_id vfio_cdx_table[] = {
	{ CDX_DRIVER_OVERRIDE_DEVICE_VFIO(CDX_ANY_ID, CDX_ANY_ID) }, /* match all by default */
	{}
};

static struct cdx_driver vfio_cdx_driver = {
	.probe		= vfio_cdx_probe,
	.remove		= vfio_cdx_remove,
	.match_id_table	= vfio_cdx_table,
	.driver	= {
		.name	= "vfio-cdx",
		.owner	= THIS_MODULE,
	},
	.driver_managed_dma = true,
};

static int __init vfio_cdx_driver_init(void)
{
	return cdx_driver_register(&vfio_cdx_driver);
}

static void __exit vfio_cdx_driver_exit(void)
{
	cdx_driver_unregister(&vfio_cdx_driver);
}

module_init(vfio_cdx_driver_init);
module_exit(vfio_cdx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VFIO for CDX devices - User Level meta-driver");

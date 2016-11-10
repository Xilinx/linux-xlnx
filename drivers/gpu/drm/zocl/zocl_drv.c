/*
 * A GEM style CMA backed memory manager for ZynQ based OpenCL accelerators.
 *
 * Copyright (C) 2016 Xilinx, Inc. All rights reserved.
 *
 * Authors:
 *    Sonal Santan <sonal.santan@xilinx.com>
 *    Umang Parekh <umang.parekh@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/ramfs.h>
#include <linux/shmem_fs.h>
#include "zocl_drv.h"
#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/zocl_ioctl.h>

#define ZOCL_DRIVER_NAME        "zocl"
#define ZOCL_DRIVER_DESC        "Zynq BO manager"
#define ZOCL_DRIVER_DATE        "20161024"
#define ZOCL_DRIVER_MAJOR       2016
#define ZOCL_DRIVER_MINOR       3
#define ZOCL_DRIVER_PATCHLEVEL  1
#define ZOCL_FILE_PAGE_OFFSET   0x00100000

#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif

static const struct vm_operations_struct reg_physical_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

static int zocl_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct platform_device *pdev;
	struct resource *res;
	struct drm_zocl_dev *zdev;
	void __iomem *map;

	pdev = to_platform_device(drm->dev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	map = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(map)) {
		DRM_ERROR("Failed to map registers: %ld\n", PTR_ERR(map));
		return PTR_ERR(map);
	}

	zdev = devm_kzalloc(drm->dev, sizeof(*zdev), GFP_KERNEL);
	if (!zdev)
		return -ENOMEM;

	zdev->ddev = drm;
	drm->dev_private = zdev;
	zdev->regs = map;
	zdev->res_start = res->start;
	zdev->res_len = resource_size(res);
	platform_set_drvdata(pdev, zdev);

	return 0;
}

static int zocl_drm_unload(struct drm_device *drm)
{
	return 0;
}

static void zocl_free_object(struct drm_gem_object *obj)
{
	struct drm_zocl_bo *zocl_obj = to_zocl_bo(obj);

	DRM_INFO("Freeing BO\n");
	zocl_describe(zocl_obj);
	drm_gem_cma_free_object(obj);
}

static int zocl_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_zocl_dev *zdev = dev->dev_private;
	unsigned long vsize;
	int rc;

	/* If the page offset is > than 4G, then let GEM handle that and do what
	 * it thinks is best,we will only handle page offsets less than 4G.
	 */
	if (likely(vma->vm_pgoff >= ZOCL_FILE_PAGE_OFFSET))
		return drm_gem_cma_mmap(filp, vma);

	if (vma->vm_pgoff != 0)
		return -EINVAL;

	vsize = vma->vm_end - vma->vm_start;
	if (vsize > zdev->res_len)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;

	vma->vm_ops = &reg_physical_vm_ops;
	rc = io_remap_pfn_range(vma, vma->vm_start,
				zdev->res_start >> PAGE_SHIFT,
				vsize, vma->vm_page_prot);

	return rc;
}

static const struct drm_ioctl_desc zocl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ZOCL_CREATE_BO, zocl_create_bo_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ZOCL_MAP_BO, zocl_map_bo_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ZOCL_SYNC_BO, zocl_sync_bo_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ZOCL_INFO_BO, zocl_info_bo_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ZOCL_PWRITE_BO, zocl_pwrite_bo_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ZOCL_PREAD_BO, zocl_pread_bo_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
};

static const struct file_operations zocl_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= zocl_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.unlocked_ioctl = drm_ioctl,
	.release	= drm_release,
};

static struct drm_driver zocl_driver = {
	.driver_features		= DRIVER_GEM | DRIVER_PRIME |
					  DRIVER_RENDER,
	.load				= zocl_drm_load,
	.unload				= zocl_drm_unload,
	.gem_free_object		= zocl_free_object,
	.gem_vm_ops			= &drm_gem_cma_vm_ops,
	.prime_handle_to_fd		= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle		= drm_gem_prime_fd_to_handle,
	.gem_prime_import		= drm_gem_prime_import,
	.gem_prime_export		= drm_gem_prime_export,
	.gem_prime_get_sg_table		= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table	= drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap			= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap		= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap			= drm_gem_cma_prime_mmap,
	.ioctls				= zocl_ioctls,
	.num_ioctls			= ARRAY_SIZE(zocl_ioctls),
	.fops				= &zocl_driver_fops,
	.name				= ZOCL_DRIVER_NAME,
	.desc				= ZOCL_DRIVER_DESC,
	.date				= ZOCL_DRIVER_DATE,
	.major				= ZOCL_DRIVER_MAJOR,
	.minor				= ZOCL_DRIVER_MINOR,
	.patchlevel			= ZOCL_DRIVER_PATCHLEVEL,
};

/* init xilinx opencl drm platform */
static int zocl_drm_platform_probe(struct platform_device *pdev)
{
	return drm_platform_init(&zocl_driver, pdev);
}

/* exit xilinx opencl drm platform */
static int zocl_drm_platform_remove(struct platform_device *pdev)
{
	struct drm_zocl_dev *zdev = platform_get_drvdata(pdev);

	if (zdev->ddev) {
		drm_dev_unregister(zdev->ddev);
		drm_dev_unref(zdev->ddev);
	}

	return 0;
}

static const struct of_device_id zocl_drm_of_match[] = {
	{ .compatible = "xlnx,zocl", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, zocl_drm_of_match);

static struct platform_driver zocl_drm_private_driver = {
	.probe			= zocl_drm_platform_probe,
	.remove			= zocl_drm_platform_remove,
	.driver			= {
		.name		= "zocl-drm",
		.of_match_table	= zocl_drm_of_match,
	},
};

module_platform_driver(zocl_drm_private_driver);

MODULE_VERSION(__stringify(ZOCL_DRIVER_MAJOR) "."
		__stringify(ZOCL_DRIVER_MINOR) "."
		__stringify(ZOCL_DRIVER_PATCHLEVEL));

MODULE_DESCRIPTION(ZOCL_DRIVER_DESC);
MODULE_AUTHOR("Sonal Santan <sonal.santan@xilinx.com>");
MODULE_LICENSE("GPL");

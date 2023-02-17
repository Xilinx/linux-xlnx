/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef VFIO_CDX_PRIVATE_H
#define VFIO_CDX_PRIVATE_H

#define VFIO_CDX_OFFSET_SHIFT    40
#define VFIO_CDX_OFFSET_MASK (((u64)(1) << VFIO_CDX_OFFSET_SHIFT) - 1)

#define VFIO_CDX_OFFSET_TO_INDEX(off) ((off) >> VFIO_CDX_OFFSET_SHIFT)

#define VFIO_CDX_INDEX_TO_OFFSET(index)	\
	((u64)(index) << VFIO_CDX_OFFSET_SHIFT)

struct vfio_cdx_region {
	u32			flags;
	u32			type;
	u64			addr;
	resource_size_t		size;
	void __iomem		*ioaddr;
};

struct vfio_cdx_device {
	struct vfio_device	vdev;
	struct cdx_device	*cdx_dev;
	struct device		*dev;
	struct vfio_cdx_region	*regions;
};

#endif /* VFIO_CDX_PRIVATE_H */

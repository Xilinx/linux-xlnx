#ifndef __NVDIMM_PMEM_H__
#define __NVDIMM_PMEM_H__
#include <linux/badblocks.h>
#include <linux/types.h>
#include <linux/pfn_t.h>
#include <linux/fs.h>

long pmem_direct_access(struct block_device *bdev, sector_t sector,
		      void **kaddr, pfn_t *pfn, long size);
/* this definition is in it's own header for tools/testing/nvdimm to consume */
struct pmem_device {
	/* One contiguous memory region per device */
	phys_addr_t		phys_addr;
	/* when non-zero this device is hosting a 'pfn' instance */
	phys_addr_t		data_offset;
	u64			pfn_flags;
	void			*virt_addr;
	/* immutable base size of the namespace */
	size_t			size;
	/* trim size when namespace capacity has been section aligned */
	u32			pfn_pad;
	struct badblocks	bb;
};
#endif /* __NVDIMM_PMEM_H__ */

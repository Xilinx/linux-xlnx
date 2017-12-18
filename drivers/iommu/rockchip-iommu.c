/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-iommu.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/** MMU register offsets */
#define RK_MMU_DTE_ADDR		0x00	/* Directory table address */
#define RK_MMU_STATUS		0x04
#define RK_MMU_COMMAND		0x08
#define RK_MMU_PAGE_FAULT_ADDR	0x0C	/* IOVA of last page fault */
#define RK_MMU_ZAP_ONE_LINE	0x10	/* Shootdown one IOTLB entry */
#define RK_MMU_INT_RAWSTAT	0x14	/* IRQ status ignoring mask */
#define RK_MMU_INT_CLEAR	0x18	/* Acknowledge and re-arm irq */
#define RK_MMU_INT_MASK		0x1C	/* IRQ enable */
#define RK_MMU_INT_STATUS	0x20	/* IRQ status after masking */
#define RK_MMU_AUTO_GATING	0x24

#define DTE_ADDR_DUMMY		0xCAFEBABE
#define FORCE_RESET_TIMEOUT	100	/* ms */

/* RK_MMU_STATUS fields */
#define RK_MMU_STATUS_PAGING_ENABLED       BIT(0)
#define RK_MMU_STATUS_PAGE_FAULT_ACTIVE    BIT(1)
#define RK_MMU_STATUS_STALL_ACTIVE         BIT(2)
#define RK_MMU_STATUS_IDLE                 BIT(3)
#define RK_MMU_STATUS_REPLAY_BUFFER_EMPTY  BIT(4)
#define RK_MMU_STATUS_PAGE_FAULT_IS_WRITE  BIT(5)
#define RK_MMU_STATUS_STALL_NOT_ACTIVE     BIT(31)

/* RK_MMU_COMMAND command values */
#define RK_MMU_CMD_ENABLE_PAGING    0  /* Enable memory translation */
#define RK_MMU_CMD_DISABLE_PAGING   1  /* Disable memory translation */
#define RK_MMU_CMD_ENABLE_STALL     2  /* Stall paging to allow other cmds */
#define RK_MMU_CMD_DISABLE_STALL    3  /* Stop stall re-enables paging */
#define RK_MMU_CMD_ZAP_CACHE        4  /* Shoot down entire IOTLB */
#define RK_MMU_CMD_PAGE_FAULT_DONE  5  /* Clear page fault */
#define RK_MMU_CMD_FORCE_RESET      6  /* Reset all registers */

/* RK_MMU_INT_* register fields */
#define RK_MMU_IRQ_PAGE_FAULT    0x01  /* page fault */
#define RK_MMU_IRQ_BUS_ERROR     0x02  /* bus read error */
#define RK_MMU_IRQ_MASK          (RK_MMU_IRQ_PAGE_FAULT | RK_MMU_IRQ_BUS_ERROR)

#define NUM_DT_ENTRIES 1024
#define NUM_PT_ENTRIES 1024

#define SPAGE_ORDER 12
#define SPAGE_SIZE (1 << SPAGE_ORDER)

 /*
  * Support mapping any size that fits in one page table:
  *   4 KiB to 4 MiB
  */
#define RK_IOMMU_PGSIZE_BITMAP 0x007ff000

#define IOMMU_REG_POLL_COUNT_FAST 1000

struct rk_iommu_domain {
	struct list_head iommus;
	struct platform_device *pdev;
	u32 *dt; /* page directory table */
	dma_addr_t dt_dma;
	spinlock_t iommus_lock; /* lock for iommus list */
	spinlock_t dt_lock; /* lock for modifying page directory table */

	struct iommu_domain domain;
};

struct rk_iommu {
	struct device *dev;
	void __iomem **bases;
	int num_mmu;
	int irq;
	struct list_head node; /* entry in rk_iommu_domain.iommus */
	struct iommu_domain *domain; /* domain to which iommu is attached */
};

static inline void rk_table_flush(struct rk_iommu_domain *dom, dma_addr_t dma,
				  unsigned int count)
{
	size_t size = count * sizeof(u32); /* count of u32 entry */

	dma_sync_single_for_device(&dom->pdev->dev, dma, size, DMA_TO_DEVICE);
}

static struct rk_iommu_domain *to_rk_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct rk_iommu_domain, domain);
}

/**
 * Inspired by _wait_for in intel_drv.h
 * This is NOT safe for use in interrupt context.
 *
 * Note that it's important that we check the condition again after having
 * timed out, since the timeout could be due to preemption or similar and
 * we've never had a chance to check the condition before the timeout.
 */
#define rk_wait_for(COND, MS) ({ \
	unsigned long timeout__ = jiffies + msecs_to_jiffies(MS) + 1;	\
	int ret__ = 0;							\
	while (!(COND)) {						\
		if (time_after(jiffies, timeout__)) {			\
			ret__ = (COND) ? 0 : -ETIMEDOUT;		\
			break;						\
		}							\
		usleep_range(50, 100);					\
	}								\
	ret__;								\
})

/*
 * The Rockchip rk3288 iommu uses a 2-level page table.
 * The first level is the "Directory Table" (DT).
 * The DT consists of 1024 4-byte Directory Table Entries (DTEs), each pointing
 * to a "Page Table".
 * The second level is the 1024 Page Tables (PT).
 * Each PT consists of 1024 4-byte Page Table Entries (PTEs), each pointing to
 * a 4 KB page of physical memory.
 *
 * The DT and each PT fits in a single 4 KB page (4-bytes * 1024 entries).
 * Each iommu device has a MMU_DTE_ADDR register that contains the physical
 * address of the start of the DT page.
 *
 * The structure of the page table is as follows:
 *
 *                   DT
 * MMU_DTE_ADDR -> +-----+
 *                 |     |
 *                 +-----+     PT
 *                 | DTE | -> +-----+
 *                 +-----+    |     |     Memory
 *                 |     |    +-----+     Page
 *                 |     |    | PTE | -> +-----+
 *                 +-----+    +-----+    |     |
 *                            |     |    |     |
 *                            |     |    |     |
 *                            +-----+    |     |
 *                                       |     |
 *                                       |     |
 *                                       +-----+
 */

/*
 * Each DTE has a PT address and a valid bit:
 * +---------------------+-----------+-+
 * | PT address          | Reserved  |V|
 * +---------------------+-----------+-+
 *  31:12 - PT address (PTs always starts on a 4 KB boundary)
 *  11: 1 - Reserved
 *      0 - 1 if PT @ PT address is valid
 */
#define RK_DTE_PT_ADDRESS_MASK    0xfffff000
#define RK_DTE_PT_VALID           BIT(0)

static inline phys_addr_t rk_dte_pt_address(u32 dte)
{
	return (phys_addr_t)dte & RK_DTE_PT_ADDRESS_MASK;
}

static inline bool rk_dte_is_pt_valid(u32 dte)
{
	return dte & RK_DTE_PT_VALID;
}

static inline u32 rk_mk_dte(dma_addr_t pt_dma)
{
	return (pt_dma & RK_DTE_PT_ADDRESS_MASK) | RK_DTE_PT_VALID;
}

/*
 * Each PTE has a Page address, some flags and a valid bit:
 * +---------------------+---+-------+-+
 * | Page address        |Rsv| Flags |V|
 * +---------------------+---+-------+-+
 *  31:12 - Page address (Pages always start on a 4 KB boundary)
 *  11: 9 - Reserved
 *   8: 1 - Flags
 *      8 - Read allocate - allocate cache space on read misses
 *      7 - Read cache - enable cache & prefetch of data
 *      6 - Write buffer - enable delaying writes on their way to memory
 *      5 - Write allocate - allocate cache space on write misses
 *      4 - Write cache - different writes can be merged together
 *      3 - Override cache attributes
 *          if 1, bits 4-8 control cache attributes
 *          if 0, the system bus defaults are used
 *      2 - Writable
 *      1 - Readable
 *      0 - 1 if Page @ Page address is valid
 */
#define RK_PTE_PAGE_ADDRESS_MASK  0xfffff000
#define RK_PTE_PAGE_FLAGS_MASK    0x000001fe
#define RK_PTE_PAGE_WRITABLE      BIT(2)
#define RK_PTE_PAGE_READABLE      BIT(1)
#define RK_PTE_PAGE_VALID         BIT(0)

static inline phys_addr_t rk_pte_page_address(u32 pte)
{
	return (phys_addr_t)pte & RK_PTE_PAGE_ADDRESS_MASK;
}

static inline bool rk_pte_is_page_valid(u32 pte)
{
	return pte & RK_PTE_PAGE_VALID;
}

/* TODO: set cache flags per prot IOMMU_CACHE */
static u32 rk_mk_pte(phys_addr_t page, int prot)
{
	u32 flags = 0;
	flags |= (prot & IOMMU_READ) ? RK_PTE_PAGE_READABLE : 0;
	flags |= (prot & IOMMU_WRITE) ? RK_PTE_PAGE_WRITABLE : 0;
	page &= RK_PTE_PAGE_ADDRESS_MASK;
	return page | flags | RK_PTE_PAGE_VALID;
}

static u32 rk_mk_pte_invalid(u32 pte)
{
	return pte & ~RK_PTE_PAGE_VALID;
}

/*
 * rk3288 iova (IOMMU Virtual Address) format
 *  31       22.21       12.11          0
 * +-----------+-----------+-------------+
 * | DTE index | PTE index | Page offset |
 * +-----------+-----------+-------------+
 *  31:22 - DTE index   - index of DTE in DT
 *  21:12 - PTE index   - index of PTE in PT @ DTE.pt_address
 *  11: 0 - Page offset - offset into page @ PTE.page_address
 */
#define RK_IOVA_DTE_MASK    0xffc00000
#define RK_IOVA_DTE_SHIFT   22
#define RK_IOVA_PTE_MASK    0x003ff000
#define RK_IOVA_PTE_SHIFT   12
#define RK_IOVA_PAGE_MASK   0x00000fff
#define RK_IOVA_PAGE_SHIFT  0

static u32 rk_iova_dte_index(dma_addr_t iova)
{
	return (u32)(iova & RK_IOVA_DTE_MASK) >> RK_IOVA_DTE_SHIFT;
}

static u32 rk_iova_pte_index(dma_addr_t iova)
{
	return (u32)(iova & RK_IOVA_PTE_MASK) >> RK_IOVA_PTE_SHIFT;
}

static u32 rk_iova_page_offset(dma_addr_t iova)
{
	return (u32)(iova & RK_IOVA_PAGE_MASK) >> RK_IOVA_PAGE_SHIFT;
}

static u32 rk_iommu_read(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static void rk_iommu_write(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}

static void rk_iommu_command(struct rk_iommu *iommu, u32 command)
{
	int i;

	for (i = 0; i < iommu->num_mmu; i++)
		writel(command, iommu->bases[i] + RK_MMU_COMMAND);
}

static void rk_iommu_base_command(void __iomem *base, u32 command)
{
	writel(command, base + RK_MMU_COMMAND);
}
static void rk_iommu_zap_lines(struct rk_iommu *iommu, dma_addr_t iova,
			       size_t size)
{
	int i;

	dma_addr_t iova_end = iova + size;
	/*
	 * TODO(djkurtz): Figure out when it is more efficient to shootdown the
	 * entire iotlb rather than iterate over individual iovas.
	 */
	for (i = 0; i < iommu->num_mmu; i++)
		for (; iova < iova_end; iova += SPAGE_SIZE)
			rk_iommu_write(iommu->bases[i], RK_MMU_ZAP_ONE_LINE, iova);
}

static bool rk_iommu_is_stall_active(struct rk_iommu *iommu)
{
	bool active = true;
	int i;

	for (i = 0; i < iommu->num_mmu; i++)
		active &= !!(rk_iommu_read(iommu->bases[i], RK_MMU_STATUS) &
					   RK_MMU_STATUS_STALL_ACTIVE);

	return active;
}

static bool rk_iommu_is_paging_enabled(struct rk_iommu *iommu)
{
	bool enable = true;
	int i;

	for (i = 0; i < iommu->num_mmu; i++)
		enable &= !!(rk_iommu_read(iommu->bases[i], RK_MMU_STATUS) &
					   RK_MMU_STATUS_PAGING_ENABLED);

	return enable;
}

static int rk_iommu_enable_stall(struct rk_iommu *iommu)
{
	int ret, i;

	if (rk_iommu_is_stall_active(iommu))
		return 0;

	/* Stall can only be enabled if paging is enabled */
	if (!rk_iommu_is_paging_enabled(iommu))
		return 0;

	rk_iommu_command(iommu, RK_MMU_CMD_ENABLE_STALL);

	ret = rk_wait_for(rk_iommu_is_stall_active(iommu), 1);
	if (ret)
		for (i = 0; i < iommu->num_mmu; i++)
			dev_err(iommu->dev, "Enable stall request timed out, status: %#08x\n",
				rk_iommu_read(iommu->bases[i], RK_MMU_STATUS));

	return ret;
}

static int rk_iommu_disable_stall(struct rk_iommu *iommu)
{
	int ret, i;

	if (!rk_iommu_is_stall_active(iommu))
		return 0;

	rk_iommu_command(iommu, RK_MMU_CMD_DISABLE_STALL);

	ret = rk_wait_for(!rk_iommu_is_stall_active(iommu), 1);
	if (ret)
		for (i = 0; i < iommu->num_mmu; i++)
			dev_err(iommu->dev, "Disable stall request timed out, status: %#08x\n",
				rk_iommu_read(iommu->bases[i], RK_MMU_STATUS));

	return ret;
}

static int rk_iommu_enable_paging(struct rk_iommu *iommu)
{
	int ret, i;

	if (rk_iommu_is_paging_enabled(iommu))
		return 0;

	rk_iommu_command(iommu, RK_MMU_CMD_ENABLE_PAGING);

	ret = rk_wait_for(rk_iommu_is_paging_enabled(iommu), 1);
	if (ret)
		for (i = 0; i < iommu->num_mmu; i++)
			dev_err(iommu->dev, "Enable paging request timed out, status: %#08x\n",
				rk_iommu_read(iommu->bases[i], RK_MMU_STATUS));

	return ret;
}

static int rk_iommu_disable_paging(struct rk_iommu *iommu)
{
	int ret, i;

	if (!rk_iommu_is_paging_enabled(iommu))
		return 0;

	rk_iommu_command(iommu, RK_MMU_CMD_DISABLE_PAGING);

	ret = rk_wait_for(!rk_iommu_is_paging_enabled(iommu), 1);
	if (ret)
		for (i = 0; i < iommu->num_mmu; i++)
			dev_err(iommu->dev, "Disable paging request timed out, status: %#08x\n",
				rk_iommu_read(iommu->bases[i], RK_MMU_STATUS));

	return ret;
}

static int rk_iommu_force_reset(struct rk_iommu *iommu)
{
	int ret, i;
	u32 dte_addr;

	/*
	 * Check if register DTE_ADDR is working by writing DTE_ADDR_DUMMY
	 * and verifying that upper 5 nybbles are read back.
	 */
	for (i = 0; i < iommu->num_mmu; i++) {
		rk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR, DTE_ADDR_DUMMY);

		dte_addr = rk_iommu_read(iommu->bases[i], RK_MMU_DTE_ADDR);
		if (dte_addr != (DTE_ADDR_DUMMY & RK_DTE_PT_ADDRESS_MASK)) {
			dev_err(iommu->dev, "Error during raw reset. MMU_DTE_ADDR is not functioning\n");
			return -EFAULT;
		}
	}

	rk_iommu_command(iommu, RK_MMU_CMD_FORCE_RESET);

	for (i = 0; i < iommu->num_mmu; i++) {
		ret = rk_wait_for(rk_iommu_read(iommu->bases[i], RK_MMU_DTE_ADDR) == 0x00000000,
				  FORCE_RESET_TIMEOUT);
		if (ret) {
			dev_err(iommu->dev, "FORCE_RESET command timed out\n");
			return ret;
		}
	}

	return 0;
}

static void log_iova(struct rk_iommu *iommu, int index, dma_addr_t iova)
{
	void __iomem *base = iommu->bases[index];
	u32 dte_index, pte_index, page_offset;
	u32 mmu_dte_addr;
	phys_addr_t mmu_dte_addr_phys, dte_addr_phys;
	u32 *dte_addr;
	u32 dte;
	phys_addr_t pte_addr_phys = 0;
	u32 *pte_addr = NULL;
	u32 pte = 0;
	phys_addr_t page_addr_phys = 0;
	u32 page_flags = 0;

	dte_index = rk_iova_dte_index(iova);
	pte_index = rk_iova_pte_index(iova);
	page_offset = rk_iova_page_offset(iova);

	mmu_dte_addr = rk_iommu_read(base, RK_MMU_DTE_ADDR);
	mmu_dte_addr_phys = (phys_addr_t)mmu_dte_addr;

	dte_addr_phys = mmu_dte_addr_phys + (4 * dte_index);
	dte_addr = phys_to_virt(dte_addr_phys);
	dte = *dte_addr;

	if (!rk_dte_is_pt_valid(dte))
		goto print_it;

	pte_addr_phys = rk_dte_pt_address(dte) + (pte_index * 4);
	pte_addr = phys_to_virt(pte_addr_phys);
	pte = *pte_addr;

	if (!rk_pte_is_page_valid(pte))
		goto print_it;

	page_addr_phys = rk_pte_page_address(pte) + page_offset;
	page_flags = pte & RK_PTE_PAGE_FLAGS_MASK;

print_it:
	dev_err(iommu->dev, "iova = %pad: dte_index: %#03x pte_index: %#03x page_offset: %#03x\n",
		&iova, dte_index, pte_index, page_offset);
	dev_err(iommu->dev, "mmu_dte_addr: %pa dte@%pa: %#08x valid: %u pte@%pa: %#08x valid: %u page@%pa flags: %#03x\n",
		&mmu_dte_addr_phys, &dte_addr_phys, dte,
		rk_dte_is_pt_valid(dte), &pte_addr_phys, pte,
		rk_pte_is_page_valid(pte), &page_addr_phys, page_flags);
}

static irqreturn_t rk_iommu_irq(int irq, void *dev_id)
{
	struct rk_iommu *iommu = dev_id;
	u32 status;
	u32 int_status;
	dma_addr_t iova;
	irqreturn_t ret = IRQ_NONE;
	int i;

	for (i = 0; i < iommu->num_mmu; i++) {
		int_status = rk_iommu_read(iommu->bases[i], RK_MMU_INT_STATUS);
		if (int_status == 0)
			continue;

		ret = IRQ_HANDLED;
		iova = rk_iommu_read(iommu->bases[i], RK_MMU_PAGE_FAULT_ADDR);

		if (int_status & RK_MMU_IRQ_PAGE_FAULT) {
			int flags;

			status = rk_iommu_read(iommu->bases[i], RK_MMU_STATUS);
			flags = (status & RK_MMU_STATUS_PAGE_FAULT_IS_WRITE) ?
					IOMMU_FAULT_WRITE : IOMMU_FAULT_READ;

			dev_err(iommu->dev, "Page fault at %pad of type %s\n",
				&iova,
				(flags == IOMMU_FAULT_WRITE) ? "write" : "read");

			log_iova(iommu, i, iova);

			/*
			 * Report page fault to any installed handlers.
			 * Ignore the return code, though, since we always zap cache
			 * and clear the page fault anyway.
			 */
			if (iommu->domain)
				report_iommu_fault(iommu->domain, iommu->dev, iova,
						   flags);
			else
				dev_err(iommu->dev, "Page fault while iommu not attached to domain?\n");

			rk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);
			rk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_PAGE_FAULT_DONE);
		}

		if (int_status & RK_MMU_IRQ_BUS_ERROR)
			dev_err(iommu->dev, "BUS_ERROR occurred at %pad\n", &iova);

		if (int_status & ~RK_MMU_IRQ_MASK)
			dev_err(iommu->dev, "unexpected int_status: %#08x\n",
				int_status);

		rk_iommu_write(iommu->bases[i], RK_MMU_INT_CLEAR, int_status);
	}

	return ret;
}

static phys_addr_t rk_iommu_iova_to_phys(struct iommu_domain *domain,
					 dma_addr_t iova)
{
	struct rk_iommu_domain *rk_domain = to_rk_domain(domain);
	unsigned long flags;
	phys_addr_t pt_phys, phys = 0;
	u32 dte, pte;
	u32 *page_table;

	spin_lock_irqsave(&rk_domain->dt_lock, flags);

	dte = rk_domain->dt[rk_iova_dte_index(iova)];
	if (!rk_dte_is_pt_valid(dte))
		goto out;

	pt_phys = rk_dte_pt_address(dte);
	page_table = (u32 *)phys_to_virt(pt_phys);
	pte = page_table[rk_iova_pte_index(iova)];
	if (!rk_pte_is_page_valid(pte))
		goto out;

	phys = rk_pte_page_address(pte) + rk_iova_page_offset(iova);
out:
	spin_unlock_irqrestore(&rk_domain->dt_lock, flags);

	return phys;
}

static void rk_iommu_zap_iova(struct rk_iommu_domain *rk_domain,
			      dma_addr_t iova, size_t size)
{
	struct list_head *pos;
	unsigned long flags;

	/* shootdown these iova from all iommus using this domain */
	spin_lock_irqsave(&rk_domain->iommus_lock, flags);
	list_for_each(pos, &rk_domain->iommus) {
		struct rk_iommu *iommu;
		iommu = list_entry(pos, struct rk_iommu, node);
		rk_iommu_zap_lines(iommu, iova, size);
	}
	spin_unlock_irqrestore(&rk_domain->iommus_lock, flags);
}

static void rk_iommu_zap_iova_first_last(struct rk_iommu_domain *rk_domain,
					 dma_addr_t iova, size_t size)
{
	rk_iommu_zap_iova(rk_domain, iova, SPAGE_SIZE);
	if (size > SPAGE_SIZE)
		rk_iommu_zap_iova(rk_domain, iova + size - SPAGE_SIZE,
					SPAGE_SIZE);
}

static u32 *rk_dte_get_page_table(struct rk_iommu_domain *rk_domain,
				  dma_addr_t iova)
{
	struct device *dev = &rk_domain->pdev->dev;
	u32 *page_table, *dte_addr;
	u32 dte_index, dte;
	phys_addr_t pt_phys;
	dma_addr_t pt_dma;

	assert_spin_locked(&rk_domain->dt_lock);

	dte_index = rk_iova_dte_index(iova);
	dte_addr = &rk_domain->dt[dte_index];
	dte = *dte_addr;
	if (rk_dte_is_pt_valid(dte))
		goto done;

	page_table = (u32 *)get_zeroed_page(GFP_ATOMIC | GFP_DMA32);
	if (!page_table)
		return ERR_PTR(-ENOMEM);

	pt_dma = dma_map_single(dev, page_table, SPAGE_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pt_dma)) {
		dev_err(dev, "DMA mapping error while allocating page table\n");
		free_page((unsigned long)page_table);
		return ERR_PTR(-ENOMEM);
	}

	dte = rk_mk_dte(pt_dma);
	*dte_addr = dte;

	rk_table_flush(rk_domain, pt_dma, NUM_PT_ENTRIES);
	rk_table_flush(rk_domain,
		       rk_domain->dt_dma + dte_index * sizeof(u32), 1);
done:
	pt_phys = rk_dte_pt_address(dte);
	return (u32 *)phys_to_virt(pt_phys);
}

static size_t rk_iommu_unmap_iova(struct rk_iommu_domain *rk_domain,
				  u32 *pte_addr, dma_addr_t pte_dma,
				  size_t size)
{
	unsigned int pte_count;
	unsigned int pte_total = size / SPAGE_SIZE;

	assert_spin_locked(&rk_domain->dt_lock);

	for (pte_count = 0; pte_count < pte_total; pte_count++) {
		u32 pte = pte_addr[pte_count];
		if (!rk_pte_is_page_valid(pte))
			break;

		pte_addr[pte_count] = rk_mk_pte_invalid(pte);
	}

	rk_table_flush(rk_domain, pte_dma, pte_count);

	return pte_count * SPAGE_SIZE;
}

static int rk_iommu_map_iova(struct rk_iommu_domain *rk_domain, u32 *pte_addr,
			     dma_addr_t pte_dma, dma_addr_t iova,
			     phys_addr_t paddr, size_t size, int prot)
{
	unsigned int pte_count;
	unsigned int pte_total = size / SPAGE_SIZE;
	phys_addr_t page_phys;

	assert_spin_locked(&rk_domain->dt_lock);

	for (pte_count = 0; pte_count < pte_total; pte_count++) {
		u32 pte = pte_addr[pte_count];

		if (rk_pte_is_page_valid(pte))
			goto unwind;

		pte_addr[pte_count] = rk_mk_pte(paddr, prot);

		paddr += SPAGE_SIZE;
	}

	rk_table_flush(rk_domain, pte_dma, pte_total);

	/*
	 * Zap the first and last iova to evict from iotlb any previously
	 * mapped cachelines holding stale values for its dte and pte.
	 * We only zap the first and last iova, since only they could have
	 * dte or pte shared with an existing mapping.
	 */
	rk_iommu_zap_iova_first_last(rk_domain, iova, size);

	return 0;
unwind:
	/* Unmap the range of iovas that we just mapped */
	rk_iommu_unmap_iova(rk_domain, pte_addr, pte_dma,
			    pte_count * SPAGE_SIZE);

	iova += pte_count * SPAGE_SIZE;
	page_phys = rk_pte_page_address(pte_addr[pte_count]);
	pr_err("iova: %pad already mapped to %pa cannot remap to phys: %pa prot: %#x\n",
	       &iova, &page_phys, &paddr, prot);

	return -EADDRINUSE;
}

static int rk_iommu_map(struct iommu_domain *domain, unsigned long _iova,
			phys_addr_t paddr, size_t size, int prot)
{
	struct rk_iommu_domain *rk_domain = to_rk_domain(domain);
	unsigned long flags;
	dma_addr_t pte_dma, iova = (dma_addr_t)_iova;
	u32 *page_table, *pte_addr;
	u32 dte_index, pte_index;
	int ret;

	spin_lock_irqsave(&rk_domain->dt_lock, flags);

	/*
	 * pgsize_bitmap specifies iova sizes that fit in one page table
	 * (1024 4-KiB pages = 4 MiB).
	 * So, size will always be 4096 <= size <= 4194304.
	 * Since iommu_map() guarantees that both iova and size will be
	 * aligned, we will always only be mapping from a single dte here.
	 */
	page_table = rk_dte_get_page_table(rk_domain, iova);
	if (IS_ERR(page_table)) {
		spin_unlock_irqrestore(&rk_domain->dt_lock, flags);
		return PTR_ERR(page_table);
	}

	dte_index = rk_domain->dt[rk_iova_dte_index(iova)];
	pte_index = rk_iova_pte_index(iova);
	pte_addr = &page_table[pte_index];
	pte_dma = rk_dte_pt_address(dte_index) + pte_index * sizeof(u32);
	ret = rk_iommu_map_iova(rk_domain, pte_addr, pte_dma, iova,
				paddr, size, prot);

	spin_unlock_irqrestore(&rk_domain->dt_lock, flags);

	return ret;
}

static size_t rk_iommu_unmap(struct iommu_domain *domain, unsigned long _iova,
			     size_t size)
{
	struct rk_iommu_domain *rk_domain = to_rk_domain(domain);
	unsigned long flags;
	dma_addr_t pte_dma, iova = (dma_addr_t)_iova;
	phys_addr_t pt_phys;
	u32 dte;
	u32 *pte_addr;
	size_t unmap_size;

	spin_lock_irqsave(&rk_domain->dt_lock, flags);

	/*
	 * pgsize_bitmap specifies iova sizes that fit in one page table
	 * (1024 4-KiB pages = 4 MiB).
	 * So, size will always be 4096 <= size <= 4194304.
	 * Since iommu_unmap() guarantees that both iova and size will be
	 * aligned, we will always only be unmapping from a single dte here.
	 */
	dte = rk_domain->dt[rk_iova_dte_index(iova)];
	/* Just return 0 if iova is unmapped */
	if (!rk_dte_is_pt_valid(dte)) {
		spin_unlock_irqrestore(&rk_domain->dt_lock, flags);
		return 0;
	}

	pt_phys = rk_dte_pt_address(dte);
	pte_addr = (u32 *)phys_to_virt(pt_phys) + rk_iova_pte_index(iova);
	pte_dma = pt_phys + rk_iova_pte_index(iova) * sizeof(u32);
	unmap_size = rk_iommu_unmap_iova(rk_domain, pte_addr, pte_dma, size);

	spin_unlock_irqrestore(&rk_domain->dt_lock, flags);

	/* Shootdown iotlb entries for iova range that was just unmapped */
	rk_iommu_zap_iova(rk_domain, iova, unmap_size);

	return unmap_size;
}

static struct rk_iommu *rk_iommu_from_dev(struct device *dev)
{
	struct iommu_group *group;
	struct device *iommu_dev;
	struct rk_iommu *rk_iommu;

	group = iommu_group_get(dev);
	if (!group)
		return NULL;
	iommu_dev = iommu_group_get_iommudata(group);
	rk_iommu = dev_get_drvdata(iommu_dev);
	iommu_group_put(group);

	return rk_iommu;
}

static int rk_iommu_attach_device(struct iommu_domain *domain,
				  struct device *dev)
{
	struct rk_iommu *iommu;
	struct rk_iommu_domain *rk_domain = to_rk_domain(domain);
	unsigned long flags;
	int ret, i;

	/*
	 * Allow 'virtual devices' (e.g., drm) to attach to domain.
	 * Such a device does not belong to an iommu group.
	 */
	iommu = rk_iommu_from_dev(dev);
	if (!iommu)
		return 0;

	ret = rk_iommu_enable_stall(iommu);
	if (ret)
		return ret;

	ret = rk_iommu_force_reset(iommu);
	if (ret)
		return ret;

	iommu->domain = domain;

	ret = devm_request_irq(iommu->dev, iommu->irq, rk_iommu_irq,
			       IRQF_SHARED, dev_name(dev), iommu);
	if (ret)
		return ret;

	for (i = 0; i < iommu->num_mmu; i++) {
		rk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR,
			       rk_domain->dt_dma);
		rk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);
		rk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, RK_MMU_IRQ_MASK);
	}

	ret = rk_iommu_enable_paging(iommu);
	if (ret)
		return ret;

	spin_lock_irqsave(&rk_domain->iommus_lock, flags);
	list_add_tail(&iommu->node, &rk_domain->iommus);
	spin_unlock_irqrestore(&rk_domain->iommus_lock, flags);

	dev_dbg(dev, "Attached to iommu domain\n");

	rk_iommu_disable_stall(iommu);

	return 0;
}

static void rk_iommu_detach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	struct rk_iommu *iommu;
	struct rk_iommu_domain *rk_domain = to_rk_domain(domain);
	unsigned long flags;
	int i;

	/* Allow 'virtual devices' (eg drm) to detach from domain */
	iommu = rk_iommu_from_dev(dev);
	if (!iommu)
		return;

	spin_lock_irqsave(&rk_domain->iommus_lock, flags);
	list_del_init(&iommu->node);
	spin_unlock_irqrestore(&rk_domain->iommus_lock, flags);

	/* Ignore error while disabling, just keep going */
	rk_iommu_enable_stall(iommu);
	rk_iommu_disable_paging(iommu);
	for (i = 0; i < iommu->num_mmu; i++) {
		rk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, 0);
		rk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR, 0);
	}
	rk_iommu_disable_stall(iommu);

	devm_free_irq(iommu->dev, iommu->irq, iommu);

	iommu->domain = NULL;

	dev_dbg(dev, "Detached from iommu domain\n");
}

static struct iommu_domain *rk_iommu_domain_alloc(unsigned type)
{
	struct rk_iommu_domain *rk_domain;
	struct platform_device *pdev;
	struct device *iommu_dev;

	if (type != IOMMU_DOMAIN_UNMANAGED && type != IOMMU_DOMAIN_DMA)
		return NULL;

	/* Register a pdev per domain, so DMA API can base on this *dev
	 * even some virtual master doesn't have an iommu slave
	 */
	pdev = platform_device_register_simple("rk_iommu_domain",
					       PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(pdev))
		return NULL;

	rk_domain = devm_kzalloc(&pdev->dev, sizeof(*rk_domain), GFP_KERNEL);
	if (!rk_domain)
		goto err_unreg_pdev;

	rk_domain->pdev = pdev;

	if (type == IOMMU_DOMAIN_DMA &&
	    iommu_get_dma_cookie(&rk_domain->domain))
		goto err_unreg_pdev;

	/*
	 * rk32xx iommus use a 2 level pagetable.
	 * Each level1 (dt) and level2 (pt) table has 1024 4-byte entries.
	 * Allocate one 4 KiB page for each table.
	 */
	rk_domain->dt = (u32 *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	if (!rk_domain->dt)
		goto err_put_cookie;

	iommu_dev = &pdev->dev;
	rk_domain->dt_dma = dma_map_single(iommu_dev, rk_domain->dt,
					   SPAGE_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(iommu_dev, rk_domain->dt_dma)) {
		dev_err(iommu_dev, "DMA map error for DT\n");
		goto err_free_dt;
	}

	rk_table_flush(rk_domain, rk_domain->dt_dma, NUM_DT_ENTRIES);

	spin_lock_init(&rk_domain->iommus_lock);
	spin_lock_init(&rk_domain->dt_lock);
	INIT_LIST_HEAD(&rk_domain->iommus);

	rk_domain->domain.geometry.aperture_start = 0;
	rk_domain->domain.geometry.aperture_end   = DMA_BIT_MASK(32);
	rk_domain->domain.geometry.force_aperture = true;

	return &rk_domain->domain;

err_free_dt:
	free_page((unsigned long)rk_domain->dt);
err_put_cookie:
	if (type == IOMMU_DOMAIN_DMA)
		iommu_put_dma_cookie(&rk_domain->domain);
err_unreg_pdev:
	platform_device_unregister(pdev);

	return NULL;
}

static void rk_iommu_domain_free(struct iommu_domain *domain)
{
	struct rk_iommu_domain *rk_domain = to_rk_domain(domain);
	int i;

	WARN_ON(!list_empty(&rk_domain->iommus));

	for (i = 0; i < NUM_DT_ENTRIES; i++) {
		u32 dte = rk_domain->dt[i];
		if (rk_dte_is_pt_valid(dte)) {
			phys_addr_t pt_phys = rk_dte_pt_address(dte);
			u32 *page_table = phys_to_virt(pt_phys);
			dma_unmap_single(&rk_domain->pdev->dev, pt_phys,
					 SPAGE_SIZE, DMA_TO_DEVICE);
			free_page((unsigned long)page_table);
		}
	}

	dma_unmap_single(&rk_domain->pdev->dev, rk_domain->dt_dma,
			 SPAGE_SIZE, DMA_TO_DEVICE);
	free_page((unsigned long)rk_domain->dt);

	if (domain->type == IOMMU_DOMAIN_DMA)
		iommu_put_dma_cookie(&rk_domain->domain);

	platform_device_unregister(rk_domain->pdev);
}

static bool rk_iommu_is_dev_iommu_master(struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret;

	/*
	 * An iommu master has an iommus property containing a list of phandles
	 * to iommu nodes, each with an #iommu-cells property with value 0.
	 */
	ret = of_count_phandle_with_args(np, "iommus", "#iommu-cells");
	return (ret > 0);
}

static int rk_iommu_group_set_iommudata(struct iommu_group *group,
					struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct platform_device *pd;
	int ret;
	struct of_phandle_args args;

	/*
	 * An iommu master has an iommus property containing a list of phandles
	 * to iommu nodes, each with an #iommu-cells property with value 0.
	 */
	ret = of_parse_phandle_with_args(np, "iommus", "#iommu-cells", 0,
					 &args);
	if (ret) {
		dev_err(dev, "of_parse_phandle_with_args(%s) => %d\n",
			np->full_name, ret);
		return ret;
	}
	if (args.args_count != 0) {
		dev_err(dev, "incorrect number of iommu params found for %s (found %d, expected 0)\n",
			args.np->full_name, args.args_count);
		return -EINVAL;
	}

	pd = of_find_device_by_node(args.np);
	of_node_put(args.np);
	if (!pd) {
		dev_err(dev, "iommu %s not found\n", args.np->full_name);
		return -EPROBE_DEFER;
	}

	/* TODO(djkurtz): handle multiple slave iommus for a single master */
	iommu_group_set_iommudata(group, &pd->dev, NULL);

	return 0;
}

static int rk_iommu_add_device(struct device *dev)
{
	struct iommu_group *group;
	int ret;

	if (!rk_iommu_is_dev_iommu_master(dev))
		return -ENODEV;

	group = iommu_group_get(dev);
	if (!group) {
		group = iommu_group_alloc();
		if (IS_ERR(group)) {
			dev_err(dev, "Failed to allocate IOMMU group\n");
			return PTR_ERR(group);
		}
	}

	ret = iommu_group_add_device(group, dev);
	if (ret)
		goto err_put_group;

	ret = rk_iommu_group_set_iommudata(group, dev);
	if (ret)
		goto err_remove_device;

	iommu_group_put(group);

	return 0;

err_remove_device:
	iommu_group_remove_device(dev);
err_put_group:
	iommu_group_put(group);
	return ret;
}

static void rk_iommu_remove_device(struct device *dev)
{
	if (!rk_iommu_is_dev_iommu_master(dev))
		return;

	iommu_group_remove_device(dev);
}

static const struct iommu_ops rk_iommu_ops = {
	.domain_alloc = rk_iommu_domain_alloc,
	.domain_free = rk_iommu_domain_free,
	.attach_dev = rk_iommu_attach_device,
	.detach_dev = rk_iommu_detach_device,
	.map = rk_iommu_map,
	.unmap = rk_iommu_unmap,
	.map_sg = default_iommu_map_sg,
	.add_device = rk_iommu_add_device,
	.remove_device = rk_iommu_remove_device,
	.iova_to_phys = rk_iommu_iova_to_phys,
	.pgsize_bitmap = RK_IOMMU_PGSIZE_BITMAP,
};

static int rk_iommu_domain_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev->dma_parms = devm_kzalloc(dev, sizeof(*dev->dma_parms), GFP_KERNEL);
	if (!dev->dma_parms)
		return -ENOMEM;

	/* Set dma_ops for dev, otherwise it would be dummy_dma_ops */
	arch_setup_dma_ops(dev, 0, DMA_BIT_MASK(32), NULL, false);

	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));
	dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));

	return 0;
}

static struct platform_driver rk_iommu_domain_driver = {
	.probe = rk_iommu_domain_probe,
	.driver = {
		   .name = "rk_iommu_domain",
	},
};

static int rk_iommu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_iommu *iommu;
	struct resource *res;
	int num_res = pdev->num_resources;
	int i;

	iommu = devm_kzalloc(dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;

	platform_set_drvdata(pdev, iommu);
	iommu->dev = dev;
	iommu->num_mmu = 0;

	iommu->bases = devm_kzalloc(dev, sizeof(*iommu->bases) * num_res,
				    GFP_KERNEL);
	if (!iommu->bases)
		return -ENOMEM;

	for (i = 0; i < num_res; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			continue;
		iommu->bases[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(iommu->bases[i]))
			continue;
		iommu->num_mmu++;
	}
	if (iommu->num_mmu == 0)
		return PTR_ERR(iommu->bases[0]);

	iommu->irq = platform_get_irq(pdev, 0);
	if (iommu->irq < 0) {
		dev_err(dev, "Failed to get IRQ, %d\n", iommu->irq);
		return -ENXIO;
	}

	return 0;
}

static int rk_iommu_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id rk_iommu_dt_ids[] = {
	{ .compatible = "rockchip,iommu" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rk_iommu_dt_ids);

static struct platform_driver rk_iommu_driver = {
	.probe = rk_iommu_probe,
	.remove = rk_iommu_remove,
	.driver = {
		   .name = "rk_iommu",
		   .of_match_table = rk_iommu_dt_ids,
	},
};

static int __init rk_iommu_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, rk_iommu_dt_ids);
	if (!np)
		return 0;

	of_node_put(np);

	ret = bus_set_iommu(&platform_bus_type, &rk_iommu_ops);
	if (ret)
		return ret;

	ret = platform_driver_register(&rk_iommu_domain_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&rk_iommu_driver);
	if (ret)
		platform_driver_unregister(&rk_iommu_domain_driver);
	return ret;
}
static void __exit rk_iommu_exit(void)
{
	platform_driver_unregister(&rk_iommu_driver);
	platform_driver_unregister(&rk_iommu_domain_driver);
}

subsys_initcall(rk_iommu_init);
module_exit(rk_iommu_exit);

MODULE_DESCRIPTION("IOMMU API for Rockchip");
MODULE_AUTHOR("Simon Xue <xxm@rock-chips.com> and Daniel Kurtz <djkurtz@chromium.org>");
MODULE_ALIAS("platform:rockchip-iommu");
MODULE_LICENSE("GPL v2");

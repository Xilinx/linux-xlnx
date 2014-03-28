/*
 * Suspend support for Zynq
 *
 *  Copyright (C) 2012 Xilinx
 *
 *  Soren Brinkmann <soren.brinkmann@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk/zynq.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/suspend.h>
#include "common.h"

#define DDRC_CTRL_REG1_OFFS		0x60
#define DDRC_DRAM_PARAM_REG3_OFFS	0x20
#define SCU_CTRL			0

#define DDRC_CLOCKSTOP_MASK	BIT(23)
#define DDRC_SELFREFRESH_MASK	BIT(12)
#define SCU_STBY_EN_MASK	BIT(5)

static void __iomem *ddrc_base;
static void __iomem *ocm_base;

static int zynq_pm_prepare_late(void)
{
	return zynq_clk_suspend_early();
}

static void zynq_pm_wake(void)
{
	zynq_clk_resume_late();
}

static int zynq_pm_suspend(unsigned long arg)
{
	u32 reg;
	int (*zynq_suspend_ptr)(void __iomem *, void __iomem *);
	int do_ddrpll_bypass = 1;

	/* Enable DDR self-refresh and clock stop */
	if (ddrc_base) {
		reg = readl(ddrc_base + DDRC_CTRL_REG1_OFFS);
		reg |= DDRC_SELFREFRESH_MASK;
		writel(reg, ddrc_base + DDRC_CTRL_REG1_OFFS);

		reg = readl(ddrc_base + DDRC_DRAM_PARAM_REG3_OFFS);
		reg |= DDRC_CLOCKSTOP_MASK;
		writel(reg, ddrc_base + DDRC_DRAM_PARAM_REG3_OFFS);
	} else {
		do_ddrpll_bypass = 0;
	}

	/* SCU standby mode */
	if (zynq_scu_base) {
		reg = readl(zynq_scu_base + SCU_CTRL);
		reg |= SCU_STBY_EN_MASK;
		writel(reg, zynq_scu_base + SCU_CTRL);
	}

	/* Topswitch clock stop disable */
	zynq_clk_topswitch_disable();

	/* A9 clock gating */
	asm volatile ("mrc  p15, 0, r12, c15, c0, 0\n"
		      "orr  r12, r12, #1\n"
		      "mcr  p15, 0, r12, c15, c0, 0\n"
		      : /* no outputs */
		      : /* no inputs */
		      : "r12");

	if (ocm_base) {
		/*
		 * Copy code to suspend system into OCM. The suspend code
		 * needs to run from OCM as DRAM may no longer be available
		 * when the PLL is stopped.
		 */
		memcpy((__force void *)ocm_base, &zynq_sys_suspend,
			zynq_sys_suspend_sz);
		flush_icache_range((unsigned long)ocm_base,
			(unsigned long)(ocm_base) + zynq_sys_suspend_sz);
		zynq_suspend_ptr = (__force void *)ocm_base;
	} else {
		do_ddrpll_bypass = 0;
	}

	/* Transfer to suspend code in OCM */
	if (do_ddrpll_bypass) {
		/*
		 * Going this way will turn off DDR related clocks and the DDR
		 * PLL. I.e. We might brake sub systems relying on any of this
		 * clocks. And even worse: If there are any other masters in the
		 * system (e.g. in the PL) accessing DDR they are screwed.
		 */
		flush_cache_all();
		if (zynq_suspend_ptr(ddrc_base, zynq_slcr_base))
			pr_warn("DDR self refresh failed.\n");
	} else {
		WARN_ONCE(1, "DRAM self-refresh not available\n");
		cpu_do_idle();
	}

	/* Topswitch clock stop enable */
	zynq_clk_topswitch_enable();

	/* SCU standby mode */
	if (zynq_scu_base) {
		reg = readl(zynq_scu_base + SCU_CTRL);
		reg &= ~SCU_STBY_EN_MASK;
		writel(reg, zynq_scu_base + SCU_CTRL);
	}

	/* A9 clock gating */
	asm volatile ("mrc  p15, 0, r12, c15, c0, 0\n"
		      "bic  r12, r12, #1\n"
		      "mcr  p15, 0, r12, c15, c0, 0\n"
		      : /* no outputs */
		      : /* no inputs */
		      : "r12");

	/* Disable DDR self-refresh and clock stop */
	if (ddrc_base) {
		reg = readl(ddrc_base + DDRC_CTRL_REG1_OFFS);
		reg &= ~DDRC_SELFREFRESH_MASK;
		writel(reg, ddrc_base + DDRC_CTRL_REG1_OFFS);

		reg = readl(ddrc_base + DDRC_DRAM_PARAM_REG3_OFFS);
		reg &= ~DDRC_CLOCKSTOP_MASK;
		writel(reg, ddrc_base + DDRC_DRAM_PARAM_REG3_OFFS);
	}

	return 0;
}

static int zynq_pm_enter(suspend_state_t suspend_state)
{
	switch (suspend_state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		outer_disable();
		cpu_suspend(0, zynq_pm_suspend);
		outer_resume();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct platform_suspend_ops zynq_pm_ops = {
	.prepare_late	= zynq_pm_prepare_late,
	.enter		= zynq_pm_enter,
	.wake		= zynq_pm_wake,
	.valid		= suspend_valid_only_mem,
};

/**
 * zynq_pm_ioremap() - Create IO mappings
 * @comp:	DT compatible string
 * Returns a pointer to the mapped memory or NULL.
 *
 * Remap the memory region for a compatible DT node.
 */
static void __iomem *zynq_pm_ioremap(const char *comp)
{
	struct device_node *np;
	void __iomem *base = NULL;

	np = of_find_compatible_node(NULL, NULL, comp);
	if (np) {
		base = of_iomap(np, 0);
		of_node_put(np);
	} else {
		pr_warn("%s: no compatible node found for '%s'\n", __func__,
				comp);
	}

	return base;
}

/**
 * zynq_pm_remap_ocm() - Remap OCM
 * Returns a pointer to the mapped memory or NULL.
 *
 * Remap the OCM.
 */
static void __iomem *zynq_pm_remap_ocm(void)
{
	struct device_node *np;
	const char *comp = "xlnx,zynq-ocmc-1.0";
	void __iomem *base = NULL;

	np = of_find_compatible_node(NULL, NULL, comp);
	if (np) {
		struct device *dev;
		unsigned long pool_addr;
		unsigned long pool_addr_virt;
		struct gen_pool *pool;

		of_node_put(np);

		dev = &(of_find_device_by_node(np)->dev);

		/* Get OCM pool from device tree or platform data */
		pool = dev_get_gen_pool(dev);
		if (!pool) {
			pr_warn("%s: OCM pool is not available\n", __func__);
			return NULL;
		}

		pool_addr_virt = gen_pool_alloc(pool, zynq_sys_suspend_sz);
		if (!pool_addr_virt) {
			pr_warn("%s: Can't get OCM poll\n", __func__);
			return NULL;
		}
		pool_addr = gen_pool_virt_to_phys(pool, pool_addr_virt);
		if (!pool_addr) {
			pr_warn("%s: Can't get physical address of OCM pool\n",
				__func__);
			return NULL;
		}
		base = __arm_ioremap(pool_addr, zynq_sys_suspend_sz, MT_MEMORY);
		if (!base) {
			pr_warn("%s: IOremap OCM pool failed\n", __func__);
			return NULL;
		}
		pr_debug("%s: Remap OCM %s from %lx to %lx\n", __func__, comp,
			 pool_addr_virt, (unsigned long)base);
	} else {
		pr_warn("%s: no compatible node found for '%s'\n", __func__,
				comp);
	}

	return base;
}

int __init zynq_pm_late_init(void)
{
	ddrc_base = zynq_pm_ioremap("xlnx,zynq-ddrc-1.0");
	if (!ddrc_base)
		pr_warn("%s: Unable to map DDRC IO memory.\n", __func__);

	/*
	 * FIXME: should be done by an ocm driver which then provides allocators
	 */
	ocm_base = zynq_pm_remap_ocm();
	if (!ocm_base)
		pr_warn("%s: Unable to map OCM.\n", __func__);

	suspend_set_ops(&zynq_pm_ops);

	return 0;
}

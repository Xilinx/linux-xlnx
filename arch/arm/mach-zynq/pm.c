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

#include <linux/clk.h>
#include <linux/bitops.h>
#include <linux/err.h>
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
#include <linux/genalloc.h>
#include "common.h"

#define DDRC_CTRL_REG1_OFFS		0x60
#define DDRC_DRAM_PARAM_REG3_OFFS	0x20
#define SCU_CTRL			0
#define SLCR_TOPSW_CLK_CTRL		0x16c

#define DDRC_CLOCKSTOP_MASK	BIT(23)
#define DDRC_SELFREFRESH_MASK	BIT(12)
#define SCU_STBY_EN_MASK	BIT(5)
#define TOPSW_CLK_CTRL_DIS_MASK	BIT(0)

static struct clk *cpupll;

/**
 * zynq_pm_ioremap() - Create IO mappings
 * @action	0: map, !0: unmap
 * @comp	DT compatible string
 * Returns a pointer to the mapped memory or NULL.
 *
 * Remap the memory region for a compatible DT node.
 */
static void __iomem *zynq_pm_ioremap(u32 action, const char *comp)
{
	static struct device_node *np;
	static void __iomem *base;

	if (!action) {
		np = of_find_compatible_node(NULL, NULL, comp);
		if (np) {
			base = of_iomap(np, 0);
			if (!base) {
				pr_warn("PM: IOremap of %s failed\n", comp);
				return NULL;
			}
			of_node_put(np);
			pr_debug("PM: Map %s to %lx\n", comp,
							(unsigned long)base);
			return base;
		}
		pr_warn("PM: No compatible node found for '%s'\n", comp);
		return NULL;
	}

	if (base) {
		of_node_get(np);
		pr_debug("PM: Unmap %s at %lx\n", comp, (unsigned long)base);
		iounmap(base);
	}
	return NULL;
}

/**
 * zynq_pm_remap_ocm() - Remap OCM
 * @action	0: map, !0: unmap
 * @comp	DT compatible string
 * Returns a pointer to the mapped memory or NULL.
 *
 * Remap the OCM.
 */
static void __iomem *zynq_pm_remap_ocm(u32 action, const char *comp)
{
	struct device *dev;
	unsigned long pool_addr;
	static unsigned long pool_addr_virt;
	static struct gen_pool *pool;
	static struct device_node *np;
	static void __iomem *base;

	if (!action) {
		np = of_find_compatible_node(NULL, NULL, comp);
		if (!np) {
			pr_warn("PM: Compatible node not found, %s\n", comp);
			return NULL;
		}
		of_node_put(np);

		dev = &(of_find_device_by_node(np)->dev);

		/* Get OCM pool from device tree or platform data */
		pool = dev_get_gen_pool(dev);
		if (!pool) {
			pr_warn("PM: OCM pool is not available\n");
			return NULL;
		}

		pool_addr_virt = gen_pool_alloc(pool, zynq_sys_suspend_sz);
		if (!pool_addr_virt) {
			pr_warn("PM: Can't get OCM poll\n");
			return NULL;
		}
		pool_addr = gen_pool_virt_to_phys(pool, pool_addr_virt);
		if (!pool_addr) {
			pr_warn("PM: Can't get physical address of OCM pool\n");
			return NULL;
		}
		base = __arm_ioremap(pool_addr, zynq_sys_suspend_sz, MT_MEMORY);
		if (!base) {
			pr_warn("PM: IOremap OCM pool failed\n");
			return NULL;
		}
		pr_debug("PM: Remap OCM %s from %lx to %lx\n", comp,
					pool_addr_virt, (unsigned long)base);
		return base;
	}

	if (base) {
		of_node_get(np);
		pr_debug("PM: Unmap %s at %lx and %lx\n", comp,
					(unsigned long)base, pool_addr_virt);
		__arm_iounmap(base);
		gen_pool_free(pool, (unsigned long) pool_addr_virt,
							zynq_sys_suspend_sz);
	}

	return 0;
}

static int zynq_pm_suspend(unsigned long arg)
{
	u32 reg;
	void __iomem *ddrc_base;
	void __iomem *ocm_base;
	int (*zynq_suspend_ptr)(void __iomem *, void __iomem *) = NULL;

	ddrc_base = zynq_pm_ioremap(0, "xlnx,ps7-ddrc");
	if (!ddrc_base)
		pr_warn("PM: Unable to map DDRC IO memory.\n");

	ocm_base = zynq_pm_remap_ocm(0, "xlnx,ps7-ocm");
	if (!ocm_base)
		pr_warn("PM: Unable to map OCM.\n");


	/* Allocate some space for temporary OCM storage */
	if (ddrc_base) {
		/* Enable DDR self-refresh and clock stop */
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
	if (scu_base) {
		reg = readl(scu_base + SCU_CTRL);
		reg |= SCU_STBY_EN_MASK;
		writel(reg, scu_base + SCU_CTRL);
	}

	/* Topswitch clock stop disable */
	reg = xslcr_read(SLCR_TOPSW_CLK_CTRL);
	reg |= TOPSW_CLK_CTRL_DIS_MASK;
	xslcr_write(reg, SLCR_TOPSW_CLK_CTRL);

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
	}

	/*
	 * at this point PLLs are supposed to be bypassed:
	 *
	 * DDRPLL: Is bypassed without further sanity checking in the suspend
	 * routine which is called below and executed from OCM.
	 *
	 * IOPLL/ARMPLL: By now all clock consumers should have released their
	 * clock resulting in the PLLs to be bypassed. To account for timers and
	 * similar which run in the CPU clock domain we call a disable on the
	 * CPU clock's PLL to bypass it.
	 *
	 * A wake up device would prevent its source PLL from
	 * being bypassed, unless its the DDRPLL.
	 */
	if (!IS_ERR(cpupll))
		clk_disable(cpupll);

	/* Transfer to suspend code in OCM */
	if (ocm_base) {
		/*
		 * Going this way will turn off DDR related clocks and the DDR
		 * PLL. I.e. We might brake sub systems relying on any of this
		 * clocks. And even worse: If there are any other masters in the
		 * system (e.g. in the PL) accessing DDR they are screwed.
		 */
		flush_cache_all();
		if (zynq_suspend_ptr(ddrc_base, zynq_slcr_base))
			pr_warn("PM: DDR self refresh failed.\n");
	} else {
		WARN_ONCE(1, "PM: DRAM self-refresh not available\n");
		wfi();
	}

	if (!IS_ERR(cpupll))
		clk_enable(cpupll);

	/* Topswitch clock stop disable */
	reg = xslcr_read(SLCR_TOPSW_CLK_CTRL);
	reg &= ~TOPSW_CLK_CTRL_DIS_MASK;
	xslcr_write(reg, SLCR_TOPSW_CLK_CTRL);

	/* SCU standby mode */
	if (scu_base) {
		reg = readl(scu_base + SCU_CTRL);
		reg &= ~SCU_STBY_EN_MASK;
		writel(reg, scu_base + SCU_CTRL);
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
		zynq_pm_ioremap(1, NULL);
	}

	if (ocm_base)
		zynq_pm_remap_ocm(1, NULL);

	return 0;
}

static int zynq_pm_enter(suspend_state_t suspend_state)
{
	switch (suspend_state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		outer_flush_all();
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
	.enter		= zynq_pm_enter,
	.valid		= suspend_valid_only_mem,
};

int __init zynq_pm_late_init(void)
{
	cpupll = clk_get_sys("CPU_6OR4X_CLK", NULL);
	if (!IS_ERR(cpupll)) {
		cpupll = clk_get_parent(cpupll);
		if (!IS_ERR(cpupll))
			cpupll = clk_get_parent(cpupll);
	}
	if (IS_ERR(cpupll))
		pr_warn("PM: CPUPLL not found.\n");

	suspend_set_ops(&zynq_pm_ops);

	return 0;
}

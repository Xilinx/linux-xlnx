/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include "gdsc.h"

#define PWR_ON_MASK		BIT(31)
#define EN_REST_WAIT_MASK	GENMASK_ULL(23, 20)
#define EN_FEW_WAIT_MASK	GENMASK_ULL(19, 16)
#define CLK_DIS_WAIT_MASK	GENMASK_ULL(15, 12)
#define SW_OVERRIDE_MASK	BIT(2)
#define HW_CONTROL_MASK		BIT(1)
#define SW_COLLAPSE_MASK	BIT(0)

/* Wait 2^n CXO cycles between all states. Here, n=2 (4 cycles). */
#define EN_REST_WAIT_VAL	(0x2 << 20)
#define EN_FEW_WAIT_VAL		(0x8 << 16)
#define CLK_DIS_WAIT_VAL	(0x2 << 12)

#define RETAIN_MEM		BIT(14)
#define RETAIN_PERIPH		BIT(13)

#define TIMEOUT_US		100

#define domain_to_gdsc(domain) container_of(domain, struct gdsc, pd)

static int gdsc_is_enabled(struct gdsc *sc, unsigned int reg)
{
	u32 val;
	int ret;

	ret = regmap_read(sc->regmap, reg, &val);
	if (ret)
		return ret;

	return !!(val & PWR_ON_MASK);
}

static int gdsc_toggle_logic(struct gdsc *sc, bool en)
{
	int ret;
	u32 val = en ? 0 : SW_COLLAPSE_MASK;
	ktime_t start;
	unsigned int status_reg = sc->gdscr;

	ret = regmap_update_bits(sc->regmap, sc->gdscr, SW_COLLAPSE_MASK, val);
	if (ret)
		return ret;

	/* If disabling votable gdscs, don't poll on status */
	if ((sc->flags & VOTABLE) && !en) {
		/*
		 * Add a short delay here to ensure that an enable
		 * right after it was disabled does not put it in an
		 * unknown state
		 */
		udelay(TIMEOUT_US);
		return 0;
	}

	if (sc->gds_hw_ctrl) {
		status_reg = sc->gds_hw_ctrl;
		/*
		 * The gds hw controller asserts/de-asserts the status bit soon
		 * after it receives a power on/off request from a master.
		 * The controller then takes around 8 xo cycles to start its
		 * internal state machine and update the status bit. During
		 * this time, the status bit does not reflect the true status
		 * of the core.
		 * Add a delay of 1 us between writing to the SW_COLLAPSE bit
		 * and polling the status bit.
		 */
		udelay(1);
	}

	start = ktime_get();
	do {
		if (gdsc_is_enabled(sc, status_reg) == en)
			return 0;
	} while (ktime_us_delta(ktime_get(), start) < TIMEOUT_US);

	if (gdsc_is_enabled(sc, status_reg) == en)
		return 0;

	return -ETIMEDOUT;
}

static inline int gdsc_deassert_reset(struct gdsc *sc)
{
	int i;

	for (i = 0; i < sc->reset_count; i++)
		sc->rcdev->ops->deassert(sc->rcdev, sc->resets[i]);
	return 0;
}

static inline int gdsc_assert_reset(struct gdsc *sc)
{
	int i;

	for (i = 0; i < sc->reset_count; i++)
		sc->rcdev->ops->assert(sc->rcdev, sc->resets[i]);
	return 0;
}

static inline void gdsc_force_mem_on(struct gdsc *sc)
{
	int i;
	u32 mask = RETAIN_MEM | RETAIN_PERIPH;

	for (i = 0; i < sc->cxc_count; i++)
		regmap_update_bits(sc->regmap, sc->cxcs[i], mask, mask);
}

static inline void gdsc_clear_mem_on(struct gdsc *sc)
{
	int i;
	u32 mask = RETAIN_MEM | RETAIN_PERIPH;

	for (i = 0; i < sc->cxc_count; i++)
		regmap_update_bits(sc->regmap, sc->cxcs[i], mask, 0);
}

static int gdsc_enable(struct generic_pm_domain *domain)
{
	struct gdsc *sc = domain_to_gdsc(domain);
	int ret;

	if (sc->pwrsts == PWRSTS_ON)
		return gdsc_deassert_reset(sc);

	ret = gdsc_toggle_logic(sc, true);
	if (ret)
		return ret;

	if (sc->pwrsts & PWRSTS_OFF)
		gdsc_force_mem_on(sc);

	/*
	 * If clocks to this power domain were already on, they will take an
	 * additional 4 clock cycles to re-enable after the power domain is
	 * enabled. Delay to account for this. A delay is also needed to ensure
	 * clocks are not enabled within 400ns of enabling power to the
	 * memories.
	 */
	udelay(1);

	return 0;
}

static int gdsc_disable(struct generic_pm_domain *domain)
{
	struct gdsc *sc = domain_to_gdsc(domain);

	if (sc->pwrsts == PWRSTS_ON)
		return gdsc_assert_reset(sc);

	if (sc->pwrsts & PWRSTS_OFF)
		gdsc_clear_mem_on(sc);

	return gdsc_toggle_logic(sc, false);
}

static int gdsc_init(struct gdsc *sc)
{
	u32 mask, val;
	int on, ret;
	unsigned int reg;

	/*
	 * Disable HW trigger: collapse/restore occur based on registers writes.
	 * Disable SW override: Use hardware state-machine for sequencing.
	 * Configure wait time between states.
	 */
	mask = HW_CONTROL_MASK | SW_OVERRIDE_MASK |
	       EN_REST_WAIT_MASK | EN_FEW_WAIT_MASK | CLK_DIS_WAIT_MASK;
	val = EN_REST_WAIT_VAL | EN_FEW_WAIT_VAL | CLK_DIS_WAIT_VAL;
	ret = regmap_update_bits(sc->regmap, sc->gdscr, mask, val);
	if (ret)
		return ret;

	/* Force gdsc ON if only ON state is supported */
	if (sc->pwrsts == PWRSTS_ON) {
		ret = gdsc_toggle_logic(sc, true);
		if (ret)
			return ret;
	}

	reg = sc->gds_hw_ctrl ? sc->gds_hw_ctrl : sc->gdscr;
	on = gdsc_is_enabled(sc, reg);
	if (on < 0)
		return on;

	/*
	 * Votable GDSCs can be ON due to Vote from other masters.
	 * If a Votable GDSC is ON, make sure we have a Vote.
	 */
	if ((sc->flags & VOTABLE) && on)
		gdsc_enable(&sc->pd);

	if (on || (sc->pwrsts & PWRSTS_RET))
		gdsc_force_mem_on(sc);
	else
		gdsc_clear_mem_on(sc);

	sc->pd.power_off = gdsc_disable;
	sc->pd.power_on = gdsc_enable;
	pm_genpd_init(&sc->pd, NULL, !on);

	return 0;
}

int gdsc_register(struct gdsc_desc *desc,
		  struct reset_controller_dev *rcdev, struct regmap *regmap)
{
	int i, ret;
	struct genpd_onecell_data *data;
	struct device *dev = desc->dev;
	struct gdsc **scs = desc->scs;
	size_t num = desc->num;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->domains = devm_kcalloc(dev, num, sizeof(*data->domains),
				     GFP_KERNEL);
	if (!data->domains)
		return -ENOMEM;

	data->num_domains = num;
	for (i = 0; i < num; i++) {
		if (!scs[i])
			continue;
		scs[i]->regmap = regmap;
		scs[i]->rcdev = rcdev;
		ret = gdsc_init(scs[i]);
		if (ret)
			return ret;
		data->domains[i] = &scs[i]->pd;
	}

	/* Add subdomains */
	for (i = 0; i < num; i++) {
		if (!scs[i])
			continue;
		if (scs[i]->parent)
			pm_genpd_add_subdomain(scs[i]->parent, &scs[i]->pd);
	}

	return of_genpd_add_provider_onecell(dev->of_node, data);
}

void gdsc_unregister(struct gdsc_desc *desc)
{
	int i;
	struct device *dev = desc->dev;
	struct gdsc **scs = desc->scs;
	size_t num = desc->num;

	/* Remove subdomains */
	for (i = 0; i < num; i++) {
		if (!scs[i])
			continue;
		if (scs[i]->parent)
			pm_genpd_remove_subdomain(scs[i]->parent, &scs[i]->pd);
	}
	of_genpd_del_provider(dev->of_node);
}

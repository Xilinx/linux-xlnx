/*
 * ZynqMP Generic PM domain support
 *
 *  Copyright (C) 2015 Xilinx
 *
 *  Davorin Mista <davorin.mista@aggios.com>
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

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/soc/xilinx/zynqmp/pm.h>

#define DRIVER_NAME "zynqmp_gpd"

/**
 * struct zynqmp_pm_domain - Wrapper around struct generic_pm_domain
 * @gpd:		Generic power domain
 * @node_id:	PM node id of a device inside PM domain
 */
struct zynqmp_pm_domain {
	struct generic_pm_domain gpd;
	u32 node_id;
};

/**
 * zynqmp_gpd_set_power - power on/off PM domain
 * @domain:	Generic PM domain
 * @power_on:	Flag to specify whether to power on or off PM domain
 *
 * This functions calls zynqmp_pm_set_requirement to trigger power state change
 * of a resource (device inside PM domain), depending on power_on flag.
 *
 * Return:	0 on success, error code otherwise.
 */
static int zynqmp_gpd_set_power(struct generic_pm_domain *domain, bool power_on)
{
	int status;
	struct zynqmp_pm_domain *pd;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);
	if (pd->node_id == 0) {
		pr_err("%s: unknown node specified, powering %s domain %s\n",
			__func__, power_on ? "on" : "off", pd->gpd.name);
		return -EINVAL;
	}

	if (!power_on)
		status = zynqmp_pm_set_requirement(pd->node_id, 0, 0,
						ZYNQMP_PM_REQUEST_ACK_NO);
	else
		status = zynqmp_pm_set_requirement(pd->node_id,
						ZYNQMP_PM_CAPABILITY_ACCESS,
						ZYNQMP_PM_MAX_QOS,
						ZYNQMP_PM_REQUEST_ACK_NO);
	return status;
}

/**
 * zynqmp_gpd_power_on - Power on PM domain
 * @domain:	Generic PM domain
 *
 * This function is called before devices inside a PM domain are resumed, to
 * power on PM domain.
 *
 * Return:	0 on success, error code otherwise.
 */
static int zynqmp_gpd_power_on(struct generic_pm_domain *domain)
{
	return zynqmp_gpd_set_power(domain, true);
}

/**
 * zynqmp_gpd_power_off - Power off PM domain
 * @domain:	Generic PM domain
 *
 * This function is called after devices inside a PM domain are suspended, to
 * power off PM domain.
 *
 * Return:	0 on success, error code otherwise.
 */
static int zynqmp_gpd_power_off(struct generic_pm_domain *domain)
{
	return zynqmp_gpd_set_power(domain, false);
}

/**
 * zynqmp_gpd_probe - Initialize ZynqMP specific PM domains
 * @pdev:	Platform device pointer
 *
 * Description:	This function populates struct zynqmp_pm_domain for each PM
 * domain and initalizes generic PM domain. If the "pd-id" DT property
 * of a certain domain is missing or invalid, that domain will be skipped.
 *
 * Return:	0 on success, error code otherwise.
 */
static int __init zynqmp_gpd_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *child_err, *child, *np = pdev->dev.of_node;

	for_each_child_of_node(np, child) {
		u32 node_id;
		struct zynqmp_pm_domain *pd;

		pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			ret = -ENOMEM;
			goto err_cleanup;
		}

		ret = of_property_read_u32(child, "pd-id", &node_id);
		if (ret)
			goto err_cleanup;

		pd->node_id = node_id;
		pd->gpd.name = kstrdup(child->name, GFP_KERNEL);
		pd->gpd.power_off = zynqmp_gpd_power_off;
		pd->gpd.power_on = zynqmp_gpd_power_on;

		ret = of_genpd_add_provider_simple(child, &pd->gpd);
		if (ret)
			goto err_cleanup;

		pm_genpd_init(&pd->gpd, NULL, false);
	}

	return 0;

err_cleanup:
	child_err = child;
	for_each_child_of_node(np, child) {
		if (child == child_err)
			break;
		of_genpd_del_provider(child);
	}

	return ret;
}

static const struct of_device_id zynqmp_gpd_of_match[] = {
	{ .compatible = "xlnx,zynqmp-genpd" },
	{},
};

MODULE_DEVICE_TABLE(of, zynqmp_gpd_of_match);

static struct platform_driver zynqmp_gpd_platform_driver = {
	.driver	= {
		.name = DRIVER_NAME,
		.of_match_table = zynqmp_gpd_of_match,
	},
};

static __init int zynqmp_gpd_init(void)
{
	return platform_driver_probe(&zynqmp_gpd_platform_driver,
				     zynqmp_gpd_probe);
}
subsys_initcall(zynqmp_gpd_init);

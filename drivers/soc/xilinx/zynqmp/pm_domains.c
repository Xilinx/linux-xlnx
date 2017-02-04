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

/* Flag stating if PM nodes mapped to the PM domain has been requested */
#define ZYNQMP_PM_DOMAIN_REQUESTED	BIT(0)

/**
 * struct zynqmp_pm_domain - Wrapper around struct generic_pm_domain
 * @gpd:		Generic power domain
 * @node_ids:		PM node IDs corresponding to device(s) inside PM domain
 * @node_id_num:	Number of PM node IDs
 * @flags:		ZynqMP PM domain flags
 */
struct zynqmp_pm_domain {
	struct generic_pm_domain gpd;
	u32 *node_ids;
	int node_id_num;
	u8 flags;
};

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
	int i, status = 0;
	struct zynqmp_pm_domain *pd;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);
	for (i = 0; i < pd->node_id_num; i++) {
		status = zynqmp_pm_set_requirement(pd->node_ids[i],
					ZYNQMP_PM_CAPABILITY_ACCESS,
					ZYNQMP_PM_MAX_QOS,
					ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		if (status)
			break;
	}
	return status;
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
	int i, status = 0;
	struct zynqmp_pm_domain *pd;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);

	/* If domain is already released there is nothing to be done */
	if (!(pd->flags & ZYNQMP_PM_DOMAIN_REQUESTED))
		return 0;

	for (i = pd->node_id_num - 1; i >= 0; i--) {
		status = zynqmp_pm_set_requirement(pd->node_ids[i], 0, 0,
						ZYNQMP_PM_REQUEST_ACK_NO);
		/**
		 * If powering down of any node inside this domain fails,
		 * report and return the error
		 */
		if (status) {
			pr_err("%s error %d, node %u\n", __func__, status,
				pd->node_ids[i]);
			return status;
		}
	}

	return status;
}

/**
 * zynqmp_gpd_attach_dev - Attach device to the PM domain
 * @domain:	Generic PM domain
 * @dev:	Device to attach
 *
 * Return:	0 on success, error code otherwise.
 */
static int zynqmp_gpd_attach_dev(struct generic_pm_domain *domain,
				struct device *dev)
{
	int i, status;
	struct zynqmp_pm_domain *pd;

	/* If this is not the first device to attach there is nothing to do */
	if (domain->device_count)
		return 0;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);
	for (i = 0; i < pd->node_id_num; i++) {
		status = zynqmp_pm_request_node(pd->node_ids[i], 0, 0,
						ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		/* If requesting a node fails print and return the error */
		if (status) {
			pr_err("%s error %d, node %u\n", __func__, status,
				pd->node_ids[i]);
			return status;
		}
	}

	pd->flags |= ZYNQMP_PM_DOMAIN_REQUESTED;

	return 0;
}

/**
 * zynqmp_gpd_detach_dev - Detach device from the PM domain
 * @domain:	Generic PM domain
 * @dev:	Device to detach
 */
static void zynqmp_gpd_detach_dev(struct generic_pm_domain *domain,
				struct device *dev)
{
	int i, status;
	struct zynqmp_pm_domain *pd;

	/* If this is not the last device to detach there is nothing to do */
	if (domain->device_count)
		return;

	pd = container_of(domain, struct zynqmp_pm_domain, gpd);
	for (i = 0; i < pd->node_id_num; i++) {
		status = zynqmp_pm_release_node(pd->node_ids[i]);
		/* If releasing a node fails print the error and return */
		if (status) {
			pr_err("%s error %d, node %u\n", __func__, status,
				pd->node_ids[i]);
			return;
		}
	}

	pd->flags &= ~ZYNQMP_PM_DOMAIN_REQUESTED;
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
		struct zynqmp_pm_domain *pd;

		pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			ret = -ENOMEM;
			goto err_cleanup;
		}

		ret = of_property_count_u32_elems(child, "pd-id");
		if (ret <= 0)
			goto err_cleanup;

		pd->node_id_num = ret;
		pd->node_ids = devm_kcalloc(&pdev->dev, ret,
					sizeof(*pd->node_ids), GFP_KERNEL);
		if (!pd->node_ids) {
			ret = -ENOMEM;
			goto err_cleanup;
		}

		ret = of_property_read_u32_array(child, "pd-id", pd->node_ids,
							pd->node_id_num);
		if (ret)
			goto err_cleanup;

		pd->gpd.name = kstrdup(child->name, GFP_KERNEL);
		pd->gpd.power_off = zynqmp_gpd_power_off;
		pd->gpd.power_on = zynqmp_gpd_power_on;
		pd->gpd.attach_dev = zynqmp_gpd_attach_dev;
		pd->gpd.detach_dev = zynqmp_gpd_detach_dev;

		/* Mark all PM domains as initially powered off */
		pm_genpd_init(&pd->gpd, NULL, true);

		ret = of_genpd_add_provider_simple(child, &pd->gpd);
		if (ret)
			goto err_cleanup;
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

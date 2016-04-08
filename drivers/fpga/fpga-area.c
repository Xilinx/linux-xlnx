/*
 * FPGA Tree Area Support for Device Tree controlled FPGA reprogramming
 *
 * Copyright (C) 2013-2015 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fpga/fpga-bridge.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>

/*
 * In the case of a FPGA doing full reconfiguration, the area == the whole
 * FPGA. In the case of partial reconfiguration, several areas can be
 * reconfigured separately.
 */

/**
 * struct fpga_area
 * @mgr:	FPGA Manager
 * @flags:	Flags for reconfiguration
 * @firmware_name: Name of FPGA image file
 * @bridge_list: Linked list of FPGA bridges controlled by area
 * @br:	FPGA Bridge corresponding to area
 * @bus_np:	device node of ancestor FPGA Bus
 */
struct fpga_area {
	struct fpga_manager *mgr;
	u32 flags;
	const char *firmware_name;
	struct list_head bridge_list;
	struct fpga_bridge *br;
	struct device_node *bus_np;
};

/**
 * fpga_area_get_parent_peer_bridges - get bridges that are peers of parent
 * @area: FPGA Area struct
 *
 * Intended to support case where multiple bridges need to be disabled
 * during FPGA reprogramming.
 *
 * Finds the FPGA bridge that is the parent of @area in the device tree.
 * Creates a linked list of FPGA bridges that includes the parent bridge and
 * its peers. Gets an exclusive reference to each of these bridges as they
 * are added to the list. The list of bridges is saved in @area's
 * fpga_bridge struct.
 *
 * These bridges must be disabled while the FPGA is being reprogrammed to
 * support the children of the @area bridge and enabled after FPGA
 * programming is finished.
 *
 * For the use case where no FPGA bridges are required, the parent node should
 * be a FPGA Manager. In this case, the bridge list will end up empty.
 *
 * Returns error code or 0 for success. Returns 0 if the parent is a FPGA
 * manager.
 */
static int fpga_area_get_parent_peer_bridges(struct fpga_area *area)
{
	struct device_node *parent, *child;
	struct fpga_bridge *bridge;
	int ret;

	/* Create a list of bridges that are peers of area's parent */
	parent = of_get_parent(area->br->dev.of_node);
	parent = of_get_next_parent(parent);

	for_each_child_of_node(parent, child) {
	/* If node is a bridge, get it and add to list */
	ret = fpga_bridge_get_to_list(child, &area->bridge_list);

	/* if any of the bridges are in use, give up */
	if (ret == -EBUSY) {
	fpga_bridges_put(&area->bridge_list);
	of_node_put(parent);
	return PTR_ERR(bridge);
	}
	}
	of_node_put(parent);

	return 0;
}

/**
 * fpga_area_get_bridges - create list of exclusive references to fpga bridges
 * @area: FPGA Area struct
 *
 * Get exclusive references to a FPGA bridge or bridges.
 * In the case of full reconfiguration build a list of bridges that are the
 * parent of @area and its peers. We are reprogramming the full FPGA and
 * need to have no communication on the processor/FPGA bridges while that
 * is happening
 * In the case of partial reconfiguration, only add the parent of @area to
 * the list. This one bridge is a freeze block which is in the FPGA itself
 * and is downstream from its parent bridge and the parent's peers.
 *
 * Return 0 for success. Return -ENODEV is there are no bridges.
 * Pass other error codes ultimately from of_fpga_bridge_get() such as:
 * Return -EBUSY if any of the bridges were already gotten.
 */
static int fpga_area_get_bridges(struct fpga_area *area)
{
	struct device_node *parent;
	int ret = 0;

	/* If parent is FPGA Manager, no bridges to get */
	parent = of_get_parent(area->br->dev.of_node);
	if (parent == area->mgr->dev.of_node) {
	of_node_put(parent);
	return -ENODEV;
	}

	if (area->flags & FPGA_MGR_PARTIAL_RECONFIG)
	ret = fpga_bridge_get_to_list(parent, &area->bridge_list);
	else
	ret = fpga_area_get_parent_peer_bridges(area);

	of_node_put(parent);

	return ret;
}

/**
 * fpga_area_load - program the FPGA based on info in area
 * @area: FPGA Area struct
 *
 * Program the FPGA that the area has a reference to.
 *
 * Returns 0 for success or error codes passed down from
 * fpga_mgr_firmware_load()
 */
static int fpga_area_load(struct fpga_area *area)
{
	return fpga_mgr_firmware_load(area->mgr,
	area->flags,
	area->firmware_name);
}

/**
 * fpga_area_get_bus - find ancestor FPGA Bus, get a reference
 * @area: FPGA Area struct
 *
 * Returns 0 for success or -ENODEV if not a child of a FPGA Bus.
 */
static int fpga_area_get_bus(struct fpga_area *area)
{
	struct device_node *np = area->br->dev.of_node;

	of_node_get(np);

	while (np && !of_device_is_compatible(np, "altr,fpga-bus"))
	np = of_get_next_parent(np);

	if (!np)
	return -ENODEV;

	area->bus_np = np;

	return 0;
}

/**
 * fpga_area_put_bus - put FPGA Bus saved in @area
 * @area: FPGA Area struct
 */
static void fpga_area_put_bus(struct fpga_area *area)
{
	of_node_put(area->bus_np);
	area->bus_np = NULL;
}

/**
 * fpga_area_get_manager - get exclusive reference for FPGA Manager
 * @area: FPGA Area struct
 *
 * One of the ancestor nodes of the FPGA Area should be a FPGA Bus. One of
 * the children of that FPGA Bus should be a FPGA Manager.
 * Assuming that fpga_area_get_bus() has already found the bus, this function
 * finds the FPGA Manager and saves it in the area struct.
 *
 * Return: 0 for success or IS_ERR() condition containing error code.
 */
static int fpga_area_get_manager(struct fpga_area *area)
{
	struct device_node *child;
	struct fpga_manager *mgr;

	for_each_child_of_node(area->bus_np, child) {
	mgr = of_fpga_mgr_get(child);
	if (IS_ERR(mgr))
	continue;

	area->mgr = mgr;
	return 0;
	}

	return -ENODEV;
}

/**
 * fpga_area_put_manager - put exclusive reference to FPGA Manager
 * @area: FPGA Area struct
 */
static void fpga_area_put_manager(struct fpga_area *area)
{
	fpga_mgr_put(area->mgr);
	area->mgr = NULL;
}

/**
 * fpga_area_probe - probe function for FPGA area
 * @pdev: platform device
 *
 * If there is an image to program to a FPGA, get the FPGA Manager and bridges,
 * reprogram the FPGA, and populate the child devices.
 *
 * If there are FPGA Bridges, this function will hold the references to the
 * bridges; they are released in fpga_area_remove().
 *
 * Return: 0 for success, -EBUSY if someone already got the bridges or manager.
 */
static int fpga_area_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct fpga_area *area;
	int ret;

	area = devm_kzalloc(dev, sizeof(*area), GFP_KERNEL);
	if (!area)
	return -ENOMEM;

	INIT_LIST_HEAD(&area->bridge_list);

	ret = fpga_bridge_register(dev, "FPGA Area", NULL, area);
	if (ret)
	return ret;
	area->br = dev_get_drvdata(dev);

	if (of_property_read_string(np, "firmware-name",
	&area->firmware_name)) {
	of_platform_populate(np, of_default_bus_match_table, NULL, dev);
	return 0;
	}

	if (of_property_read_bool(np, "partial-reconfig"))
	area->flags |= FPGA_MGR_PARTIAL_RECONFIG;

	ret = fpga_area_get_bus(area);
	if (ret) {
	dev_dbg(dev, "Should be child of a FPGA Bus");
	goto err_unreg;
	}

	ret = fpga_area_get_manager(area);
	if (ret) {
	dev_dbg(dev, "Could not find FPGA Manager");
	goto err_unreg;
	}

	/* Give up if there is an error other than no bridges. */
	ret = fpga_area_get_bridges(area);
	if (ret && ret != -ENODEV)
	goto err_release;

	ret = fpga_bridges_disable(&area->bridge_list);
	if (ret)
	goto err_release;

	ret = fpga_area_load(area);
	if (ret)
	goto err_release;

	ret = fpga_bridges_enable(&area->bridge_list);
	if (ret)
	goto err_release;

	/* If successful, put the mgr, but keep the bridges */
	fpga_area_put_manager(area);

	of_platform_populate(np, of_default_bus_match_table, NULL, dev);

	return 0;

err_release:
	fpga_bridges_put(&area->bridge_list);
	fpga_area_put_manager(area);
err_unreg:
	fpga_bridge_unregister(dev);

	return ret;
}

/**
 * fpga_area_remove - remove a FPGA area
 * @pdev: platform device
 *
 * Called when an FPGA Area is removed. If there are any FPGA Bridges in the
 * area's bridge list, disable them and put them.
 *
 * Return: 0
 */
static int fpga_area_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fpga_bridge *bridge = dev_get_drvdata(dev);
	struct fpga_area *area = bridge->priv;

	fpga_area_put_bus(area);

	fpga_bridges_disable(&area->bridge_list);
	fpga_bridges_put(&area->bridge_list);

	fpga_bridge_unregister(dev);

	return 0;
}

static const struct of_device_id fpga_area_of_match[] = {
	{ .compatible = "fpga-area", },
	{},
};
MODULE_DEVICE_TABLE(of, fpga_area_of_match);

static struct platform_driver fpga_area_driver = {
	.probe = fpga_area_probe,
	.remove = fpga_area_remove,
	.driver = {
	.name	= "FPGA Area",
	.of_match_table = of_match_ptr(fpga_area_of_match),
	},
};

module_platform_driver(fpga_area_driver);

MODULE_DESCRIPTION("Altera FPGA Bus");
MODULE_AUTHOR("Alan Tull <atull@xxxxxxxxxxxxxxxxxxxxx>");
MODULE_LICENSE("GPL v2");

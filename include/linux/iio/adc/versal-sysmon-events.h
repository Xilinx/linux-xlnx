/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx SYSMON hardware info
 *
 * Copyright (C) 2019 - 2021 Xilinx, Inc.
 *
 */
#ifndef _VERSAL_SYSMON_H_
#define _VERSAL_SYSMON_H_

#include <linux/types.h>
#include <linux/iio/types.h>

/* Sysmon region ids */
enum sysmon_region {
	SYSMON_AIE = 0,
	SYSMON_PMC = 1,
	SYSMON_XPIO = 2,
	SYSMON_VNOC = 3,
	SYSMON_CC = 4,
};

/**
 * struct regional_node - regional node properties
 * @sat_id: node_id
 * @x: x co-ordinate of the node
 * @y: y co-ordinate of the node
 * @temp: raw sensor value
 * @regional_node_list: list of nodes in the region
 */
struct regional_node {
	int sat_id;
	int x;
	int y;
	u16 temp;
	struct list_head regional_node_list;
};

/**
 * struct region_info - information about a regions sensors
 * @id: region id
 * @cb: callback to be called when there is a region specific event
 * @data: pointer to the callback data
 * @node_list: head to the regional_nodes list
 * @list: list of regions
 */
struct region_info {
	enum sysmon_region id;
	void (*cb)(void *data, struct regional_node *node);
	void *data;
	struct list_head node_list;
	struct list_head list;
};

#endif

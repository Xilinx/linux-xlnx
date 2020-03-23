// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM bridge header
 *
 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _XLNX_BRIDGE_H_
#define _XLNX_BRIDGE_H_

struct videomode;

struct xlnx_bridge_debugfs_file;

/**
 * struct xlnx_bridge - Xilinx bridge device
 * @list: list node for Xilinx bridge device list
 * @of_node: OF node for the bridge
 * @owned: flag if the bridge is owned
 * @enable: callback to enable the bridge
 * @disable: callback to disable the bridge
 * @set_input: callback to set the input
 * @get_input_fmts: callback to get supported input formats.
 * @set_output: callback to set the output
 * @get_output_fmts: callback to get supported output formats.
 * @set_timing: callback to set timing in connected video timing controller.
 * @debugfs_file: for debugfs support
 */
struct xlnx_bridge {
	struct list_head list;
	struct device_node *of_node;
	bool owned;
	int (*enable)(struct xlnx_bridge *bridge);
	void (*disable)(struct xlnx_bridge *bridge);
	int (*set_input)(struct xlnx_bridge *bridge,
			 u32 width, u32 height, u32 bus_fmt);
	int (*get_input_fmts)(struct xlnx_bridge *bridge,
			      const u32 **fmts, u32 *count);
	int (*set_output)(struct xlnx_bridge *bridge,
			  u32 width, u32 height, u32 bus_fmt);
	int (*get_output_fmts)(struct xlnx_bridge *bridge,
			       const u32 **fmts, u32 *count);
	int (*set_timing)(struct xlnx_bridge *bridge, struct videomode *vm);
	struct xlnx_bridge_debugfs_file *debugfs_file;
};

#if IS_ENABLED(CONFIG_DRM_XLNX_BRIDGE)
/*
 * Helper functions: used within Xlnx DRM
 */

struct xlnx_bridge_helper;

int xlnx_bridge_helper_init(void);
void xlnx_bridge_helper_fini(void);

/*
 * Helper functions: used by client driver
 */

int xlnx_bridge_enable(struct xlnx_bridge *bridge);
void xlnx_bridge_disable(struct xlnx_bridge *bridge);
int xlnx_bridge_set_input(struct xlnx_bridge *bridge,
			  u32 width, u32 height, u32 bus_fmt);
int xlnx_bridge_get_input_fmts(struct xlnx_bridge *bridge,
			       const u32 **fmts, u32 *count);
int xlnx_bridge_set_output(struct xlnx_bridge *bridge,
			   u32 width, u32 height, u32 bus_fmt);
int xlnx_bridge_get_output_fmts(struct xlnx_bridge *bridge,
				const u32 **fmts, u32 *count);
int xlnx_bridge_set_timing(struct xlnx_bridge *bridge, struct videomode *vm);
struct xlnx_bridge *of_xlnx_bridge_get(struct device_node *bridge_np);
void of_xlnx_bridge_put(struct xlnx_bridge *bridge);

/*
 * Bridge registration: used by bridge driver
 */

int xlnx_bridge_register(struct xlnx_bridge *bridge);
void xlnx_bridge_unregister(struct xlnx_bridge *bridge);

#else /* CONFIG_DRM_XLNX_BRIDGE */

struct xlnx_bridge_helper;

static inline inline int xlnx_bridge_helper_init(void)
{
	return 0;
}

static inline void xlnx_bridge_helper_fini(void)
{
}

static inline int xlnx_bridge_enable(struct xlnx_bridge *bridge)
{
	if (bridge)
		return -ENODEV;
	return 0;
}

static inline void xlnx_bridge_disable(struct xlnx_bridge *bridge)
{
}

static inline int xlnx_bridge_set_input(struct xlnx_bridge *bridge,
					u32 width, u32 height, u32 bus_fmt)
{
	if (bridge)
		return -ENODEV;
	return 0;
}

static inline int xlnx_bridge_get_input_fmts(struct xlnx_bridge *bridge,
					     const u32 **fmts, u32 *count)
{
	if (bridge)
		return -ENODEV;
	return 0;
}

static inline int xlnx_bridge_set_output(struct xlnx_bridge *bridge,
					 u32 width, u32 height, u32 bus_fmt)
{
	if (bridge)
		return -ENODEV;
	return 0;
}

static inline int xlnx_bridge_get_output_fmts(struct xlnx_bridge *bridge,
					      const u32 **fmts, u32 *count)
{
	if (bridge)
		return -ENODEV;
	return 0;
}

static inline int xlnx_bridge_set_timing(struct xlnx_bridge *bridge,
					 struct videomode *vm)
{
	if (bridge)
		return -ENODEV;
	return 0;
}

static inline struct xlnx_bridge *
of_xlnx_bridge_get(struct device_node *bridge_np)
{
	return NULL;
}

static inline void of_xlnx_bridge_put(struct xlnx_bridge *bridge)
{
}

static inline int xlnx_bridge_register(struct xlnx_bridge *bridge)
{
	return 0;
}

static inline void xlnx_bridge_unregister(struct xlnx_bridge *bridge)
{
}

#endif /* CONFIG_DRM_XLNX_BRIDGE */

#endif /* _XLNX_BRIDGE_H_ */

// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI engine stream switch implementation
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc
 */

#include <linux/errno.h>

#include "ai-engine-internal.h"

#define AIE_PORT_OFFSET	4U
#define AIE_MUX_PL	0U
#define AIE_MUX_NOC	1U
#define AIE_DEMUX_PL	0U
#define AIE_DEMUX_NOC	1U

/**
 * aie_part_strmsw_get_slv_id() - gets slave id given port numbert and type
 * @strmsw: AI Engine stream switch attribute
 * @slv: stream switch slave port type
 * @slv_port_num: stream switch slave port number
 * @return: 0 for success, -EINVAL for failure
 */
static int aie_part_strmsw_get_slv_id(const struct aie_strmsw_attr *strmsw,
				      enum aie_strmsw_port_type slv,
				      u8 slv_port_num)
{
	const struct aie_strmsw_port_attr *port;
	u32 port_addr;

	port = &strmsw->slv_ports[slv];
	if (slv_port_num >= port->num_ports)
		return -EINVAL;

	port_addr = port->port_regoff + slv_port_num * AIE_PORT_OFFSET;
	return (port_addr - strmsw->slv_config_base) / 4U;
}

/**
 * aie_part_strmsw_mstr_config() - sets master port configuration
 * @apart: AI Engine partition instance
 * @strmsw: AI Engine stream switch attribute
 * @loc: AI Engine tile location
 * @mstr: stream switch master port type
 * @mstr_port_num: stream switch master port number
 * @slv_id: stream switch slave port ID
 * @return: 0 for success, negative value for failure
 */
static int aie_part_strmsw_mstr_config(struct aie_partition *apart,
				       const struct aie_strmsw_attr *strmsw,
				       struct aie_location *loc,
				       enum aie_strmsw_port_type mstr,
				       u8 mstr_port_num, u8 slv_id)
{
	struct aie_aperture *aperture = apart->aperture;
	const struct aie_strmsw_port_attr *port;
	void __iomem *va;
	u32 val;

	port = &strmsw->mstr_ports[mstr];
	if (port->num_ports == 0 || mstr_port_num >= port->num_ports) {
		dev_err(&apart->dev, "Invalid port number");
		return -EINVAL;
	}

	va = aperture->base +
		aie_cal_regoff(apart->adev, *loc, port->port_regoff +
			       mstr_port_num * AIE_PORT_OFFSET);

	val = aie_get_field_val(&strmsw->mstr_en, 1) |
		aie_get_field_val(&strmsw->config, slv_id);
	writel(val, va);

	return 0;
}

/**
 * aie_part_strmsw_slv_config() - sets slave port configuration
 * @apart: AI Engine partition instance
 * @strmsw: AI Engine stream switch attribute
 * @loc: AI Engine tile location
 * @slv: stream switch slave port type
 * @slv_port_num: stream switch slave port number
 * @return: 0 for success, negative value for failure
 */
static int aie_part_strmsw_slv_config(struct aie_partition *apart,
				      const struct aie_strmsw_attr *strmsw,
				      struct aie_location *loc,
				      enum aie_strmsw_port_type slv,
				      u8 slv_port_num)
{
	struct aie_aperture *aperture = apart->aperture;
	const struct aie_strmsw_port_attr *port;
	void __iomem *va;
	u32 val;

	port = &strmsw->slv_ports[slv];
	if (port->num_ports == 0 || slv_port_num >= port->num_ports) {
		dev_err(&apart->dev, "Invalid port number");
		return -EINVAL;
	}

	va = aperture->base +
		aie_cal_regoff(apart->adev, *loc, port->port_regoff +
			       slv_port_num * AIE_PORT_OFFSET);

	val = aie_get_field_val(&strmsw->slv_en, 1);
	writel(val, va);

	return 0;
}

/**
 * aie_part_set_strmsw_cct() - configures and enables a circuit switch connection
 *			       between given between given slave and master port.
 * @apart: AI Engine partition
 * @loc: AI Engine tile location
 * @slv: stream switch slave port type
 * @slv_port_num: stream switch slave port number
 * @mstr: stream switch master port type
 * @mstr_port_num: stream switch master port number
 * @return: 0 for success, negative value for failure.
 */
int aie_part_set_strmsw_cct(struct aie_partition *apart, struct aie_location *loc,
			    enum aie_strmsw_port_type slv, u8 slv_port_num,
			    enum aie_strmsw_port_type mstr, u8 mstr_port_num)
{
	struct aie_device *adev = apart->adev;
	const struct aie_strmsw_attr *strmsw;
	int slv_id, ret = 0;
	u8 ttype;

	if (adev->dev_gen != AIE_DEVICE_GEN_AIE2PS) {
		dev_err(&adev->dev,
			"failed to set stream switch, device not supported");
		return -EINVAL;
	}

	if (slv >= AIE_STRMSW_MAX || mstr >= AIE_STRMSW_MAX) {
		dev_err(&adev->dev,
			"failed to set stream switch, invalid stream switch port type");
		return -EINVAL;
	}

	ttype = adev->ops->get_tile_type(adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		strmsw = adev->tile_strmsw;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		strmsw = adev->memory_strmsw;
	else
		strmsw = adev->shim_strmsw;

	ret = adev->ops->strmsw_port_verify(ttype, slv, slv_port_num, mstr,
					    mstr_port_num);
	if (ret < 0) {
		dev_err(&apart->dev,
			"failed to set stream switch, ports cannot be connected");
		return ret;
	}

	slv_id = aie_part_strmsw_get_slv_id(strmsw, slv, slv_port_num);
	if (slv_id < 0) {
		dev_err(&apart->dev,
			"failed to set stream switch, ports slave port number");
		return -EINVAL;
	}

	ret = aie_part_strmsw_mstr_config(apart, strmsw, loc, mstr,
					  mstr_port_num, slv_id);
	if (ret < 0) {
		dev_err(&apart->dev,
			"failed to set stream switch master port configuration");
		return ret;
	}

	ret = aie_part_strmsw_slv_config(apart, strmsw, loc, slv, slv_port_num);
	if (ret < 0) {
		dev_err(&apart->dev,
			"failed to set stream switch slave port configuration");
		return -EINVAL;
	}

	return ret;
}

/**
 * aie_part_enable_noc_to_aie() - configures the mux to enable an input stream
 *				  from NoC for given location
 * @apart: AI Engine partition
 * @loc: AI Engine tile location
 * @port_num: stream switch port number to enable input
 * @return: 0 for success, -EINVAL for failure
 */
int aie_part_enable_noc_to_aie(struct aie_partition *apart,
			       struct aie_location *loc, u8 port_num)
{
	struct aie_device *adev = apart->adev;
	const struct aie_strmsw_attr *strmsw;
	void __iomem *va;
	u32 regval;
	u8 ttype;

	if (adev->dev_gen != AIE_DEVICE_GEN_AIE2PS) {
		dev_err(&adev->dev,
			"failed to configure input stream mux, device not supported");
		return -EINVAL;
	}

	ttype = adev->ops->get_tile_type(adev, loc);
	if (ttype != AIE_TILE_TYPE_SHIMNOC) {
		dev_err(&apart->dev, "invalid tile type");
		return -EINVAL;
	}
	strmsw = adev->shim_strmsw;

	if (port_num != 3 && port_num != 7) {
		dev_err(&apart->dev, "invalid port number");
		return -EINVAL;
	}

	regval = aie_get_field_val(&strmsw->mux_ports[port_num], AIE_MUX_NOC);
	va = apart->aperture->base +
		aie_cal_regoff(adev, *loc, strmsw->mux_ports[port_num].regoff);

	writel(regval, va);
	return 0;
}

/**
 * aie_part_enable_aie_to_noc() - configures the demux to enable an output stream
 *				  to NoC for given location
 * @apart: AI Engine partition
 * @loc: AI Engine tile location
 * @port_num: stream switch port number to enable output
 * @return: 0 for success, -EINVAL for failure
 */
int aie_part_enable_aie_to_noc(struct aie_partition *apart,
			       struct aie_location *loc, u8 port_num)
{
	struct aie_device *adev = apart->adev;
	const struct aie_strmsw_attr *strmsw;
	void __iomem *va;
	u32 regval;
	u8 ttype;

	if (adev->dev_gen != AIE_DEVICE_GEN_AIE2PS) {
		dev_err(&adev->dev,
			"failed to configure output stream demux, device not supported");
		return -EINVAL;
	}

	ttype = adev->ops->get_tile_type(adev, loc);
	if (ttype != AIE_TILE_TYPE_SHIMNOC) {
		dev_err(&apart->dev, "invalid tile type");
		return -EINVAL;
	}
	strmsw = adev->shim_strmsw;

	if (port_num != 1 && port_num != 3) {
		dev_err(&apart->dev, "invalid port number");
		return -EINVAL;
	}

	regval = aie_get_field_val(&strmsw->demux_ports[port_num], AIE_DEMUX_NOC);
	va = apart->aperture->base +
		aie_cal_regoff(adev, *loc, strmsw->demux_ports[port_num].regoff);

	writel(regval, va);
	return 0;
}

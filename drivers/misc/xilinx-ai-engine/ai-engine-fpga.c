// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver FPGA region implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include "ai-engine-internal.h"

static int aie_fpga_bridge_enable_set(struct fpga_bridge *bridge, bool enable)
{
	struct aie_partition *apart = bridge->priv;
	int ret;

	/*
	 * TBD:
	 * "Enable" should enable the SHIM tile configuration.
	 * "Disable" should should disable SHIM DMAs, and wait
	 * until SHIM DMA stops, and disable SHIM
	 * to PL streams within partition.
	 */
	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	if (enable)
		apart->status |= XAIE_PART_STATUS_BRIDGE_ENABLED;
	else
		apart->status &= ~XAIE_PART_STATUS_BRIDGE_ENABLED;
	mutex_unlock(&apart->mlock);
	return 0;
}

static int aie_fpga_bridge_enable_show(struct fpga_bridge *bridge)
{
	struct aie_partition *apart = bridge->priv;
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	if (apart->status & XAIE_PART_STATUS_BRIDGE_ENABLED)
		ret = 1;
	else
		ret = 0;
	mutex_unlock(&apart->mlock);
	return ret;
}

static const struct fpga_bridge_ops aie_fpga_bridge_ops = {
	.enable_set = aie_fpga_bridge_enable_set,
	.enable_show = aie_fpga_bridge_enable_show,
};

/**
 * aie_fpga_create_bridge() - Create FPGA bridge for AI engine partition
 * @apart: AI engine partition
 * @return: 0 for success, negative value for failure
 *
 * This function will create FPGA bridge for AI engine partition.
 * FPGA bridge is the presentation of SHIM row of the AI engine partition.
 * FPGA bridge connects AI engine partition with other FPGA regions.
 */
int aie_fpga_create_bridge(struct aie_partition *apart)
{
	struct fpga_bridge *br;
	int ret;

	snprintf(apart->br.name, sizeof(apart->br.name) - 1,
		 "xlnx-aie-bridge-%u-%u", apart->range.start.col, 0);
	br = devm_fpga_bridge_create(&apart->dev, apart->br.name,
				     &aie_fpga_bridge_ops, apart);
	if (!br)
		return -ENOMEM;
	ret = fpga_bridge_register(br);
	if (ret) {
		dev_err(&apart->dev, "Failed to register bridge.\n");
		return ret;
	}
	apart->br.br = br;
	return 0;
}

/**
 * aie_fpga_free_bridge() - Free AI engine partition FPGA bridge
 * @apart: AI engine partition
 *
 * This function will free the FPGA bridge for AI engine partition.
 */
void aie_fpga_free_bridge(struct aie_partition *apart)
{
	if (!WARN_ON(!apart->br.br))
		fpga_bridge_unregister(apart->br.br);
}

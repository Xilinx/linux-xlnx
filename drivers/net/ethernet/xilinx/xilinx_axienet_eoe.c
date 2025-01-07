// SPDX-License-Identifier: GPL-2.0

/* Xilinx AXI EOE (EOE programming)
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * This file contains probe function for EOE TX and RX programming.
 */

#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "xilinx_axienet_eoe.h"

/**
 * axienet_eoe_probe - Axi EOE probe function
 * @pdev:       Pointer to platform device structure.
 *
 * Return: 0, on success
 *         Non-zero error value on failure.
 *
 * This is the probe routine for Ethernet Offload Engine and called when
 * EOE is connected to Ethernet IP. It allocates the address space
 * for EOE.
 */
int axienet_eoe_probe(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct resource eoe_res;
	int index, ret = 0;

	index = of_property_match_string(pdev->dev.of_node, "reg-names", "eoe");

	if (index < 0)
		return dev_err_probe(&pdev->dev, -EINVAL, "failed to find EOE registers\n");

	ret = of_address_to_resource(pdev->dev.of_node, index, &eoe_res);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "unable to get EOE resource\n");

	lp->eoe_regs = devm_ioremap_resource(&pdev->dev, &eoe_res);

	if (IS_ERR(lp->eoe_regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(lp->eoe_regs), "couldn't map EOE regs\n");

	return 0;
}

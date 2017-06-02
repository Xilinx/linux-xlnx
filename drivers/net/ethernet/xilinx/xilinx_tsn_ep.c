/*
 * Xilinx FPGA Xilinx TSN End point driver.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Author: Saurabh Sengar <saurabhs@xilinx.com>
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

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/skbuff.h>

#include "xilinx_axienet.h"

/**
 * tsn_ep_ioctl - TSN endpoint ioctl interface.
 * @dev: Pointer to the net_device structure
 * @rq: Socket ioctl interface request structure
 * @cmd: Ioctl case
 *
 * Return: 0 on success, Non-zero error value on failure.
 *
 * This is the ioctl interface for TSN end point. Currently this
 * supports only gate programming.
 */
static int tsn_ep_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	switch (cmd) {
#ifdef CONFIG_XILINX_TSN_QBV
	case SIOCCHIOCTL:
		return axienet_set_schedule(dev, rq->ifr_data);
#endif
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * tsn_ep_xmit - TSN endpoint xmit routine.
 * @skb: Packet data
 * @dev: Pointer to the net_device structure
 *
 * Return: Always returns NETDEV_TX_OK.
 *
 * This is dummy xmit function for endpoint as all the data path is assumed to
 * be connected by TEMAC1 as per linux view
 */
static int tsn_ep_xmit(struct sk_buff *skb, struct net_device *dev)
{
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops ep_netdev_ops = {
	.ndo_do_ioctl = tsn_ep_ioctl,
	.ndo_start_xmit = tsn_ep_xmit,
};

static const struct of_device_id tsn_ep_of_match[] = {
	{ .compatible = "xlnx,tsn-ep"},
	{},
};

MODULE_DEVICE_TABLE(of, tsn_ep_of_match);

/**
 * tsn_ep_probe - TSN ep pointer probe function.
 * @pdev:	Pointer to platform device structure.
 *
 * Return: 0, on success
 *	    Non-zero error value on failure.
 *
 * This is the probe routine for TSN endpoint driver.
 */
static int tsn_ep_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct axienet_local *lp;
	struct net_device *ndev;
	struct resource *ethres;

	ndev = alloc_netdev(0, "ep", NET_NAME_UNKNOWN, ether_setup);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &ep_netdev_ops;

	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;

	/* Map device registers */
	ethres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->regs = devm_ioremap_resource(&pdev->dev, ethres);
	if (IS_ERR(lp->regs)) {
		ret = PTR_ERR(lp->regs);
		goto free_netdev;
	}

	ret = register_netdev(lp->ndev);
	if (ret)
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);

	return ret;

free_netdev:
	free_netdev(ndev);

	return ret;
}

static int tsn_ep_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);

	free_netdev(ndev);

	return 0;
}

static struct platform_driver tsn_ep_driver = {
	.probe = tsn_ep_probe,
	.remove = tsn_ep_remove,
	.driver = {
		 .name = "tsn_ep_axienet",
		 .of_match_table = tsn_ep_of_match,
	},
};

module_platform_driver(tsn_ep_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL v2");

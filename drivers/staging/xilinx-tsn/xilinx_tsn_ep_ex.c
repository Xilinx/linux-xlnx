// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/of_net.h>
#include <linux/skbuff.h>

#include "xilinx_axienet_tsn.h"
#include "xilinx_tsn_switch.h"

static const struct of_device_id tsn_ex_ep_of_match[] = {
	{ .compatible = "xlnx,tsn-ex-ep"},
	{},
};

MODULE_DEVICE_TABLE(of, tsn_ex_ep_of_match);

static int tsn_ex_ep_open(struct net_device *ndev)
{
	return 0;
}

static int tsn_ex_ep_stop(struct net_device *ndev)
{
	return 0;
}

static int tsn_ex_ep_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct net_device *master = lp->master;

	skb->dev = master;
	dev_queue_xmit(skb);
	return NETDEV_TX_OK;
}

static void tsn_ex_ep_set_mac_address(struct net_device *ndev, const void *address)
{
	if (address)
		ether_addr_copy(ndev->dev_addr, address);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);
}

static int netdev_set_ex_ep_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;

	tsn_ex_ep_set_mac_address(ndev, addr->sa_data);
	return 0;
}

static const struct net_device_ops ex_ep_netdev_ops = {
	.ndo_open = tsn_ex_ep_open,
	.ndo_stop = tsn_ex_ep_stop,
	.ndo_start_xmit = tsn_ex_ep_xmit,
	.ndo_set_mac_address = netdev_set_ex_ep_mac_address,
};

static int tsn_ex_ep_probe(struct platform_device *pdev)
{
	struct axienet_local *lp;
	struct net_device *ndev;
	struct device_node *ep_node;
	struct axienet_local *ep_lp;
	const void *mac_addr;
	int ret = 0;
	const void *packet_switch;

	ndev = alloc_netdev(sizeof(*lp), "exep",
			    NET_NAME_UNKNOWN, ether_setup);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->flags &= ~IFF_MULTICAST;  /* clear multicast */
	ndev->features = NETIF_F_SG;
	ndev->netdev_ops = &ex_ep_netdev_ops;

	/* MTU range: 64 - 9000 */
	ndev->min_mtu = 64;
	ndev->max_mtu = XAE_JUMBO_MTU;

	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;
	/* Retrieve the MAC address */
	ret = of_get_mac_address(pdev->dev.of_node, (u8 *)mac_addr);
	if (ret) {
		dev_err(&pdev->dev, "could not find MAC address\n");
		goto free_netdev;
	}
	tsn_ex_ep_set_mac_address(ndev, mac_addr);
	packet_switch = of_get_property(pdev->dev.of_node, "packet-switch", NULL);
	ep_node = of_parse_phandle(pdev->dev.of_node, "tsn,endpoint", 0);

	lp->master = of_find_net_device_by_node(ep_node);
	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		goto free_netdev;
	}
	ep_lp = netdev_priv(lp->master);
	ep_lp->ex_ep = ndev;
	if (packet_switch)
		ep_lp->packet_switch = 1;
	return ret;
free_netdev:
	free_netdev(ndev);

	return ret;
}

static int tsn_ex_ep_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);
	free_netdev(ndev);
	return 0;
}

static struct platform_driver tsn_ex_ep_driver = {
	.probe = tsn_ex_ep_probe,
	.remove = tsn_ex_ep_remove,
	.driver = {
		 .name = "tsn_ex_ep_axienet",
		 .of_match_table = tsn_ex_ep_of_match,
	},
};

module_platform_driver(tsn_ex_ep_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL v2");

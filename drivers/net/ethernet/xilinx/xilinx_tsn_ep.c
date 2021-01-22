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

#include "xilinx_axienet.h"

#define TX_BD_NUM_DEFAULT	64
#define RX_BD_NUM_DEFAULT	1024

/**
 * tsn_ep_open - TSN EP driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It also allocates interrupt service
 * routines, enables the interrupt lines and ISR handling. Axi Ethernet
 * core is reset through Axi DMA core. Buffer descriptors are initialized.
 */
static int tsn_ep_open(struct net_device *ndev)
{
	int ret, i = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;

	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		/*MCDMA TX RESET*/
		__axienet_device_reset(q);
	}

	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];

		ret = axienet_mcdma_rx_q_init(ndev, q);
		/* Enable interrupts for Axi MCDMA Rx
		 */
		ret = request_irq(q->rx_irq, axienet_mcdma_rx_irq,
				  IRQF_SHARED, ndev->name, ndev);
		if (ret)
			goto err_dma_rx_irq;

		tasklet_init(&lp->dma_err_tasklet[i],
			     axienet_mcdma_err_handler,
			     (unsigned long)lp->dq[i]);
		napi_enable(&lp->napi[i]);
	}

	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];

		ret = axienet_mcdma_tx_q_init(ndev, q);
		/* Enable interrupts for Axi MCDMA Tx */
		ret = request_irq(q->tx_irq, axienet_mcdma_tx_irq,
				  IRQF_SHARED, ndev->name, ndev);
		if (ret)
			goto err_dma_tx_irq;
	}

	netif_tx_start_all_queues(ndev);
	return 0;

err_dma_tx_irq:
	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];
		free_irq(q->rx_irq, ndev);
	}
err_dma_rx_irq:
	return ret;
}

/**
 * tsn_ep_stop - TSN EP driver stop routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *
 * This is the driver stop routine. It also removes the interrupt handlers
 * and disables the interrupts. The Axi DMA Tx/Rx BDs are released.
 */
static int tsn_ep_stop(struct net_device *ndev)
{
	u32 cr;
	u32 i;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;

	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
		axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET,
				  cr & (~XAXIDMA_CR_RUNSTOP_MASK));
		if (netif_running(ndev))
			netif_stop_queue(ndev);
		free_irq(q->tx_irq, ndev);
	}
	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];
		cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET,
				  cr & (~XAXIDMA_CR_RUNSTOP_MASK));
		if (netif_running(ndev))
			netif_stop_queue(ndev);
		napi_disable(&lp->napi[i]);
		tasklet_kill(&lp->dma_err_tasklet[i]);

		free_irq(q->rx_irq, ndev);
	}

	return 0;
}

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
	case SIOC_GET_SCHED:
		return axienet_get_schedule(dev, rq->ifr_data);
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
	.ndo_open = tsn_ep_open,
	.ndo_stop = tsn_ep_stop,
	.ndo_do_ioctl = tsn_ep_ioctl,
	.ndo_start_xmit = tsn_ep_xmit,
};

static const struct of_device_id tsn_ep_of_match[] = {
	{ .compatible = "xlnx,tsn-ep"},
	{},
};

MODULE_DEVICE_TABLE(of, tsn_ep_of_match);

static const struct axienet_config tsn_endpoint_cfg = {
	.mactype = XAXIENET_1G,
	.setoptions = NULL,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

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
	u16 num_tc = 0;

	ndev = alloc_netdev(sizeof(*lp), "ep", NET_NAME_UNKNOWN, ether_setup);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->flags &= ~IFF_MULTICAST;  /* clear multicast */
	ndev->features = NETIF_F_SG;
	ndev->netdev_ops = &ep_netdev_ops;

	/* MTU range: 64 - 9000 */
	ndev->min_mtu = 64;
	ndev->max_mtu = XAE_JUMBO_MTU;

	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;
	lp->tx_bd_num = TX_BD_NUM_DEFAULT;
	lp->rx_bd_num = RX_BD_NUM_DEFAULT;

	/* TODO
	 * there are two temacs or two slaves to ep
	 * get this infor from design?
	 */
	lp->slaves[0] = NULL;
	lp->slaves[1] = NULL;

	lp->axienet_config = &tsn_endpoint_cfg;

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;

	/* Setup checksum offload, but default to off if not specified */
	lp->features = 0;

	lp->eth_hasnobuf = of_property_read_bool(pdev->dev.of_node,
						 "xlnx,eth-hasnobuf");

	/* Retrieve the MAC address */
	ret = of_get_mac_address(pdev->dev.of_node, ndev->dev_addr);
	if (ret) {
		dev_err(&pdev->dev, "could not find MAC address\n");
		goto free_netdev;
	}
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-tc", &num_tc);
	if (ret || (num_tc != 2 && num_tc != 3))
		lp->num_tc = XAE_MAX_TSN_TC;
	else
		lp->num_tc = num_tc;
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

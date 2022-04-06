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
#include "xilinx_tsn_switch.h"

#define TX_BD_NUM_DEFAULT	64
#define RX_BD_NUM_DEFAULT	1024

static u8 st_pcp[8];
static uint st_count;
module_param_array(st_pcp, byte, &st_count, 0644);
MODULE_PARM_DESC(st_pcp, "Array of pcp values mapped to ST class at the compile time");

static u8 res_pcp[8];
static uint res_count;
module_param_array(res_pcp, byte, &res_count, 0644);
MODULE_PARM_DESC(res_pcp, "Array of pcp values mapped to RES class at the compile time");

int tsn_data_path_open(struct net_device *ndev)
{
	int ret, i = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;

	static char irq_name[XAE_MAX_QUEUES + XAE_TSN_MIN_QUEUES][24];
	u8 irq_cnt = 0;

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
		sprintf(irq_name[irq_cnt], "%s_mcdma_rx_%d", ndev->name, i + 1);
		ret = request_irq(q->rx_irq, axienet_mcdma_rx_irq,
				  IRQF_SHARED, irq_name[irq_cnt], ndev);
		if (ret)
			goto err_dma_rx_irq;

		tasklet_init(&lp->dma_err_tasklet[i],
			     axienet_mcdma_err_handler,
			     (unsigned long)lp->dq[i]);
		napi_enable(&lp->napi[i]);
		irq_cnt++;
	}

	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];

		ret = axienet_mcdma_tx_q_init(ndev, q);
		/* Enable interrupts for Axi MCDMA Tx */
		sprintf(irq_name[irq_cnt], "%s_mcdma_tx_%d", ndev->name, i + 1);
		ret = request_irq(q->tx_irq, axienet_mcdma_tx_irq,
				  IRQF_SHARED, irq_name[irq_cnt], ndev);
		if (ret)
			goto err_dma_tx_irq;
		irq_cnt++;
	}
#ifdef CONFIG_AXIENET_HAS_TADMA
	ret = axienet_tadma_open(ndev);
	if (ret)
		goto err_tadma;
#endif

	netif_tx_start_all_queues(ndev);
	return 0;

#ifdef CONFIG_AXIENET_HAS_TADMA
err_tadma:
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		free_irq(q->tx_irq, ndev);
	}
#endif
err_dma_tx_irq:
	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];
		free_irq(q->rx_irq, ndev);
	}
err_dma_rx_irq:
	return ret;
}

/**
 * tsn_ep_open - TSN EP driver open routine.
 * @ndev:       Pointer to net_device structure
 *
 * Return: 0, on success.
 *          non-zero error value on failure
 *
 * This is the driver open routine. It also allocates interrupt service
 * routines, enables the interrupt lines and ISR handling. Axi Ethernet
 * core is reset through Axi DMA core. Buffer descriptors are initialized.
 */
static int tsn_ep_open(struct net_device *ndev)
{
	return tsn_data_path_open(ndev);
}

int tsn_data_path_close(struct net_device *ndev)
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
#ifdef CONFIG_AXIENET_HAS_TADMA
	axienet_tadma_stop(ndev);
#endif
	axienet_dma_bd_release(ndev);

	return 0;
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
	return tsn_data_path_close(ndev);
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
#ifdef CONFIG_AXIENET_HAS_TADMA
	case SIOC_TADMA_OFF:
		return axienet_tadma_off(dev, rq->ifr_data);
	case SIOC_TADMA_STR_ADD:
		return axienet_tadma_add_stream(dev, rq->ifr_data);
	case SIOC_TADMA_PROG_ALL:
		return axienet_tadma_program(dev, rq->ifr_data);
	case SIOC_TADMA_STR_FLUSH:
		return axienet_tadma_flush_stream(dev, rq->ifr_data);
#endif
	default:
		return -EOPNOTSUPP;
	}
}

u16 axienet_tsn_pcp_to_queue(struct net_device *ndev, struct sk_buff *skb)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct ethhdr *hdr = (struct ethhdr *)skb->data;
	u16 ether_type = ntohs(hdr->h_proto);
	u16 vlan_tci;
	u8 pcp = 0;

	if (unlikely(ether_type == ETH_P_8021Q)) {
		struct vlan_ethhdr *vhdr = (struct vlan_ethhdr *)skb->data;

		/* ether_type = ntohs(vhdr->h_vlan_encapsulated_proto); */

		vlan_tci = ntohs(vhdr->h_vlan_TCI);

		pcp = (vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
#ifdef CONFIG_AXIENET_HAS_TADMA
		if (lp->st_pcp & (1 << pcp)) /* ST Traffic */
			return ST_QUEUE_NUMBER;
#endif
		if (lp->num_tc == 3 && (lp->res_pcp & (1 << pcp))) {
			if (lp->num_tx_queues > 1)
				return RES_QUEUE_NUMBER;
		}
	}
	return BE_QUEUE_NUMBER;
}

static u16 axienet_tsn_ep_select_queue(struct net_device *ndev, struct sk_buff *skb,
				       struct net_device *sb_dev)
{
	return axienet_tsn_pcp_to_queue(ndev, skb);
}

/**
 * tsn_ep_xmit - TSN endpoint xmit routine.
 * @skb: Packet data
 * @ndev: Pointer to the net_device structure
 *
 * Return: Always returns NETDEV_TX_OK.
 *
 * This is dummy xmit function for endpoint as all the data path is assumed to
 * be connected by TEMAC1 as per linux view
 */
static int tsn_ep_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	u16 map = skb_get_queue_mapping(skb);

#ifdef CONFIG_AXIENET_HAS_TADMA
	if (map == ST_QUEUE_NUMBER) /* ST Traffic */
		return axienet_tadma_xmit(skb, ndev, map);
#endif

	return axienet_queue_xmit(skb, ndev, map);
}

static void tsn_ep_set_mac_address(struct net_device *ndev, const void *address)
{
	if (address)
		ether_addr_copy(ndev->dev_addr, address);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);
}

/**
 * netdev_set_mac_address - Write the MAC address (from outside the driver)
 * @ndev:	Pointer to the net_device structure
 * @p:		6 byte Address to be written as MAC address
 *
 * Return: 0 for all conditions. Presently, there is no failure case.
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. This is the function that goes into net_device_ops structure entry
 * ndo_set_mac_address.
 */
static int netdev_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;

	tsn_ep_set_mac_address(ndev, addr->sa_data);
	return 0;
}

static const struct net_device_ops ep_netdev_ops = {
	.ndo_open = tsn_ep_open,
	.ndo_stop = tsn_ep_stop,
	.ndo_do_ioctl = tsn_ep_ioctl,
	.ndo_start_xmit = tsn_ep_xmit,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_select_queue = axienet_tsn_ep_select_queue,
#if defined(CONFIG_XILINX_TSN_SWITCH)
	.ndo_get_port_parent_id = tsn_switch_get_port_parent_id,
#endif
};

static const struct of_device_id tsn_ep_of_match[] = {
	{ .compatible = "xlnx,tsn-ep"},
	{},
};

MODULE_DEVICE_TABLE(of, tsn_ep_of_match);

/* separate function is needed to probe tsn mcdma
 * as there is asymmetry between rx channels and tx channels
 * having unique probe for both tsn and axienet with mcdma is not possible
 */
int __maybe_unused tsn_mcdma_probe(struct platform_device *pdev, struct axienet_local *lp,
				   struct net_device *ndev)
{
	int i, ret = 0;
	struct axienet_dma_q *q;
	struct device_node *np;
	struct resource dmares;
	const char *str;
	u32 num;

	ret = of_property_count_strings(pdev->dev.of_node, "xlnx,channel-ids");
	if (ret < 0)
		return -EINVAL;

	np = of_parse_phandle(pdev->dev.of_node, "axistream-connected-rx",
			      0);
	/* get number of associated queues */
	ret = of_property_read_u32(np, "xlnx,num-s2mm-channels", &num);
	if (ret < 0)
		return -EINVAL;

	lp->num_rx_queues = num;
	pr_info("%s: num_rx_queues: %d\n", __func__, lp->num_rx_queues);

	for_each_rx_dma_queue(lp, i) {
		q = kzalloc(sizeof(*q), GFP_KERNEL);

		/* parent */
		q->lp = lp;
		lp->dq[i] = q;
		ret = of_property_read_string_index(pdev->dev.of_node,
						    "xlnx,channel-ids", i,
						    &str);
		ret = kstrtou16(str, 16, &q->chan_id);
		lp->qnum[i] = i;
		lp->chan_num[i] = q->chan_id;
	}

	if (IS_ERR(np)) {
		dev_err(&pdev->dev, "could not find DMA node\n");
		return ret;
	}

	ret = of_address_to_resource(np, 0, &dmares);
	if (ret) {
		dev_err(&pdev->dev, "unable to get DMA resource\n");
		return ret;
	}

	lp->mcdma_regs = devm_ioremap_resource(&pdev->dev, &dmares);
	if (IS_ERR(lp->mcdma_regs)) {
		dev_err(&pdev->dev, "iormeap failed for the dma\n");
		ret = PTR_ERR(lp->mcdma_regs);
		return ret;
	}

	axienet_mcdma_rx_probe(pdev, lp, ndev);
	axienet_mcdma_tx_probe(pdev, np, lp);

	return 0;
}

static const struct axienet_config tsn_endpoint_cfg = {
	.mactype = XAXIENET_1G,
	.setoptions = NULL,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

/**
 *axienet_get_pcp_mask - gets the compile time pcp values that
 *are mapped to ST and RES traffic from uEnv.txt and assigns them
 *to st_pcp and res_pcp fields of axienet_local structure
 *@lp:		axienet local structure
 *@num_tc:	number of traffic classes
 *
 * Return: Always returns 0
 */
int axienet_get_pcp_mask(struct axienet_local *lp, u16 num_tc)
{
	u8 i;
	u8 invalid_pcp = 0;

	lp->st_pcp = 0;
	lp->res_pcp = 0;
	if (st_count == 0 || st_count > 8) {
		lp->st_pcp = 1 << 4;
	} else {
		for (i = 0; i < st_count; i++) {
			if (st_pcp[i] >= 8) {
				invalid_pcp = 1;
				break;
			}
			lp->st_pcp = lp->st_pcp | 1 << st_pcp[i];
		}
		if (invalid_pcp) {
			pr_warn("pcp value cannot be greater than or equal to 8\n");
			lp->st_pcp = 1 << 4;
			invalid_pcp = 0;
		}
	}
	if (num_tc == 3) {
		if (res_count == 0 || res_count > 8) {
			lp->res_pcp = 1 << 2 | 1 << 3;
		} else {
			for (i = 0; i < res_count; i++) {
				if (res_pcp[i] >= 8) {
					invalid_pcp = 1;
					break;
				}
				lp->res_pcp = lp->res_pcp | 1 << res_pcp[i];
			}
			if (invalid_pcp) {
				pr_warn("pcp value cannot be greater than or equal to 8\n");
				lp->res_pcp = 1 << 2 | 1 << 3;
			}
		}
	}
	return 0;
}

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
	u16 num_queues = XAE_MAX_QUEUES;
	u16 num_tc = 0;
	struct device_node *np;
	char irq_name[32];

	ndev = alloc_netdev_mq(sizeof(*lp), "ep",
			       NET_NAME_UNKNOWN, ether_setup, num_queues);
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
	lp->switch_prt = PORT_EP;

	/* TODO
	 * there are two temacs or two slaves to ep
	 * get this infor from design?
	 */
	lp->slaves[0] = NULL;
	lp->slaves[1] = NULL;
	lp->ex_ep = NULL;
	lp->packet_switch = 0;

	lp->axienet_config = &tsn_endpoint_cfg;

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;

	/* check if ep has dma connected
	 * in a ep_only system dma(mcdma/tadma) is connected to temac1
	 */
	np = of_parse_phandle(pdev->dev.of_node, "axistream-connected-rx", 0);
	if (!np) {
		/* dont expose ep dev in ep_only system
		 * all functionality handled by temac1/eth1
		 */
		free_netdev(ndev);
		of_node_put(np);
		return 0;
	}

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

	ret = tsn_mcdma_probe(pdev, lp, ndev);
	if (ret) {
		dev_err(&pdev->dev, "Getting MCDMA resource failed\n");
		goto free_netdev;
	}

#ifdef CONFIG_AXIENET_HAS_TADMA
	ret = axienet_tadma_probe(pdev, ndev);
	if (ret) {
		dev_err(&pdev->dev, "Getting TADMA resource failed\n");
		goto free_netdev;
	}
#endif
	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-tc", &num_tc);
	if (ret || (num_tc != 2 && num_tc != 3))
		lp->num_tc = XAE_MAX_TSN_TC;
	else
		lp->num_tc = num_tc;
	axienet_get_pcp_mask(lp, lp->num_tc);
	/* Map device registers */
	ethres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->regs = devm_ioremap_resource(&pdev->dev, ethres);
	if (IS_ERR(lp->regs)) {
		ret = PTR_ERR(lp->regs);
		goto free_netdev;
	}
#ifdef CONFIG_XILINX_TSN_QBV
	lp->qbv_regs = lp->regs;
#endif

	sprintf(irq_name, "tsn_ep_scheduler_irq");
	lp->qbv_irq = platform_get_irq_byname(pdev, irq_name);
#ifdef CONFIG_XILINX_TSN_QBV
	axienet_qbv_init(ndev);
#endif

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		goto free_netdev;
	}
	return ret;

free_netdev:
	free_netdev(ndev);

	return ret;
}

static int tsn_ep_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

#ifdef CONFIG_XILINX_TSN_QBV
	axienet_qbv_remove(ndev);
#endif
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

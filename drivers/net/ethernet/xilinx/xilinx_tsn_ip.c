// SPDX-License-Identifier: GPL-2.0

/* Xilinx FPGA Xilinx TSN IP driver.
 *
 * Copyright (C) 2018 Xilinx, Inc. All rights reserved.
 *
 * Author: Priyadarshini Babu <priyadar@xilinx.com>
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

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/xilinx_phy.h>

#include "xilinx_axienet.h"

#ifdef CONFIG_XILINX_TSN_PTP
#include "xilinx_tsn_ptp.h"
#include "xilinx_tsn_timer.h"
#endif

#define TSN_TX_BE_QUEUE  0
#define TSN_TX_RES_QUEUE 1
#define TSN_TX_ST_QUEUE  2

#define XAE_TEMAC1 0
#define XAE_TEMAC2 1
static const struct of_device_id tsn_ip_of_match[] = {
	{ .compatible = "xlnx,tsn-endpoint-ethernet-mac-1.0"},
	{ .compatible = "xlnx,tsn-endpoint-ethernet-mac-2.0"},
	{},
};

MODULE_DEVICE_TABLE(of, tsn_ip_of_match);

/**
 * tsn_ip_probe - TSN ip pointer probe function.
 * @pdev:	Pointer to platform device structure.
 *
 * Return: 0, on success
 *	    Non-zero error value on failure.
 *
 * This is the probe routine for TSN driver.
 */
static int tsn_ip_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info("TSN endpoint ethernet mac Probe\n");

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret)
		pr_err("TSN endpoint probe error (%i)\n", ret);

	return ret;
}

static int tsn_ip_remove(struct platform_device *pdev)
{
	return 0;
}

/**
 * axienet_tsn_xmit - Starts the TSN transmission.
 * @skb:	sk_buff pointer that contains data to be Txed.
 * @ndev:	Pointer to net_device structure.
 *
 * Return: NETDEV_TX_OK, on success
 *	    Non-zero error value on failure.
 *
 * This function is invoked from upper layers to initiate transmission. The
 * function uses the next available free BDs and populates their fields to
 * start the transmission. Use axienet_ptp_xmit() for PTP 1588 packets and
 * use master EP xmit for other packets transmission.
 */
int axienet_tsn_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct ethhdr *hdr = (struct ethhdr *)skb->data;
	u16 ether_type = ntohs(hdr->h_proto);
	struct net_device *master = lp->master;

#ifdef CONFIG_XILINX_TSN_PTP
	/* check if skb is a PTP frame ? */
	if (unlikely(ether_type == ETH_P_1588))
		return axienet_ptp_xmit(skb, ndev);
#endif
	/* use EP to xmit non-PTP frames */
	skb->dev = master;
	dev_queue_xmit(skb);

	return NETDEV_TX_OK;
}

/**
 * axienet_tsn_probe - TSN mac probe function.
 * @pdev:	Pointer to platform device structure.
 * @lp:		Pointer to axienet local structure
 * @ndev:	Pointer to net_device structure.
 *
 * Return: 0, on success
 *	    Non-zero error value on failure.
 *
 * This is the probe for TSN mac nodes.
 */
int axienet_tsn_probe(struct platform_device *pdev,
		      struct axienet_local *lp,
		      struct net_device *ndev)
{
	int ret = 0;
	char irq_name[32];
	bool slave = false;
	u8     temac_no;
	u32 qbv_addr, qbv_size;
	u32 abl_reg;
	struct device_node *ep_node;
	struct axienet_local *ep_lp;

	slave = of_property_read_bool(pdev->dev.of_node,
				      "xlnx,tsn-slave");
	if (slave)
		temac_no = XAE_TEMAC2;
	else
		temac_no = XAE_TEMAC1;

	sprintf(irq_name, "interrupt_ptp_rx_%d", temac_no + 1);
	lp->ptp_rx_irq = platform_get_irq_byname(pdev, irq_name);

	pr_info("ptp RX irq: %d %s\n", lp->ptp_rx_irq, irq_name);
	sprintf(irq_name, "interrupt_ptp_tx_%d", temac_no + 1);
	lp->ptp_tx_irq = platform_get_irq_byname(pdev, irq_name);
	pr_info("ptp TX irq: %d %s\n", lp->ptp_tx_irq, irq_name);

	sprintf(irq_name, "tsn_switch_scheduler_irq_%d", temac_no + 1);
	lp->qbv_irq = platform_get_irq_byname(pdev, irq_name);

	/*Ignoring if the qbv_irq is not exist*/
	if (lp->qbv_irq > 0)
		pr_info("qbv_irq: %d %s\n", lp->qbv_irq, irq_name);

	spin_lock_init(&lp->ptp_tx_lock);

	if (temac_no == XAE_TEMAC1)
		axienet_ptp_timer_probe((lp->regs + XAE_RTC_OFFSET), pdev);

	/* enable VLAN */
	lp->options |= XAE_OPTION_VLAN;
	axienet_setoptions(lp->ndev, lp->options);

	/* get the ep device */
	ep_node = of_parse_phandle(pdev->dev.of_node, "tsn,endpoint", 0);

	lp->master = of_find_net_device_by_node(ep_node);
#ifdef CONFIG_XILINX_TSN_QBV
	lp->qbv_regs = 0;
	abl_reg = axienet_ior(lp, XAE_TSN_ABL_OFFSET);
	if (!(abl_reg & TSN_BRIDGEEP_EPONLY)) {
		if (of_property_read_u32(pdev->dev.of_node,
					 "xlnx,qbv-addr", &qbv_addr) == 0) {
			if ((of_property_read_u32(pdev->dev.of_node,
						  "xlnx,qbv-size", &qbv_size) ==
			     0) && qbv_size) {
				lp->qbv_regs = devm_ioremap(&pdev->dev,
							    qbv_addr, qbv_size);
				if (IS_ERR(lp->qbv_regs)) {
					dev_err(&pdev->dev,
						"ioremap failed for the qbv\n");
					ret = PTR_ERR(lp->qbv_regs);
					return ret;
				}
				ret = axienet_qbv_init(ndev);
			}
		}
	}
#endif
	/* EP+Switch */
	/* store the slaves to master(ep) */
	ep_lp = netdev_priv(lp->master);
	ep_lp->slaves[temac_no] = ndev;

	return 0;
}

/**
 * axienet_device_reset - Reset and initialize the Axi Ethernet hardware.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to reset and initialize the Axi Ethernet core. This
 * is typically called during initialization. It does a reset of the Axi DMA
 * Rx/Tx channels and initializes the Axi DMA BDs. Since Axi DMA reset lines
 * areconnected to Axi Ethernet reset lines, this in turn resets the Axi
 * Ethernet core. No separate hardware reset is done for the Axi Ethernet
 * core.
 */
static void axienet_device_reset(struct net_device *ndev)
{
	u32 axienet_status;
	struct axienet_local *lp = netdev_priv(ndev);

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;

	lp->options |= XAE_OPTION_VLAN;
	lp->options &= (~XAE_OPTION_JUMBO);

	if (ndev->mtu > XAE_MTU && ndev->mtu <= XAE_JUMBO_MTU) {
		lp->max_frm_size = ndev->mtu + VLAN_ETH_HLEN +
					XAE_TRL_SIZE;
		if (lp->max_frm_size <= lp->rxmem)
			lp->options |= XAE_OPTION_JUMBO;
	}

	axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
	axienet_status &= ~XAE_RCW1_RX_MASK;
	axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);

	if (lp->axienet_config->mactype == XAXIENET_1G &&
	    !lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);

		/* Enable Receive errors */
		axienet_iow(lp, XAE_IE_OFFSET, XAE_INT_RECV_ERROR_MASK);
	}

	axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);
	lp->axienet_config->setoptions(ndev, lp->options &
				       ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	lp->axienet_config->setoptions(ndev, lp->options);

	netif_trans_update(ndev);
}

/**
 * axienet_mii_init - MII init routine
 * @ndev:	Pointer to net_device structure.
 *
 * Return: 0, on success
 *	    Non-zero error value on failure.
 *
 * This routine initializes MII.
 */
static int axienet_mii_init(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int ret, mdio_mcreg;

	mdio_mcreg = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	/* Disable the MDIO interface till Axi Ethernet Reset is completed.
	 * When we do an Axi Ethernet reset, it resets the complete core
	 * Including the MDIO. If MDIO is not disabled when the reset process is
	 * Started, MDIO will be broken afterwards.
	 */
	axienet_iow(lp, XAE_MDIO_MC_OFFSET,
		    (mdio_mcreg & (~XAE_MDIO_MC_MDIOEN_MASK)));
	axienet_device_reset(ndev);
	/* Enable the MDIO */
	axienet_iow(lp, XAE_MDIO_MC_OFFSET, mdio_mcreg);
	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * axienet_tsn_open - TSN driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It calls phy_start to start the PHY device.
 * It also allocates interrupt service routines, enables the interrupt lines
 * and ISR handling. Axi Ethernet core is reset through Axi DMA core.
 */
int axienet_tsn_open(struct net_device *ndev)
{
	int ret;
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = NULL;

	ret = axienet_mii_init(ndev);
	if (ret < 0)
		return ret;

	if (lp->phy_node) {
		if (lp->phy_mode == XAE_PHY_TYPE_GMII) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
						axienet_adjust_link, 0,
						PHY_INTERFACE_MODE_GMII);
		} else if (lp->phy_mode == XAE_PHY_TYPE_RGMII_2_0) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
						axienet_adjust_link, 0,
						PHY_INTERFACE_MODE_RGMII_ID);
		} else if ((lp->axienet_config->mactype == XAXIENET_1G) ||
			     (lp->axienet_config->mactype == XAXIENET_2_5G)) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
						axienet_adjust_link,
						lp->phy_flags,
						lp->phy_mode);
		}

		if (!phydev)
			dev_err(lp->dev, "of_phy_connect() failed\n");
		else
			phy_start(phydev);
	}

	INIT_WORK(&lp->tx_tstamp_work, axienet_tx_tstamp);
	skb_queue_head_init(&lp->ptp_txq);

	lp->ptp_rx_hw_pointer = 0;
	lp->ptp_rx_sw_pointer = 0xff;

	axienet_iow(lp, PTP_RX_CONTROL_OFFSET, PTP_RX_PACKET_CLEAR);

	ret = request_irq(lp->ptp_rx_irq, axienet_ptp_rx_irq,
			  0, "ptp_rx", ndev);
	if (ret)
		goto err_ptp_rx_irq;

	ret = request_irq(lp->ptp_tx_irq, axienet_ptp_tx_irq,
			  0, "ptp_tx", ndev);
	if (ret)
		goto err_ptp_tx_irq;

	netif_tx_start_all_queues(ndev);

	return 0;

err_ptp_tx_irq:
	free_irq(lp->ptp_rx_irq, ndev);
err_ptp_rx_irq:
	return ret;
}

static struct platform_driver tsn_ip_driver = {
	.probe = tsn_ip_probe,
	.remove = tsn_ip_remove,
	.driver = {
		 .name = "tsn_ip_axienet",
		 .of_match_table = tsn_ip_of_match,
	},
};

module_platform_driver(tsn_ip_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL v2");

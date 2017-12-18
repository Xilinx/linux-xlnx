/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/
#include <linux/pci.h>
#include <linux/if_vlan.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"

int liquidio_set_feature(struct net_device *netdev, int cmd, u16 param1)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = cmd;
	nctrl.ncmd.s.param1 = param1;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Feature change failed in core (ret: 0x%x)\n",
			ret);
	}
	return ret;
}

void octeon_report_tx_completion_to_bql(void *txq, unsigned int pkts_compl,
					unsigned int bytes_compl)
{
	struct netdev_queue *netdev_queue = txq;

	netdev_tx_completed_queue(netdev_queue, pkts_compl, bytes_compl);
}

void octeon_update_tx_completion_counters(void *buf, int reqtype,
					  unsigned int *pkts_compl,
					  unsigned int *bytes_compl)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb = NULL;
	struct octeon_soft_command *sc;

	switch (reqtype) {
	case REQTYPE_NORESP_NET:
	case REQTYPE_NORESP_NET_SG:
		finfo = buf;
		skb = finfo->skb;
		break;

	case REQTYPE_RESP_NET_SG:
	case REQTYPE_RESP_NET:
		sc = buf;
		skb = sc->callback_arg;
		break;

	default:
		return;
	}

	(*pkts_compl)++;
/*TODO, Use some other pound define to suggest
 * the fact that iqs are not tied to netdevs
 * and can take traffic from different netdevs
 * hence bql reporting is done per packet
 * than in bulk. Usage of NO_NAPI in txq completion is
 * a little confusing
 */
	*bytes_compl += skb->len;
}

void octeon_report_sent_bytes_to_bql(void *buf, int reqtype)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb;
	struct octeon_soft_command *sc;
	struct netdev_queue *txq;

	switch (reqtype) {
	case REQTYPE_NORESP_NET:
	case REQTYPE_NORESP_NET_SG:
		finfo = buf;
		skb = finfo->skb;
		break;

	case REQTYPE_RESP_NET_SG:
	case REQTYPE_RESP_NET:
		sc = buf;
		skb = sc->callback_arg;
		break;

	default:
		return;
	}

	txq = netdev_get_tx_queue(skb->dev, skb_get_queue_mapping(skb));
	netdev_tx_sent_queue(txq, skb->len);
}

void liquidio_link_ctrl_cmd_completion(void *nctrl_ptr)
{
	struct octnic_ctrl_pkt *nctrl = (struct octnic_ctrl_pkt *)nctrl_ptr;
	struct net_device *netdev = (struct net_device *)nctrl->netpndev;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	u8 *mac;

	switch (nctrl->ncmd.s.cmd) {
	case OCTNET_CMD_CHANGE_DEVFLAGS:
	case OCTNET_CMD_SET_MULTI_LIST:
		break;

	case OCTNET_CMD_CHANGE_MACADDR:
		mac = ((u8 *)&nctrl->udd[0]) + 2;
		netif_info(lio, probe, lio->netdev,
			   "MACAddr changed to %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
			   mac[0], mac[1],
			   mac[2], mac[3],
			   mac[4], mac[5]);
		break;

	case OCTNET_CMD_CHANGE_MTU:
		/* If command is successful, change the MTU. */
		netif_info(lio, probe, lio->netdev, "MTU Changed from %d to %d\n",
			   netdev->mtu, nctrl->ncmd.s.param1);
		dev_info(&oct->pci_dev->dev, "%s MTU Changed from %d to %d\n",
			 netdev->name, netdev->mtu,
			 nctrl->ncmd.s.param1);
		netdev->mtu = nctrl->ncmd.s.param1;
		queue_delayed_work(lio->link_status_wq.wq,
				   &lio->link_status_wq.wk.work, 0);
		break;

	case OCTNET_CMD_GPIO_ACCESS:
		netif_info(lio, probe, lio->netdev, "LED Flashing visual identification\n");

		break;

	case OCTNET_CMD_ID_ACTIVE:
		netif_info(lio, probe, lio->netdev, "LED Flashing visual identification\n");

		break;

	case OCTNET_CMD_LRO_ENABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Enabled\n", netdev->name);
		break;

	case OCTNET_CMD_LRO_DISABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Disabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_VERBOSE_ENABLE:
		dev_info(&oct->pci_dev->dev, "%s Firmware debug enabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_VERBOSE_DISABLE:
		dev_info(&oct->pci_dev->dev, "%s Firmware debug disabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_ENABLE_VLAN_FILTER:
		dev_info(&oct->pci_dev->dev, "%s VLAN filter enabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_ADD_VLAN_FILTER:
		dev_info(&oct->pci_dev->dev, "%s VLAN filter %d added\n",
			 netdev->name, nctrl->ncmd.s.param1);
		break;

	case OCTNET_CMD_DEL_VLAN_FILTER:
		dev_info(&oct->pci_dev->dev, "%s VLAN filter %d removed\n",
			 netdev->name, nctrl->ncmd.s.param1);
		break;

	case OCTNET_CMD_SET_SETTINGS:
		dev_info(&oct->pci_dev->dev, "%s settings changed\n",
			 netdev->name);

		break;

	/* Case to handle "OCTNET_CMD_TNL_RX_CSUM_CTL"
	 * Command passed by NIC driver
	 */
	case OCTNET_CMD_TNL_RX_CSUM_CTL:
		if (nctrl->ncmd.s.param1 == OCTNET_CMD_RXCSUM_ENABLE) {
			netif_info(lio, probe, lio->netdev,
				   "RX Checksum Offload Enabled\n");
		} else if (nctrl->ncmd.s.param1 ==
			   OCTNET_CMD_RXCSUM_DISABLE) {
			netif_info(lio, probe, lio->netdev,
				   "RX Checksum Offload Disabled\n");
		}
		break;

		/* Case to handle "OCTNET_CMD_TNL_TX_CSUM_CTL"
		 * Command passed by NIC driver
		 */
	case OCTNET_CMD_TNL_TX_CSUM_CTL:
		if (nctrl->ncmd.s.param1 == OCTNET_CMD_TXCSUM_ENABLE) {
			netif_info(lio, probe, lio->netdev,
				   "TX Checksum Offload Enabled\n");
		} else if (nctrl->ncmd.s.param1 ==
			   OCTNET_CMD_TXCSUM_DISABLE) {
			netif_info(lio, probe, lio->netdev,
				   "TX Checksum Offload Disabled\n");
		}
		break;

		/* Case to handle "OCTNET_CMD_VXLAN_PORT_CONFIG"
		 * Command passed by NIC driver
		 */
	case OCTNET_CMD_VXLAN_PORT_CONFIG:
		if (nctrl->ncmd.s.more == OCTNET_CMD_VXLAN_PORT_ADD) {
			netif_info(lio, probe, lio->netdev,
				   "VxLAN Destination UDP PORT:%d ADDED\n",
				   nctrl->ncmd.s.param1);
		} else if (nctrl->ncmd.s.more ==
			   OCTNET_CMD_VXLAN_PORT_DEL) {
			netif_info(lio, probe, lio->netdev,
				   "VxLAN Destination UDP PORT:%d DELETED\n",
				   nctrl->ncmd.s.param1);
		}
		break;

	case OCTNET_CMD_SET_FLOW_CTL:
		netif_info(lio, probe, lio->netdev, "Set RX/TX flow control parameters\n");
		break;

	default:
		dev_err(&oct->pci_dev->dev, "%s Unknown cmd %d\n", __func__,
			nctrl->ncmd.s.cmd);
	}
}

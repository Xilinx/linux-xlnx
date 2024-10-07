// SPDX-License-Identifier: GPL-2.0

/* Xilinx AXI EOE (EOE programming)
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * This file contains probe function for EOE TX and RX programming.
 */

#include <linux/of_address.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/ip.h>

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
 * for EOE. Parses through device tree and updates Tx and RX offload features
 * in netdev and axiethernet private structure respectively.
 */
int axienet_eoe_probe(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);
	struct resource eoe_res;
	int index, ret = 0;
	int value;

	index = of_property_match_string(pdev->dev.of_node, "reg-names", "eoe");

	if (index < 0)
		return dev_err_probe(&pdev->dev, -EINVAL, "failed to find EOE registers\n");

	ret = of_address_to_resource(pdev->dev.of_node, index, &eoe_res);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "unable to get EOE resource\n");

	lp->eoe_regs = devm_ioremap_resource(&pdev->dev, &eoe_res);

	if (IS_ERR(lp->eoe_regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(lp->eoe_regs), "couldn't map EOE regs\n");

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,tx-hw-offload", &value);
	if (!ret) {
		dev_dbg(&pdev->dev, "xlnx,tx-hw-offload %d\n", value);

		switch (value) {
		case 0:
			break;

		case 1:
			/* Can checksum Tx UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM;
			ndev->hw_features |= NETIF_F_IP_CSUM;
			break;

		case 2:
			ndev->features |= NETIF_F_IP_CSUM | NETIF_F_GSO_UDP_L4;
			ndev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_GSO_UDP_L4;
			break;

		default:
			dev_warn(&pdev->dev, "xlnx,tx-hw-offload: %d is an invalid value\n", value);
			return -EINVAL;
		}
	}

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,rx-hw-offload", &value);
	if (!ret) {
		dev_dbg(&pdev->dev, "xlnx,rx-hw-offload %d\n", value);

		switch (value) {
		case 0:
			lp->eoe_features |= RX_HW_NO_OFFLOAD;
			break;
		case 1:
			lp->eoe_features |= RX_HW_CSO;
			break;
		case 2:
			lp->eoe_features |= RX_HW_UDP_GRO;
			break;
		default:
			dev_warn(&pdev->dev, "xlnx,rx-hw-offload: %d is an invalid value\n", value);
			return -EINVAL;
		}
	}

	return 0;
}

static inline int axienet_eoe_packet_header_length(struct sk_buff *skb)
{
	u32 hdr_len = skb_mac_header_len(skb) + skb_network_header_len(skb);

	if (skb->sk->sk_protocol == IPPROTO_UDP)
		hdr_len += sizeof(struct udphdr);
	else if (skb->sk->sk_protocol == IPPROTO_TCP)
		hdr_len += tcp_hdrlen(skb);

	return hdr_len;
}

void axienet_eoe_config_hwcso(struct net_device *ndev,
			      struct aximcdma_bd *cur_p)
{
	/* 1) When total length < MSS, APP0 can be made all 0's and no need to program
	 * valid values on other fields except bits 8 to 11 in APP1
	 * 2) When APP0 is all 0's, the total length is assumed to be less than the MSS
	 * size
	 * 3) Bit 9(checksum offload) must be 0 to calculate checksum on segmented
	 * packets.
	 */
	cur_p->app1 |= (ndev->mtu << XMCDMA_APP1_MSS_SIZE_SHIFT) &
			XMCDMA_APP1_MSS_SIZE_MASK;

	cur_p->app1 |= (XMCDMA_APP1_GSO_PKT_MASK |
			XMCDMA_APP1_UDP_SO_MASK |
			XMCDMA_APP1_TCP_SO_MASK);
}

void axienet_eoe_config_hwgso(struct net_device *ndev,
			      struct sk_buff *skb,
			      struct aximcdma_bd *cur_p)
{
	/* 1) Total length, MSS, Header length has to be filled out correctly. There is
	 * no error checking mechanism in the code. Code blindly believes in this
	 * information for segmentation.
	 * 2) When total length < MSS, APP0 can be made all 0's and no need to program
	 * valid values on other fields except bits 8 to 11 in APP1
	 * 3) When APP0 is all 0's, the total length is assumed to be less than the MSS
	 * size and no segmentation will be performed
	 * 4) TCP segmentation is performed when bit 10 (TCP segmentation offload) and
	 * bit 8(is GSO packet) are 0's in APP1. Otherwise the packets are bypassed.
	 * 5) UDP segmentation is performed when bit 11 (UDP segmentation offload) and
	 * bit 8(is GSO packet) are 0's in APP1.Otherwise the packets are bypassed.
	 * 6) Bit 9(checksum offload) must be 0 to calculate checksum on segmented
	 * packets.
	 */
	cur_p->app1 = (ndev->mtu << XMCDMA_APP1_MSS_SIZE_SHIFT) &
		       XMCDMA_APP1_MSS_SIZE_MASK;

	if (skb_shinfo(skb)->gso_size) {
		cur_p->app0 = (skb->len - XAE_HDR_SIZE) & XMCDMA_APP0_TOTAL_PKT_LEN_MASK;
		cur_p->app0 |= (axienet_eoe_packet_header_length(skb) <<
				XMCDMA_APP0_PKT_HEAD_LEN_SHIFT) &
				XMCDMA_APP0_PKT_HEAD_LEN_MASK;

		if (skb_shinfo(skb)->gso_type == SKB_GSO_UDP_L4)
			cur_p->app1 |= XMCDMA_APP1_TCP_SO_MASK;
		else if (skb_shinfo(skb)->gso_type == SKB_GSO_TCPV4)
			cur_p->app1 |= XMCDMA_APP1_UDP_SO_MASK;

	} else {
		cur_p->app1 |= (XMCDMA_APP1_GSO_PKT_MASK | XMCDMA_APP1_UDP_SO_MASK |
				XMCDMA_APP1_TCP_SO_MASK);
	}
}

int __maybe_unused axienet_eoe_mcdma_gro_q_init(struct net_device *ndev,
						struct axienet_dma_q *q,
						int i)
{
	dma_addr_t mapping;
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 0);
	if (!page) {
		netdev_err(ndev, "page allocation failed\n");
		goto out;
	}
	q->rxq_bd_v[i].page = page;
	mapping = dma_map_page(ndev->dev.parent, page, 0,
			       PAGE_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(ndev->dev.parent, mapping))) {
		netdev_err(ndev, "dma mapping error\n");
		goto free_page;
	}
	q->rxq_bd_v[i].phys = mapping;
	q->rxq_bd_v[i].cntrl = PAGE_SIZE;

	return 0;

free_page:
	__free_pages(q->rxq_bd_v[i].page, 0);
out:
	return -ENOMEM;
}

void __maybe_unused axienet_eoe_mcdma_gro_bd_free(struct net_device *ndev,
						  struct axienet_dma_q *q)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int i;

	if (!q->rxq_bd_v)
		return;

	for (i = 0; i < lp->rx_bd_num; i++) {
		if (q->rxq_bd_v[i].phys) {
			dma_unmap_page(ndev->dev.parent, q->rxq_bd_v[i].phys, PAGE_SIZE,
				       DMA_FROM_DEVICE);
			__free_pages(q->rxq_bd_v[i].page, 0);
		}
	}

	dma_free_coherent(ndev->dev.parent,
			  sizeof(*q->rxq_bd_v) * lp->rx_bd_num,
			  q->rxq_bd_v,
			  q->rx_bd_p);

	q->rxq_bd_v = NULL;
}

int axienet_eoe_recv_gro(struct net_device *ndev, int budget,
			 struct axienet_dma_q *q)
{
	struct axienet_local *lp = netdev_priv(ndev);
	static struct sk_buff *skb[XAE_MAX_QUEUES];
	static u32 rx_data[XAE_MAX_QUEUES];
	u32 length, packets = 0, size = 0;
	unsigned int numbdfree = 0;
	struct aximcdma_bd *cur_p;
	dma_addr_t tail_p = 0;
	struct iphdr *iphdr;
	struct page *page;
	struct udphdr *uh;
	void *page_addr;

	/* Get relevat BD status value */
	rmb();
	cur_p = &q->rxq_bd_v[q->rx_bd_ci];

	while ((numbdfree < budget) &&
	       (cur_p->status & XAXIDMA_BD_STS_COMPLETE_MASK)) {
		tail_p = q->rx_bd_p + sizeof(*q->rxq_bd_v) * q->rx_bd_ci;
		dma_unmap_page(ndev->dev.parent, cur_p->phys, PAGE_SIZE,
			       DMA_FROM_DEVICE);

		length = cur_p->status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;

		page = (struct page *)cur_p->page;
		if (!page) {
			netdev_err(ndev, "Page is Not Defined\n");
			break;
		}

		page_addr = page_address(page);

		rx_data[q->chan_id - 1] += length;

		if (skb[q->chan_id - 1]) {
			skb_add_rx_frag(skb[q->chan_id - 1],
					skb_shinfo(skb[q->chan_id - 1])->nr_frags,
					page, 0, length, rx_data[q->chan_id - 1]);
		}

		if (((cur_p->app0 & XEOE_UDP_GRO_RXSOP_MASK) >> XEOE_UDP_GRO_RXSOP_SHIFT)) {
			/* Allocate new skb and update in BD */
			skb[q->chan_id - 1] = netdev_alloc_skb(ndev, length);
			memcpy(skb[q->chan_id - 1]->data, page_addr, length);
			skb_put(skb[q->chan_id - 1], length);
			put_page(page);
		} else if (((cur_p->app0 & XEOE_UDP_GRO_RXEOP_MASK) >> XEOE_UDP_GRO_RXEOP_SHIFT)) {
			skb_set_network_header(skb[q->chan_id - 1], XEOE_MAC_HEADER_LENGTH);
			iphdr = (struct iphdr *)skb_network_header(skb[q->chan_id - 1]);
			skb_set_transport_header(skb[q->chan_id - 1],
						 iphdr->ihl * 4 + XEOE_MAC_HEADER_LENGTH);
			uh = (struct udphdr *)skb_transport_header(skb[q->chan_id - 1]);

			/* App Fields are in Little Endian Byte Order */
			iphdr->tot_len = htons(cur_p->app1 & XEOE_UDP_GRO_PKT_LEN_MASK);
			iphdr->check = (__force __sum16)htons((cur_p->app1 &
					XEOE_UDP_GRO_RX_CSUM_MASK) >> XEOE_UDP_GRO_RX_CSUM_SHIFT);
			uh->len = htons((cur_p->app1 & XEOE_UDP_GRO_PKT_LEN_MASK) - iphdr->ihl * 4);
			skb[q->chan_id - 1]->protocol = eth_type_trans(skb[q->chan_id - 1], ndev);
			skb[q->chan_id - 1]->ip_summed = CHECKSUM_UNNECESSARY;
			rx_data[q->chan_id - 1] = 0;
			/* This will give SKB to n/w Stack */
			if (skb_shinfo(skb[q->chan_id - 1])->nr_frags <= XEOE_UDP_GRO_MAX_FRAG) {
				netif_receive_skb(skb[q->chan_id - 1]);
				skb[q->chan_id - 1] = NULL;
			}
		}

		size += length;
		packets++;
		/* Ensure that the skb is completely updated
		 * prior to mapping the MCDMA
		 */
		wmb();
		cur_p->status = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		page = alloc_pages(GFP_KERNEL, 0);
		if (!page) {
			netdev_err(ndev, "Page allocation failed\n");
			break;
		}
		cur_p->page = page;
		cur_p->phys = dma_map_page(ndev->dev.parent, page, 0,
					   PAGE_SIZE, DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(ndev->dev.parent, cur_p->phys))) {
			cur_p->phys = 0;
			__free_pages(cur_p->page, 0);
			netdev_err(ndev, "dma mapping failed\n");
			break;
		}

		cur_p->cntrl = PAGE_SIZE;

		if (++q->rx_bd_ci >= lp->rx_bd_num)
			q->rx_bd_ci = 0;

		/* Get relevat BD status value */
		rmb();
		cur_p = &q->rxq_bd_v[q->rx_bd_ci];
		numbdfree++;
	}

	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += size;
	q->rx_packets += packets;
	q->rx_bytes += size;

	if (tail_p) {
		axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id) +
				q->rx_offset, tail_p);
	}

	return numbdfree;
}

int axienet_eoe_add_udp_port_register(struct net_device *ndev, struct ethtool_rx_flow_spec *fs,
				      int chan_id, struct axienet_local *lp)
{
	int udp_port = lp->assigned_rx_port[chan_id - 1];
	int ret = 0;
	u32 val;

	/* Configure Control Register to Disable GRO */
	val = axienet_eoe_ior(lp, XEOE_UDP_GRO_CR_OFFSET(chan_id));
	axienet_eoe_iow(lp, XEOE_UDP_GRO_CR_OFFSET(chan_id), val & (~XEOE_UDP_GRO_ENABLE));

	/* Set 16 Fragments to stitch other than header and add 3 Tuple and Checksum */
	axienet_eoe_iow(lp, XEOE_UDP_GRO_RX_COMMON_CR_OFFSET,
			(XEOE_UDP_GRO_FRAG | XEOE_UDP_GRO_4K_FRAG_SIZE
			| XEOE_UDP_GRO_TUPLE | XEOE_UDP_GRO_CHKSUM));

	/* Configure Port Number */
	axienet_eoe_iow(lp, XEOE_UDP_GRO_PORT__OFFSET(chan_id),
			((udp_port << XEOE_UDP_GRO_DSTPORT_SHIFT) & XEOE_UDP_GRO_DST_PORT_MASK));

	/* Check Status whether GRO Channel is busy */
	/* Wait for GRO Channel busy with timeout */
	ret = readl_poll_timeout(lp->eoe_regs + XEOE_UDP_GRO_SR_OFFSET(chan_id),
				 val, !(val & XEOE_UDP_GRO_BUSY_MASK),
				 10, DELAY_OF_ONE_MILLISEC);
	if (ret) {
		netdev_err(ndev, "GRO Channel %d is busy and can't be configured\n", chan_id);
		return ret;
	}

	/* Configure Control Register to Enable GRO */
	axienet_eoe_iow(lp, XEOE_UDP_GRO_CR_OFFSET(chan_id),
			(((XEOE_UDP_CR_PROTOCOL << XEOE_UDP_GRO_PROTOCOL_SHIFT) &
			XEOE_UDP_GRO_PROTOCOL_MASK) | XEOE_UDP_GRO_ENABLE));

	lp->rx_fs_list.count++;
	return 0;
}

int axienet_eoe_add_flow_filter(struct net_device *ndev, struct ethtool_rxnfc *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct ethtool_rx_flow_spec *fs = &cmd->fs;
	struct ethtool_rx_fs_item *item, *newfs;
	int ret = -EINVAL, chan_id = 0;
	bool added = false;

	newfs = kmalloc(sizeof(*newfs), GFP_KERNEL);
	if (!newfs)
		return -ENOMEM;
	memcpy(&newfs->fs, fs, sizeof(newfs->fs));

	netdev_dbg(ndev,
		   "Adding flow filter entry,type=%u,queue=%u,loc=%u,src=%08X,dst=%08X,ps=%u,pd=%u\n",
		   fs->flow_type, (int)fs->ring_cookie, fs->location,
		   fs->h_u.tcp_ip4_spec.ip4src,
		   fs->h_u.tcp_ip4_spec.ip4dst,
		   be16_to_cpu(fs->h_u.udp_ip4_spec.psrc),
		   be16_to_cpu(fs->h_u.udp_ip4_spec.pdst));

	/* check for Repeated Port Number */
	for (int i = 0; i < XAE_MAX_QUEUES; i++) {
		if (lp->assigned_rx_port[i] == be16_to_cpu(fs->h_u.udp_ip4_spec.pdst)) {
			netdev_err(ndev, "GRO Port %d is Repeated\n", lp->assigned_rx_port[i]);
			ret = -EBUSY;
			goto err_kfree;
		}
	}
	/* find correct place to add in list */
	list_for_each_entry(item, &lp->rx_fs_list.list, list) {
		if (item->fs.location > newfs->fs.location) {
			chan_id = lp->dq[newfs->fs.location]->chan_id;
			lp->assigned_rx_port[newfs->fs.location] =
				be16_to_cpu(fs->h_u.udp_ip4_spec.pdst);
			list_add_tail(&newfs->list, &item->list);
			added = true;
			break;
		} else if (item->fs.location == fs->location) {
			netdev_err(ndev, "Rule not added: location %d not free!\n",
				   fs->location);
			ret = -EBUSY;
			goto err_kfree;
		}
	}
	if (!added) {
		chan_id = lp->dq[newfs->fs.location]->chan_id;
		lp->assigned_rx_port[newfs->fs.location] = be16_to_cpu(fs->h_u.udp_ip4_spec.pdst);
		list_add_tail(&newfs->list, &lp->rx_fs_list.list);
	}

	switch (fs->flow_type) {
	case UDP_V4_FLOW:
		ret = axienet_eoe_add_udp_port_register(ndev, fs, chan_id, lp);
		if (ret)
			goto err_del_list;
		break;
	default:
		netdev_err(ndev, "Invalid flow type\n");
		ret = -EINVAL;
		goto err_del_list;
	}

	return ret;

err_del_list:
	lp->assigned_rx_port[cmd->fs.location] = 0;
	list_del(&newfs->list);
err_kfree:
	kfree(newfs);
	return ret;
}

int axienet_eoe_del_flow_filter(struct net_device *ndev,
				struct ethtool_rxnfc *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct ethtool_rx_fs_item *item;
	struct ethtool_rx_flow_spec *fs;

	list_for_each_entry(item, &lp->rx_fs_list.list, list) {
		if (item->fs.location == cmd->fs.location) {
			/* disable screener regs for the flow entry */
			fs = &item->fs;
			netdev_dbg(ndev,
				   "Deleting flow filter entry,type=%u,queue=%u,loc=%u,src=%08X,dst=%08X,ps=%u,pd=%u\n",
				   fs->flow_type, (int)fs->ring_cookie, fs->location,
				   fs->h_u.udp_ip4_spec.ip4src,
				   fs->h_u.udp_ip4_spec.ip4dst,
				   be16_to_cpu(fs->h_u.tcp_ip4_spec.psrc),
				   be16_to_cpu(fs->h_u.tcp_ip4_spec.pdst));

			lp->assigned_rx_port[cmd->fs.location] = 0;
			list_del(&item->list);
			lp->rx_fs_list.count--;
			kfree(item);
			return 0;
		}
	}

	return -EINVAL;
}

int axienet_eoe_get_flow_entry(struct net_device *ndev,
			       struct ethtool_rxnfc *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct ethtool_rx_fs_item *item;

	list_for_each_entry(item, &lp->rx_fs_list.list, list) {
		if (item->fs.location == cmd->fs.location) {
			memcpy(&cmd->fs, &item->fs, sizeof(cmd->fs));
			cmd->fs.ring_cookie = item->fs.location;
			return 0;
		}
	}
	return -EINVAL;
}

int axienet_eoe_get_all_flow_entries(struct net_device *ndev,
				     struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct ethtool_rx_fs_item *item;
	u32 cnt = 0;

	list_for_each_entry(item, &lp->rx_fs_list.list, list) {
		if (cnt == cmd->rule_cnt)
			return -EMSGSIZE;
		rule_locs[cnt] = item->fs.location;
		cnt++;
	}
	cmd->data = lp->num_rx_queues;
	cmd->rule_cnt = cnt;

	return 0;
}

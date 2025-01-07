/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for Xilinx Ethernet Offload Engine.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 */

#ifndef XILINX_AXIENET_EOE_H
#define XILINX_AXIENET_EOE_H

#include "xilinx_axienet.h"

#define XMCDMA_DFT_RX_THRESHOLD		16

/* UDP Tx : GSO- Generic Segmentation offload APP0/APP1 HW offset */
#define XMCDMA_APP0_TOTAL_PKT_LEN_MASK	GENMASK(23, 0)
#define XMCDMA_APP0_PKT_HEAD_LEN_MASK	GENMASK(31, 24)
#define XMCDMA_APP0_PKT_HEAD_LEN_SHIFT	24

#define XMCDMA_APP1_MSS_SIZE_MASK	GENMASK(29, 16)
#define XMCDMA_APP1_UDP_SO_MASK		BIT(11)
#define XMCDMA_APP1_TCP_SO_MASK		BIT(10)
#define XMCDMA_APP1_CSO_MASK		BIT(9)
#define XMCDMA_APP1_GSO_PKT_MASK	BIT(8)

#define XMCDMA_APP1_MSS_SIZE_SHIFT	16

#define XEOE_UDP_NON_GRO_CHAN_ID	1
#define XEOE_MAC_HEADER_LENGTH		0xe

/* UDP Rx : GRO- Generic Receive Offload HW offset */
#define XEOE_UDP_GRO_RX_COMMON_CR_OFFSET	0x10
#define XEOE_UDP_CR_PROTOCOL			0x11
#define XEOE_UDP_GRO_CR_OFFSET(chan_id)		(0x00 + ((chan_id) - 1) * 0x40)
#define XEOE_UDP_GRO_SR_OFFSET(chan_id)		(0x04 + ((chan_id) - 1) * 0x40)
#define XEOE_UDP_GRO_SRC_IP_OFFSET(chan_id)	(0x08 + ((chan_id) - 1) * 0x40)
#define XEOE_UDP_GRO_DST_IP_OFFSET(chan_id)	(0x0C + ((chan_id) - 1) * 0x40)
#define XEOE_UDP_GRO_PORT__OFFSET(chan_id)	(0x10 + ((chan_id) - 1) * 0x40)
#define XEOE_UDP_GRO_FRAG		0x10000000
#define XEOE_UDP_GRO_TUPLE		BIT(3)
#define XEOE_UDP_GRO_CHKSUM		BIT(1)
#define XEOE_UDP_GRO_BUSY_MASK		BIT(0)
#define XEOE_UDP_GRO_4K_FRAG_SIZE	BIT(20)
#define XEOE_UDP_GRO_ENABLE		BIT(0)

#define XEOE_UDP_GRO_RXSOP_SHIFT	30 /* First GRO Packet */
#define XEOE_UDP_GRO_RXEOP_SHIFT	29 /* Last GRO Packet */

#define XEOE_UDP_GRO_RXSOP_MASK		BIT(30)
#define XEOE_UDP_GRO_RXEOP_MASK		BIT(29)

#define XEOE_UDP_GRO_MAX_FRAG		16

#define XEOE_UDP_GRO_PKT_LEN_MASK	GENMASK(15, 0)
#define XEOE_UDP_GRO_RX_CSUM_MASK	GENMASK(31, 16)
#define XEOE_UDP_GRO_RX_CSUM_SHIFT	16

#define XEOE_UDP_GRO_DSTPORT_SHIFT	16
#define XEOE_UDP_GRO_PROTOCOL_SHIFT	24

#define XEOE_UDP_GRO_DST_PORT_MASK	GENMASK(31, 16)
#define XEOE_UDP_GRO_PROTOCOL_MASK	GENMASK(31, 24)

/* EOE Features */
#define RX_HW_NO_OFFLOAD		BIT(0)
#define RX_HW_CSO			BIT(1)
#define RX_HW_UDP_GRO			BIT(2)

struct ethtool_rx_fs_item {
	struct ethtool_rx_flow_spec fs;
	struct list_head list;
};

#ifdef CONFIG_XILINX_AXI_EOE
int axienet_eoe_probe(struct platform_device *pdev);
void axienet_eoe_config_hwcso(struct net_device *ndev,
			      struct aximcdma_bd *cur_p);
void axienet_eoe_config_hwgso(struct net_device *ndev,
			      struct sk_buff *skb,
			      struct aximcdma_bd *cur_p);
int __maybe_unused axienet_eoe_mcdma_gro_q_init(struct net_device *ndev,
						struct axienet_dma_q *q,
						int i);
void __maybe_unused axienet_eoe_mcdma_gro_bd_free(struct net_device *ndev,
						  struct axienet_dma_q *q);
int axienet_eoe_recv_gro(struct net_device *ndev,
			 int budget,
			 struct axienet_dma_q *q);
int axienet_eoe_add_udp_port_register(struct net_device *ndev,
				      struct ethtool_rx_flow_spec *fs,
				      int chan_id, struct axienet_local *lp);
int axienet_eoe_add_flow_filter(struct net_device *ndev, struct ethtool_rxnfc *cmd);
int axienet_eoe_del_flow_filter(struct net_device *ndev, struct ethtool_rxnfc *cmd);
int axienet_eoe_get_flow_entry(struct net_device *ndev, struct ethtool_rxnfc *cmd);
int axienet_eoe_get_all_flow_entries(struct net_device *ndev,
				     struct ethtool_rxnfc *cmd,
				     u32 *rule_locs);
#else
static inline int axienet_eoe_probe(struct platform_device *pdev)
{
	return -ENODEV;
}

static inline void axienet_eoe_config_hwcso(struct net_device *ndev,
					    struct aximcdma_bd *cur_p)
{ }

static inline void axienet_eoe_config_hwgso(struct net_device *ndev,
					    struct sk_buff *skb,
					    struct aximcdma_bd *cur_p)
{ }

static inline int __maybe_unused axienet_eoe_mcdma_gro_q_init(struct net_device *ndev,
							      struct axienet_dma_q *q,
							      int i)
{
	return 0;
}

static inline void __maybe_unused axienet_eoe_mcdma_gro_bd_free(struct net_device *ndev,
								struct axienet_dma_q *q)
{ }

static inline int axienet_eoe_recv_gro(struct net_device *ndev,
				       int budget,
				       struct axienet_dma_q *q)
{
	return 0;
}
#endif

static inline bool axienet_eoe_is_channel_gro(struct axienet_local *lp,
					      struct axienet_dma_q *q)
{
	return ((lp->eoe_features & RX_HW_UDP_GRO) && q->chan_id != XEOE_UDP_NON_GRO_CHAN_ID);
}

/**
 * axienet_eoe_ior - Memory mapped EOE register read
 * @lp:		Pointer to axienet local structure
 * @offset:	Address offset from the base address of EOE
 *
 * Return: The contents of the EOE register
 *
 * This function returns the contents of the corresponding register.
 */
static inline u32 axienet_eoe_ior(struct axienet_local *lp, off_t offset)
{
	return ioread32(lp->eoe_regs + offset);
}

/**
 * axienet_eoe_iow - Memory mapped EOE register write
 * @lp:		Pointer to axienet local structure
 * @offset:	Address offset from the base address of EOE
 * @value:	Value to be written into the EOE register
 *
 * This function writes the desired value into the corresponding EOE
 * register.
 */
static inline void axienet_eoe_iow(struct axienet_local *lp, off_t offset,
				   u32 value)
{
	iowrite32(value, lp->eoe_regs + offset);
}

#endif /* XILINX_AXIENET_EOE_H */

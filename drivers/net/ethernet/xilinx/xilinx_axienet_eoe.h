/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for Xilinx Ethernet Offload Engine.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 */

#ifndef XILINX_AXIENET_EOE_H
#define XILINX_AXIENET_EOE_H

#include "xilinx_axienet.h"

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

#ifdef CONFIG_XILINX_AXI_EOE
int axienet_eoe_probe(struct platform_device *pdev);
void axienet_eoe_config_hwcso(struct net_device *ndev,
			      struct aximcdma_bd *cur_p);
void axienet_eoe_config_hwgso(struct net_device *ndev,
			      struct sk_buff *skb,
			      struct aximcdma_bd *cur_p);
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
#endif

#endif /* XILINX_AXIENET_EOE_H */

/*
 * Xilinx TSN PTP header
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * Author: Syed S <syeds@xilinx.com>
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

#ifndef _TSN_PTP_H_
#define _TSN_PTP_H_

#define PTP_HW_TSTAMP_SIZE  8   /* 64 bit timestamp */
#define PTP_RX_HWBUF_SIZE   256
#define PTP_RX_FRAME_SIZE   252
#define PTP_HW_TSTAMP_OFFSET (PTP_RX_HWBUF_SIZE - PTP_HW_TSTAMP_SIZE)

#define PTP_MSG_TYPE_MASK				BIT(3)
#define PTP_TYPE_SYNC                                   0x0
#define PTP_TYPE_FOLLOW_UP                              0x8
#define PTP_TYPE_PDELAYREQ                              0x2
#define PTP_TYPE_PDELAYRESP                             0x3
#define PTP_TYPE_PDELAYRESP_FOLLOW_UP                   0xA
#define PTP_TYPE_ANNOUNCE                               0xB
#define PTP_TYPE_SIGNALING                              0xC

#define PTP_TX_CONTROL_OFFSET		0x00012000 /**< Tx PTP Control Reg */
#define PTP_RX_CONTROL_OFFSET		0x00012004 /**< Rx PTP Control Reg */
#define RX_FILTER_CONTROL		0x00012008 /**< Rx Filter Ctrl Reg */

#define PTP_RX_BASE_OFFSET		0x00010000
#define PTP_RX_CONTROL_OFFSET		0x00012004 /**< Rx PTP Control Reg */
#define PTP_RX_PACKET_FIELD_MASK	0x00000F00
#define PTP_RX_PACKET_CLEAR		0x00000001

#define PTP_TX_BUFFER_OFFSET(index)	   (0x00011000 + (index) * 0x100)

#define PTP_TX_SYNC_OFFSET                 0x00011000
#define PTP_TX_FOLLOW_UP_OFFSET            0x00011100
#define PTP_TX_PDELAYREQ_OFFSET            0x00011200
#define PTP_TX_PDELAYRESP_OFFSET           0x00011300
#define PTP_TX_PDELAYRESP_FOLLOW_UP_OFFSET 0x00011400
#define PTP_TX_ANNOUNCE_OFFSET             0x00011500
#define PTP_TX_SIGNALING_OFFSET		   0x00011600
#define PTP_TX_GENERIC_OFFSET		   0x00011700
#define PTP_TX_SEND_SYNC_FRAME_MASK                     0x00000001
#define PTP_TX_SEND_FOLLOWUP_FRAME_MASK                 0x00000002
#define PTP_TX_SEND_PDELAYREQ_FRAME_MASK                0x00000004
#define PTP_TX_SEND_PDELAYRESP_FRAME_MASK               0x00000008
#define PTP_TX_SEND_PDELAYRESPFOLLOWUP_FRAME_MASK       0x00000010
#define PTP_TX_SEND_ANNOUNCE_FRAME_MASK                 0x00000020
#define PTP_TX_SEND_FRAME6_BIT_MASK                     0x00000040
#define PTP_TX_SEND_FRAME7_BIT_MASK                     0x00000080
#define PTP_TX_FRAME_WAITING_MASK			0x0000ff00
#define PTP_TX_FRAME_WAITING_SHIFT			8
#define PTP_TX_WAIT_SYNC_FRAME_MASK                     0x00000100
#define PTP_TX_WAIT_FOLLOWUP_FRAME_MASK                 0x00000200
#define PTP_TX_WAIT_PDELAYREQ_FRAME_MASK                0x00000400
#define PTP_TX_WAIT_PDELAYRESP_FRAME_MASK               0x00000800
#define PTP_TX_WAIT_PDELAYRESPFOLLOWUP_FRAME_MASK       0x00001000
#define PTP_TX_WAIT_ANNOUNCE_FRAME_MASK                 0x00002000
#define PTP_TX_WAIT_FRAME6_BIT_MASK                     0x00004000
#define PTP_TX_WAIT_FRAME7_BIT_MASK                     0x00008000
#define PTP_TX_WAIT_ALL_FRAMES_MASK                     0x0000FF00
#define PTP_TX_PACKET_FIELD_MASK                        0x00070000
#define PTP_TX_PACKET_FIELD_SHIFT                       16

int axienet_ptp_xmit(struct sk_buff *skb, struct net_device *ndev);
irqreturn_t axienet_ptp_rx_irq(int irq, void *_ndev);
irqreturn_t axienet_ptp_tx_irq(int irq, void *_ndev);

#endif

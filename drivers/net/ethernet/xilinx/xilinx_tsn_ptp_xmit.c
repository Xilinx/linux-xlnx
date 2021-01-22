/*
 * Xilinx FPGA Xilinx TSN PTP transfer protocol module.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
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

#include "xilinx_axienet.h"
#include "xilinx_tsn_ptp.h"
#include "xilinx_tsn_timer.h"
#include <linux/ptp_classify.h>

#define PTP_ONE_SECOND            1000000000    /**< Value in ns */

#define msg_type_string(type) \
	((type) == PTP_TYPE_SYNC) ? "SYNC" : \
	((type) == PTP_TYPE_FOLLOW_UP)		  ? "FOLLOW_UP" : \
	((type) == PTP_TYPE_PDELAYREQ)		  ? "PDELAY_REQ" : \
	((type) == PTP_TYPE_PDELAYRESP)		  ? "PDELAY_RESP" : \
	((type) == PTP_TYPE_PDELAYRESP_FOLLOW_UP) ? "PDELAY_RESP_FOLLOW_UP" : \
	((type) == PTP_TYPE_ANNOUNCE)		  ? "ANNOUNCE" : \
	"UNKNOWN"

/**
 * memcpy_fromio_32 - copy ptp buffer from HW
 * @lp:		Pointer to axienet local structure
 * @offset:	Offset in the PTP buffer
 * @data:	Destination buffer
 * @len:	Len to copy
 *
 * This functions copies the data from PTP buffer to destination data buffer
 */
static void memcpy_fromio_32(struct axienet_local *lp,
			     unsigned long offset, u8 *data, size_t len)
{
	while (len >= 4) {
		*(u32 *)data = axienet_ior(lp, offset);
		len -= 4;
		offset += 4;
		data += 4;
	}

	if (len > 0) {
		u32 leftover = axienet_ior(lp, offset);
		u8 *src = (u8 *)&leftover;

		while (len) {
			*data++ = *src++;
			len--;
		}
	}
}

/**
 * memcpy_toio_32 - copy ptp buffer from HW
 * @lp:		Pointer to axienet local structure
 * @offset:	Offset in the PTP buffer
 * @data:	Source data
 * @len:	Len to copy
 *
 * This functions copies the source data to desination ptp buffer
 */
static void memcpy_toio_32(struct axienet_local *lp,
			   unsigned long offset, u8 *data, size_t len)
{
	while (len >= 4) {
		axienet_iow(lp, offset, *(u32 *)data);
		len -= 4;
		offset += 4;
		data += 4;
	}

	if (len > 0) {
		u32 leftover = 0;
		u8 *dest = (u8 *)&leftover;

		while (len) {
			*dest++ = *data++;
			len--;
		}
		axienet_iow(lp, offset, leftover);
	}
}

static int is_sync(struct sk_buff *skb)
{
	u8 *msg_type;

	msg_type = (u8 *)skb->data + ETH_HLEN;

	return (*msg_type & 0xf) == PTP_TYPE_SYNC;
}

/**
 * axienet_ptp_xmit - xmit skb using PTP HW
 * @skb:	sk_buff pointer that contains data to be Txed.
 * @ndev:	Pointer to net_device structure.
 *
 * Return: NETDEV_TX_OK, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is called to transmit a PTP skb. The function uses
 * the free PTP TX buffer entry and sends the frame
 */
int axienet_ptp_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	u8 msg_type;
	struct axienet_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u8 tx_frame_waiting;
	u8 free_index;
	u32 cmd1_field = 0;
	u32 cmd2_field = 0;

	msg_type  = *(u8 *)(skb->data + ETH_HLEN);

	pr_debug("  -->XMIT: protocol: %x message: %s frame_len: %d\n",
		 skb->protocol,
		 msg_type_string(msg_type & 0xf), skb->len);

	tx_frame_waiting =  (axienet_ior(lp, PTP_TX_CONTROL_OFFSET) &
				PTP_TX_FRAME_WAITING_MASK) >>
				PTP_TX_FRAME_WAITING_SHIFT;

	/* we reached last frame */
	if (tx_frame_waiting & (1 << 7)) {
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
		pr_debug("tx_frame_waiting: %d\n", tx_frame_waiting);
		return NETDEV_TX_BUSY;
	}

	/* go to next available slot */
	free_index  = fls(tx_frame_waiting);

	/* write the len */
	if (lp->ptp_ts_type == HWTSTAMP_TX_ONESTEP_SYNC &&
	    is_sync(skb)) {
		/* enable 1STEP SYNC */
		cmd1_field |= PTP_TX_CMD_1STEP_SHIFT;
		cmd2_field |= PTP_TOD_FIELD_OFFSET;
	}

	cmd1_field |= skb->len;

	axienet_iow(lp, PTP_TX_BUFFER_OFFSET(free_index), cmd1_field);
	axienet_iow(lp, PTP_TX_BUFFER_OFFSET(free_index) +
			PTP_TX_BUFFER_CMD2_FIELD, cmd2_field);
	memcpy_toio_32(lp,
		       (PTP_TX_BUFFER_OFFSET(free_index) +
			PTP_TX_CMD_FIELD_LEN),
		       skb->data, skb->len);

	/* send the frame */
	axienet_iow(lp, PTP_TX_CONTROL_OFFSET, (1 << free_index));

	if (lp->ptp_ts_type != HWTSTAMP_TX_ONESTEP_SYNC ||
	    (!is_sync(skb))) {
		spin_lock_irqsave(&lp->ptp_tx_lock, flags);
		skb->cb[0] = free_index;
		skb_queue_tail(&lp->ptp_txq, skb);

		if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

		skb_tx_timestamp(skb);
		spin_unlock_irqrestore(&lp->ptp_tx_lock, flags);
	}
	return NETDEV_TX_OK;
}

/**
 * axienet_set_timestamp - timestamp skb with HW timestamp
 * @lp:		Pointer to axienet local structure
 * @hwtstamps:  Pointer to skb timestamp structure
 * @offset:	offset of the timestamp in the PTP buffer
 *
 * Return:	None.
 *
 */
static void axienet_set_timestamp(struct axienet_local *lp,
				  struct skb_shared_hwtstamps *hwtstamps,
				  unsigned int offset)
{
	u32 captured_ns;
	u32 captured_sec;

	captured_ns = axienet_ior(lp, offset + 4);
	captured_sec = axienet_ior(lp, offset);

	/* Upper 32 bits contain s, lower 32 bits contain ns. */
	hwtstamps->hwtstamp = ktime_set(captured_sec,
						captured_ns);
}

/**
 * axienet_ptp_recv - receive ptp buffer in skb from HW
 * @ndev:	Pointer to net_device structure.
 *
 * This function is called from the ptp rx isr. It allocates skb, and
 * copies the ptp rx buffer data to it and calls netif_rx for further
 * processing.
 *
 */
static void axienet_ptp_recv(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	unsigned long ptp_frame_base_addr = 0;
	struct sk_buff *skb;
	u16 msg_len;
	u8 msg_type;
	u32 bytes = 0;
	u32 packets = 0;

	pr_debug("%s:\n ", __func__);

	while (((lp->ptp_rx_hw_pointer & 0xf) !=
		 (lp->ptp_rx_sw_pointer & 0xf))) {
		skb = netdev_alloc_skb(ndev, PTP_RX_FRAME_SIZE);

		lp->ptp_rx_sw_pointer += 1;

		ptp_frame_base_addr = PTP_RX_BASE_OFFSET +
				   ((lp->ptp_rx_sw_pointer & 0xf) *
							PTP_RX_HWBUF_SIZE);

		memset(skb->data, 0x0, PTP_RX_FRAME_SIZE);

		memcpy_fromio_32(lp, ptp_frame_base_addr, skb->data,
				 PTP_RX_FRAME_SIZE);

		msg_type  = *(u8 *)(skb->data + ETH_HLEN) & 0xf;
		msg_len  = *(u16 *)(skb->data + ETH_HLEN + 2);

		skb_put(skb, ntohs(msg_len) + ETH_HLEN);

		bytes += skb->len;
		packets++;

		skb->protocol = eth_type_trans(skb, ndev);
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		pr_debug("  -->RECV: protocol: %x message: %s frame_len: %d\n",
			 skb->protocol, msg_type_string(msg_type & 0xf),
			 skb->len);
		/* timestamp only event messages */
		if (!(msg_type & PTP_MSG_TYPE_MASK)) {
			axienet_set_timestamp(lp, skb_hwtstamps(skb),
					      (ptp_frame_base_addr +
					      PTP_HW_TSTAMP_OFFSET));
		}

		netif_rx(skb);
	}
	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += bytes;
}

/**
 * axienet_ptp_rx_irq - PTP RX ISR handler
 * @irq:		irq number
 * @_ndev:	net_device pointer
 *
 * Return:	IRQ_HANDLED for all cases.
 */
irqreturn_t axienet_ptp_rx_irq(int irq, void *_ndev)
{
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);

	pr_debug("%s: received\n ", __func__);
	lp->ptp_rx_hw_pointer = (axienet_ior(lp, PTP_RX_CONTROL_OFFSET)
					& PTP_RX_PACKET_FIELD_MASK)  >> 8;

	axienet_ptp_recv(ndev);

	return IRQ_HANDLED;
}

/**
 * axienet_tx_tstamp - timestamp skb on trasmit path
 * @work:	Pointer to work_struct structure
 *
 * This adds TX timestamp to skb
 */
void axienet_tx_tstamp(struct work_struct *work)
{
	struct axienet_local *lp = container_of(work, struct axienet_local,
			tx_tstamp_work);
	struct net_device *ndev = lp->ndev;
	struct skb_shared_hwtstamps hwtstamps;
	struct sk_buff *skb;
	unsigned long ts_reg_offset;
	unsigned long flags;
	u8 tx_packet;
	u8 index;
	u32 bytes = 0;
	u32 packets = 0;

	memset(&hwtstamps, 0, sizeof(struct skb_shared_hwtstamps));

	spin_lock_irqsave(&lp->ptp_tx_lock, flags);

	tx_packet =  (axienet_ior(lp, PTP_TX_CONTROL_OFFSET) &
				PTP_TX_PACKET_FIELD_MASK) >>
				PTP_TX_PACKET_FIELD_SHIFT;

	while ((skb = __skb_dequeue(&lp->ptp_txq)) != NULL) {
		index = skb->cb[0];

		/* dequeued packet yet to be xmited? */
		if (index > tx_packet) {
			/* enqueue it back and break */
			skb_queue_tail(&lp->ptp_txq, skb);
			break;
		}
		/* time stamp reg offset */
		ts_reg_offset = PTP_TX_BUFFER_OFFSET(index) +
					PTP_HW_TSTAMP_OFFSET;

		if (skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS) {
			axienet_set_timestamp(lp, &hwtstamps, ts_reg_offset);
			skb_tstamp_tx(skb, &hwtstamps);
		}

		bytes += skb->len;
		packets++;
		dev_kfree_skb_any(skb);
	}
	ndev->stats.tx_packets += packets;
	ndev->stats.tx_bytes += bytes;

	spin_unlock_irqrestore(&lp->ptp_tx_lock, flags);
}

/**
 * axienet_ptp_tx_irq - PTP TX irq handler
 * @irq:		irq number
 * @_ndev:	net_device pointer
 *
 * Return:	IRQ_HANDLED for all cases.
 *
 */
irqreturn_t axienet_ptp_tx_irq(int irq, void *_ndev)
{
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);

	pr_debug("%s: got tx interrupt\n", __func__);

	/* read ctrl register to clear the interrupt */
	axienet_ior(lp, PTP_TX_CONTROL_OFFSET);

	schedule_work(&lp->tx_tstamp_work);

	netif_wake_queue(ndev);

	return IRQ_HANDLED;
}

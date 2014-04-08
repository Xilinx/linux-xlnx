/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"
#include "fwil_types.h"
#include "p2p.h"
#include "wl_cfg80211.h"
#include "fwil.h"
#include "fwsignal.h"

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11 wireless LAN fullmac driver.");
MODULE_LICENSE("Dual BSD/GPL");

#define MAX_WAIT_FOR_8021X_TX		50	/* msecs */

/* AMPDU rx reordering definitions */
#define BRCMF_RXREORDER_FLOWID_OFFSET		0
#define BRCMF_RXREORDER_MAXIDX_OFFSET		2
#define BRCMF_RXREORDER_FLAGS_OFFSET		4
#define BRCMF_RXREORDER_CURIDX_OFFSET		6
#define BRCMF_RXREORDER_EXPIDX_OFFSET		8

#define BRCMF_RXREORDER_DEL_FLOW		0x01
#define BRCMF_RXREORDER_FLUSH_ALL		0x02
#define BRCMF_RXREORDER_CURIDX_VALID		0x04
#define BRCMF_RXREORDER_EXPIDX_VALID		0x08
#define BRCMF_RXREORDER_NEW_HOLE		0x10

/* Error bits */
int brcmf_msg_level;
module_param_named(debug, brcmf_msg_level, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(debug, "level of debug output");

/* P2P0 enable */
static int brcmf_p2p_enable;
#ifdef CONFIG_BRCMDBG
module_param_named(p2pon, brcmf_p2p_enable, int, 0);
MODULE_PARM_DESC(p2pon, "enable p2p management functionality");
#endif

char *brcmf_ifname(struct brcmf_pub *drvr, int ifidx)
{
	if (ifidx < 0 || ifidx >= BRCMF_MAX_IFS) {
		brcmf_err("ifidx %d out of range\n", ifidx);
		return "<if_bad>";
	}

	if (drvr->iflist[ifidx] == NULL) {
		brcmf_err("null i/f %d\n", ifidx);
		return "<if_null>";
	}

	if (drvr->iflist[ifidx]->ndev)
		return drvr->iflist[ifidx]->ndev->name;

	return "<if_none>";
}

static void _brcmf_set_multicast_list(struct work_struct *work)
{
	struct brcmf_if *ifp;
	struct net_device *ndev;
	struct netdev_hw_addr *ha;
	u32 cmd_value, cnt;
	__le32 cnt_le;
	char *buf, *bufp;
	u32 buflen;
	s32 err;

	ifp = container_of(work, struct brcmf_if, multicast_work);

	brcmf_dbg(TRACE, "Enter, idx=%d\n", ifp->bssidx);

	ndev = ifp->ndev;

	/* Determine initial value of allmulti flag */
	cmd_value = (ndev->flags & IFF_ALLMULTI) ? true : false;

	/* Send down the multicast list first. */
	cnt = netdev_mc_count(ndev);
	buflen = sizeof(cnt) + (cnt * ETH_ALEN);
	buf = kmalloc(buflen, GFP_ATOMIC);
	if (!buf)
		return;
	bufp = buf;

	cnt_le = cpu_to_le32(cnt);
	memcpy(bufp, &cnt_le, sizeof(cnt_le));
	bufp += sizeof(cnt_le);

	netdev_for_each_mc_addr(ha, ndev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETH_ALEN);
		bufp += ETH_ALEN;
		cnt--;
	}

	err = brcmf_fil_iovar_data_set(ifp, "mcast_list", buf, buflen);
	if (err < 0) {
		brcmf_err("Setting mcast_list failed, %d\n", err);
		cmd_value = cnt ? true : cmd_value;
	}

	kfree(buf);

	/*
	 * Now send the allmulti setting.  This is based on the setting in the
	 * net_device flags, but might be modified above to be turned on if we
	 * were trying to set some addresses and dongle rejected it...
	 */
	err = brcmf_fil_iovar_int_set(ifp, "allmulti", cmd_value);
	if (err < 0)
		brcmf_err("Setting allmulti failed, %d\n", err);

	/*Finally, pick up the PROMISC flag */
	cmd_value = (ndev->flags & IFF_PROMISC) ? true : false;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PROMISC, cmd_value);
	if (err < 0)
		brcmf_err("Setting BRCMF_C_SET_PROMISC failed, %d\n",
			  err);
}

static void
_brcmf_set_mac_address(struct work_struct *work)
{
	struct brcmf_if *ifp;
	s32 err;

	ifp = container_of(work, struct brcmf_if, setmacaddr_work);

	brcmf_dbg(TRACE, "Enter, idx=%d\n", ifp->bssidx);

	err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", ifp->mac_addr,
				       ETH_ALEN);
	if (err < 0) {
		brcmf_err("Setting cur_etheraddr failed, %d\n", err);
	} else {
		brcmf_dbg(TRACE, "MAC address updated to %pM\n",
			  ifp->mac_addr);
		memcpy(ifp->ndev->dev_addr, ifp->mac_addr, ETH_ALEN);
	}
}

static int brcmf_netdev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct sockaddr *sa = (struct sockaddr *)addr;

	memcpy(&ifp->mac_addr, sa->sa_data, ETH_ALEN);
	schedule_work(&ifp->setmacaddr_work);
	return 0;
}

static void brcmf_netdev_set_multicast_list(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	schedule_work(&ifp->multicast_work);
}

static netdev_tx_t brcmf_netdev_start_xmit(struct sk_buff *skb,
					   struct net_device *ndev)
{
	int ret;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct ethhdr *eh;

	brcmf_dbg(DATA, "Enter, idx=%d\n", ifp->bssidx);

	/* Can the device send data? */
	if (drvr->bus_if->state != BRCMF_BUS_DATA) {
		brcmf_err("xmit rejected state=%d\n", drvr->bus_if->state);
		netif_stop_queue(ndev);
		dev_kfree_skb(skb);
		ret = -ENODEV;
		goto done;
	}

	if (!drvr->iflist[ifp->bssidx]) {
		brcmf_err("bad ifidx %d\n", ifp->bssidx);
		netif_stop_queue(ndev);
		dev_kfree_skb(skb);
		ret = -ENODEV;
		goto done;
	}

	/* Make sure there's enough room for any header */
	if (skb_headroom(skb) < drvr->hdrlen) {
		struct sk_buff *skb2;

		brcmf_dbg(INFO, "%s: insufficient headroom\n",
			  brcmf_ifname(drvr, ifp->bssidx));
		drvr->bus_if->tx_realloc++;
		skb2 = skb_realloc_headroom(skb, drvr->hdrlen);
		dev_kfree_skb(skb);
		skb = skb2;
		if (skb == NULL) {
			brcmf_err("%s: skb_realloc_headroom failed\n",
				  brcmf_ifname(drvr, ifp->bssidx));
			ret = -ENOMEM;
			goto done;
		}
	}

	/* validate length for ether packet */
	if (skb->len < sizeof(*eh)) {
		ret = -EINVAL;
		dev_kfree_skb(skb);
		goto done;
	}

	ret = brcmf_fws_process_skb(ifp, skb);

done:
	if (ret) {
		ifp->stats.tx_dropped++;
	} else {
		ifp->stats.tx_packets++;
		ifp->stats.tx_bytes += skb->len;
	}

	/* Return ok: we always eat the packet */
	return NETDEV_TX_OK;
}

void brcmf_txflowblock_if(struct brcmf_if *ifp,
			  enum brcmf_netif_stop_reason reason, bool state)
{
	unsigned long flags;

	if (!ifp || !ifp->ndev)
		return;

	brcmf_dbg(TRACE, "enter: idx=%d stop=0x%X reason=%d state=%d\n",
		  ifp->bssidx, ifp->netif_stop, reason, state);

	spin_lock_irqsave(&ifp->netif_stop_lock, flags);
	if (state) {
		if (!ifp->netif_stop)
			netif_stop_queue(ifp->ndev);
		ifp->netif_stop |= reason;
	} else {
		ifp->netif_stop &= ~reason;
		if (!ifp->netif_stop)
			netif_wake_queue(ifp->ndev);
	}
	spin_unlock_irqrestore(&ifp->netif_stop_lock, flags);
}

void brcmf_txflowblock(struct device *dev, bool state)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	brcmf_fws_bus_blocked(drvr, state);
}

static void brcmf_netif_rx(struct brcmf_if *ifp, struct sk_buff *skb)
{
	skb->dev = ifp->ndev;
	skb->protocol = eth_type_trans(skb, skb->dev);

	if (skb->pkt_type == PACKET_MULTICAST)
		ifp->stats.multicast++;

	/* Process special event packets */
	brcmf_fweh_process_skb(ifp->drvr, skb);

	if (!(ifp->ndev->flags & IFF_UP)) {
		brcmu_pkt_buf_free_skb(skb);
		return;
	}

	ifp->stats.rx_bytes += skb->len;
	ifp->stats.rx_packets++;

	brcmf_dbg(DATA, "rx proto=0x%X\n", ntohs(skb->protocol));
	if (in_interrupt())
		netif_rx(skb);
	else
		/* If the receive is not processed inside an ISR,
		 * the softirqd must be woken explicitly to service
		 * the NET_RX_SOFTIRQ.  This is handled by netif_rx_ni().
		 */
		netif_rx_ni(skb);
}

static void brcmf_rxreorder_get_skb_list(struct brcmf_ampdu_rx_reorder *rfi,
					 u8 start, u8 end,
					 struct sk_buff_head *skb_list)
{
	/* initialize return list */
	__skb_queue_head_init(skb_list);

	if (rfi->pend_pkts == 0) {
		brcmf_dbg(INFO, "no packets in reorder queue\n");
		return;
	}

	do {
		if (rfi->pktslots[start]) {
			__skb_queue_tail(skb_list, rfi->pktslots[start]);
			rfi->pktslots[start] = NULL;
		}
		start++;
		if (start > rfi->max_idx)
			start = 0;
	} while (start != end);
	rfi->pend_pkts -= skb_queue_len(skb_list);
}

static void brcmf_rxreorder_process_info(struct brcmf_if *ifp, u8 *reorder_data,
					 struct sk_buff *pkt)
{
	u8 flow_id, max_idx, cur_idx, exp_idx, end_idx;
	struct brcmf_ampdu_rx_reorder *rfi;
	struct sk_buff_head reorder_list;
	struct sk_buff *pnext;
	u8 flags;
	u32 buf_size;

	flow_id = reorder_data[BRCMF_RXREORDER_FLOWID_OFFSET];
	flags = reorder_data[BRCMF_RXREORDER_FLAGS_OFFSET];

	/* validate flags and flow id */
	if (flags == 0xFF) {
		brcmf_err("invalid flags...so ignore this packet\n");
		brcmf_netif_rx(ifp, pkt);
		return;
	}

	rfi = ifp->drvr->reorder_flows[flow_id];
	if (flags & BRCMF_RXREORDER_DEL_FLOW) {
		brcmf_dbg(INFO, "flow-%d: delete\n",
			  flow_id);

		if (rfi == NULL) {
			brcmf_dbg(INFO, "received flags to cleanup, but no flow (%d) yet\n",
				  flow_id);
			brcmf_netif_rx(ifp, pkt);
			return;
		}

		brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx, rfi->exp_idx,
					     &reorder_list);
		/* add the last packet */
		__skb_queue_tail(&reorder_list, pkt);
		kfree(rfi);
		ifp->drvr->reorder_flows[flow_id] = NULL;
		goto netif_rx;
	}
	/* from here on we need a flow reorder instance */
	if (rfi == NULL) {
		buf_size = sizeof(*rfi);
		max_idx = reorder_data[BRCMF_RXREORDER_MAXIDX_OFFSET];

		buf_size += (max_idx + 1) * sizeof(pkt);

		/* allocate space for flow reorder info */
		brcmf_dbg(INFO, "flow-%d: start, maxidx %d\n",
			  flow_id, max_idx);
		rfi = kzalloc(buf_size, GFP_ATOMIC);
		if (rfi == NULL) {
			brcmf_err("failed to alloc buffer\n");
			brcmf_netif_rx(ifp, pkt);
			return;
		}

		ifp->drvr->reorder_flows[flow_id] = rfi;
		rfi->pktslots = (struct sk_buff **)(rfi+1);
		rfi->max_idx = max_idx;
	}
	if (flags & BRCMF_RXREORDER_NEW_HOLE)  {
		if (rfi->pend_pkts) {
			brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx,
						     rfi->exp_idx,
						     &reorder_list);
			WARN_ON(rfi->pend_pkts);
		} else {
			__skb_queue_head_init(&reorder_list);
		}
		rfi->cur_idx = reorder_data[BRCMF_RXREORDER_CURIDX_OFFSET];
		rfi->exp_idx = reorder_data[BRCMF_RXREORDER_EXPIDX_OFFSET];
		rfi->max_idx = reorder_data[BRCMF_RXREORDER_MAXIDX_OFFSET];
		rfi->pktslots[rfi->cur_idx] = pkt;
		rfi->pend_pkts++;
		brcmf_dbg(DATA, "flow-%d: new hole %d (%d), pending %d\n",
			  flow_id, rfi->cur_idx, rfi->exp_idx, rfi->pend_pkts);
	} else if (flags & BRCMF_RXREORDER_CURIDX_VALID) {
		cur_idx = reorder_data[BRCMF_RXREORDER_CURIDX_OFFSET];
		exp_idx = reorder_data[BRCMF_RXREORDER_EXPIDX_OFFSET];

		if ((exp_idx == rfi->exp_idx) && (cur_idx != rfi->exp_idx)) {
			/* still in the current hole */
			/* enqueue the current on the buffer chain */
			if (rfi->pktslots[cur_idx] != NULL) {
				brcmf_dbg(INFO, "HOLE: ERROR buffer pending..free it\n");
				brcmu_pkt_buf_free_skb(rfi->pktslots[cur_idx]);
				rfi->pktslots[cur_idx] = NULL;
			}
			rfi->pktslots[cur_idx] = pkt;
			rfi->pend_pkts++;
			rfi->cur_idx = cur_idx;
			brcmf_dbg(DATA, "flow-%d: store pkt %d (%d), pending %d\n",
				  flow_id, cur_idx, exp_idx, rfi->pend_pkts);

			/* can return now as there is no reorder
			 * list to process.
			 */
			return;
		}
		if (rfi->exp_idx == cur_idx) {
			if (rfi->pktslots[cur_idx] != NULL) {
				brcmf_dbg(INFO, "error buffer pending..free it\n");
				brcmu_pkt_buf_free_skb(rfi->pktslots[cur_idx]);
				rfi->pktslots[cur_idx] = NULL;
			}
			rfi->pktslots[cur_idx] = pkt;
			rfi->pend_pkts++;

			/* got the expected one. flush from current to expected
			 * and update expected
			 */
			brcmf_dbg(DATA, "flow-%d: expected %d (%d), pending %d\n",
				  flow_id, cur_idx, exp_idx, rfi->pend_pkts);

			rfi->cur_idx = cur_idx;
			rfi->exp_idx = exp_idx;

			brcmf_rxreorder_get_skb_list(rfi, cur_idx, exp_idx,
						     &reorder_list);
			brcmf_dbg(DATA, "flow-%d: freeing buffers %d, pending %d\n",
				  flow_id, skb_queue_len(&reorder_list),
				  rfi->pend_pkts);
		} else {
			u8 end_idx;

			brcmf_dbg(DATA, "flow-%d (0x%x): both moved, old %d/%d, new %d/%d\n",
				  flow_id, flags, rfi->cur_idx, rfi->exp_idx,
				  cur_idx, exp_idx);
			if (flags & BRCMF_RXREORDER_FLUSH_ALL)
				end_idx = rfi->exp_idx;
			else
				end_idx = exp_idx;

			/* flush pkts first */
			brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx, end_idx,
						     &reorder_list);

			if (exp_idx == ((cur_idx + 1) % (rfi->max_idx + 1))) {
				__skb_queue_tail(&reorder_list, pkt);
			} else {
				rfi->pktslots[cur_idx] = pkt;
				rfi->pend_pkts++;
			}
			rfi->exp_idx = exp_idx;
			rfi->cur_idx = cur_idx;
		}
	} else {
		/* explicity window move updating the expected index */
		exp_idx = reorder_data[BRCMF_RXREORDER_EXPIDX_OFFSET];

		brcmf_dbg(DATA, "flow-%d (0x%x): change expected: %d -> %d\n",
			  flow_id, flags, rfi->exp_idx, exp_idx);
		if (flags & BRCMF_RXREORDER_FLUSH_ALL)
			end_idx =  rfi->exp_idx;
		else
			end_idx =  exp_idx;

		brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx, end_idx,
					     &reorder_list);
		__skb_queue_tail(&reorder_list, pkt);
		/* set the new expected idx */
		rfi->exp_idx = exp_idx;
	}
netif_rx:
	skb_queue_walk_safe(&reorder_list, pkt, pnext) {
		__skb_unlink(pkt, &reorder_list);
		brcmf_netif_rx(ifp, pkt);
	}
}

void brcmf_rx_frame(struct device *dev, struct sk_buff *skb)
{
	struct brcmf_if *ifp;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_skb_reorder_data *rd;
	u8 ifidx;
	int ret;

	brcmf_dbg(DATA, "Enter: %s: rxp=%p\n", dev_name(dev), skb);

	/* process and remove protocol-specific header */
	ret = brcmf_proto_hdrpull(drvr, true, &ifidx, skb);
	ifp = drvr->iflist[ifidx];

	if (ret || !ifp || !ifp->ndev) {
		if ((ret != -ENODATA) && ifp)
			ifp->stats.rx_errors++;
		brcmu_pkt_buf_free_skb(skb);
		return;
	}

	rd = (struct brcmf_skb_reorder_data *)skb->cb;
	if (rd->reorder)
		brcmf_rxreorder_process_info(ifp, rd->reorder, skb);
	else
		brcmf_netif_rx(ifp, skb);
}

void brcmf_txfinalize(struct brcmf_pub *drvr, struct sk_buff *txp,
		      bool success)
{
	struct brcmf_if *ifp;
	struct ethhdr *eh;
	u8 ifidx;
	u16 type;
	int res;

	res = brcmf_proto_hdrpull(drvr, false, &ifidx, txp);

	ifp = drvr->iflist[ifidx];
	if (!ifp)
		goto done;

	if (res == 0) {
		eh = (struct ethhdr *)(txp->data);
		type = ntohs(eh->h_proto);

		if (type == ETH_P_PAE) {
			atomic_dec(&ifp->pend_8021x_cnt);
			if (waitqueue_active(&ifp->pend_8021x_wait))
				wake_up(&ifp->pend_8021x_wait);
		}
	}
	if (!success)
		ifp->stats.tx_errors++;
done:
	brcmu_pkt_buf_free_skb(txp);
}

void brcmf_txcomplete(struct device *dev, struct sk_buff *txp, bool success)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	/* await txstatus signal for firmware if active */
	if (brcmf_fws_fc_active(drvr->fws)) {
		if (!success)
			brcmf_fws_bustxfail(drvr->fws, txp);
	} else {
		brcmf_txfinalize(drvr, txp, success);
	}
}

static struct net_device_stats *brcmf_netdev_get_stats(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter, idx=%d\n", ifp->bssidx);

	return &ifp->stats;
}

/*
 * Set current toe component enables in toe_ol iovar,
 * and set toe global enable iovar
 */
static int brcmf_toe_set(struct brcmf_if *ifp, u32 toe_ol)
{
	s32 err;

	err = brcmf_fil_iovar_int_set(ifp, "toe_ol", toe_ol);
	if (err < 0) {
		brcmf_err("Setting toe_ol failed, %d\n", err);
		return err;
	}

	err = brcmf_fil_iovar_int_set(ifp, "toe", (toe_ol != 0));
	if (err < 0)
		brcmf_err("Setting toe failed, %d\n", err);

	return err;

}

static void brcmf_ethtool_get_drvinfo(struct net_device *ndev,
				    struct ethtool_drvinfo *info)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	snprintf(info->version, sizeof(info->version), "%lu",
		 drvr->drv_version);
	strlcpy(info->bus_info, dev_name(drvr->bus_if->dev),
		sizeof(info->bus_info));
}

static const struct ethtool_ops brcmf_ethtool_ops = {
	.get_drvinfo = brcmf_ethtool_get_drvinfo,
};

static int brcmf_ethtool(struct brcmf_if *ifp, void __user *uaddr)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct ethtool_drvinfo info;
	char drvname[sizeof(info.driver)];
	u32 cmd;
	struct ethtool_value edata;
	u32 toe_cmpnt, csum_dir;
	int ret;

	brcmf_dbg(TRACE, "Enter, idx=%d\n", ifp->bssidx);

	/* all ethtool calls start with a cmd word */
	if (copy_from_user(&cmd, uaddr, sizeof(u32)))
		return -EFAULT;

	switch (cmd) {
	case ETHTOOL_GDRVINFO:
		/* Copy out any request driver name */
		if (copy_from_user(&info, uaddr, sizeof(info)))
			return -EFAULT;
		strncpy(drvname, info.driver, sizeof(info.driver));
		drvname[sizeof(info.driver) - 1] = '\0';

		/* clear struct for return */
		memset(&info, 0, sizeof(info));
		info.cmd = cmd;

		/* if requested, identify ourselves */
		if (strcmp(drvname, "?dhd") == 0) {
			sprintf(info.driver, "dhd");
			strcpy(info.version, BRCMF_VERSION_STR);
		}
		/* report dongle driver type */
		else
			sprintf(info.driver, "wl");

		sprintf(info.version, "%lu", drvr->drv_version);
		if (copy_to_user(uaddr, &info, sizeof(info)))
			return -EFAULT;
		brcmf_dbg(TRACE, "given %*s, returning %s\n",
			  (int)sizeof(drvname), drvname, info.driver);
		break;

		/* Get toe offload components from dongle */
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
		ret = brcmf_fil_iovar_int_get(ifp, "toe_ol", &toe_cmpnt);
		if (ret < 0)
			return ret;

		csum_dir =
		    (cmd == ETHTOOL_GTXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		edata.cmd = cmd;
		edata.data = (toe_cmpnt & csum_dir) ? 1 : 0;

		if (copy_to_user(uaddr, &edata, sizeof(edata)))
			return -EFAULT;
		break;

		/* Set toe offload components in dongle */
	case ETHTOOL_SRXCSUM:
	case ETHTOOL_STXCSUM:
		if (copy_from_user(&edata, uaddr, sizeof(edata)))
			return -EFAULT;

		/* Read the current settings, update and write back */
		ret = brcmf_fil_iovar_int_get(ifp, "toe_ol", &toe_cmpnt);
		if (ret < 0)
			return ret;

		csum_dir =
		    (cmd == ETHTOOL_STXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		if (edata.data != 0)
			toe_cmpnt |= csum_dir;
		else
			toe_cmpnt &= ~csum_dir;

		ret = brcmf_toe_set(ifp, toe_cmpnt);
		if (ret < 0)
			return ret;

		/* If setting TX checksum mode, tell Linux the new mode */
		if (cmd == ETHTOOL_STXCSUM) {
			if (edata.data)
				ifp->ndev->features |= NETIF_F_IP_CSUM;
			else
				ifp->ndev->features &= ~NETIF_F_IP_CSUM;
		}

		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int brcmf_netdev_ioctl_entry(struct net_device *ndev, struct ifreq *ifr,
				    int cmd)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;

	brcmf_dbg(TRACE, "Enter, idx=%d, cmd=0x%04x\n", ifp->bssidx, cmd);

	if (!drvr->iflist[ifp->bssidx])
		return -1;

	if (cmd == SIOCETHTOOL)
		return brcmf_ethtool(ifp, ifr->ifr_data);

	return -EOPNOTSUPP;
}

static int brcmf_netdev_stop(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter, idx=%d\n", ifp->bssidx);

	brcmf_cfg80211_down(ndev);

	/* Set state and stop OS transmissions */
	netif_stop_queue(ndev);

	return 0;
}

static int brcmf_netdev_open(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_bus *bus_if = drvr->bus_if;
	u32 toe_ol;
	s32 ret = 0;

	brcmf_dbg(TRACE, "Enter, idx=%d\n", ifp->bssidx);

	/* If bus is not ready, can't continue */
	if (bus_if->state != BRCMF_BUS_DATA) {
		brcmf_err("failed bus is not ready\n");
		return -EAGAIN;
	}

	atomic_set(&ifp->pend_8021x_cnt, 0);

	/* Get current TOE mode from dongle */
	if (brcmf_fil_iovar_int_get(ifp, "toe_ol", &toe_ol) >= 0
	    && (toe_ol & TOE_TX_CSUM_OL) != 0)
		ndev->features |= NETIF_F_IP_CSUM;
	else
		ndev->features &= ~NETIF_F_IP_CSUM;

	/* Allow transmit calls */
	netif_start_queue(ndev);
	if (brcmf_cfg80211_up(ndev)) {
		brcmf_err("failed to bring up cfg80211\n");
		return -1;
	}

	return ret;
}

static const struct net_device_ops brcmf_netdev_ops_pri = {
	.ndo_open = brcmf_netdev_open,
	.ndo_stop = brcmf_netdev_stop,
	.ndo_get_stats = brcmf_netdev_get_stats,
	.ndo_do_ioctl = brcmf_netdev_ioctl_entry,
	.ndo_start_xmit = brcmf_netdev_start_xmit,
	.ndo_set_mac_address = brcmf_netdev_set_mac_address,
	.ndo_set_rx_mode = brcmf_netdev_set_multicast_list
};

int brcmf_net_attach(struct brcmf_if *ifp, bool rtnl_locked)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct net_device *ndev;
	s32 err;

	brcmf_dbg(TRACE, "Enter, idx=%d mac=%pM\n", ifp->bssidx,
		  ifp->mac_addr);
	ndev = ifp->ndev;

	/* set appropriate operations */
	ndev->netdev_ops = &brcmf_netdev_ops_pri;

	ndev->hard_header_len += drvr->hdrlen;
	ndev->ethtool_ops = &brcmf_ethtool_ops;

	drvr->rxsz = ndev->mtu + ndev->hard_header_len +
			      drvr->hdrlen;

	/* set the mac address */
	memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);

	INIT_WORK(&ifp->setmacaddr_work, _brcmf_set_mac_address);
	INIT_WORK(&ifp->multicast_work, _brcmf_set_multicast_list);

	if (rtnl_locked)
		err = register_netdevice(ndev);
	else
		err = register_netdev(ndev);
	if (err != 0) {
		brcmf_err("couldn't register the net device\n");
		goto fail;
	}

	brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

	ndev->destructor = free_netdev;
	return 0;

fail:
	drvr->iflist[ifp->bssidx] = NULL;
	ndev->netdev_ops = NULL;
	free_netdev(ndev);
	return -EBADE;
}

static int brcmf_net_p2p_open(struct net_device *ndev)
{
	brcmf_dbg(TRACE, "Enter\n");

	return brcmf_cfg80211_up(ndev);
}

static int brcmf_net_p2p_stop(struct net_device *ndev)
{
	brcmf_dbg(TRACE, "Enter\n");

	return brcmf_cfg80211_down(ndev);
}

static int brcmf_net_p2p_do_ioctl(struct net_device *ndev,
				  struct ifreq *ifr, int cmd)
{
	brcmf_dbg(TRACE, "Enter\n");
	return 0;
}

static netdev_tx_t brcmf_net_p2p_start_xmit(struct sk_buff *skb,
					    struct net_device *ndev)
{
	if (skb)
		dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops brcmf_netdev_ops_p2p = {
	.ndo_open = brcmf_net_p2p_open,
	.ndo_stop = brcmf_net_p2p_stop,
	.ndo_do_ioctl = brcmf_net_p2p_do_ioctl,
	.ndo_start_xmit = brcmf_net_p2p_start_xmit
};

static int brcmf_net_p2p_attach(struct brcmf_if *ifp)
{
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, idx=%d mac=%pM\n", ifp->bssidx,
		  ifp->mac_addr);
	ndev = ifp->ndev;

	ndev->netdev_ops = &brcmf_netdev_ops_p2p;

	/* set the mac address */
	memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);

	if (register_netdev(ndev) != 0) {
		brcmf_err("couldn't register the p2p net device\n");
		goto fail;
	}

	brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

	return 0;

fail:
	ifp->drvr->iflist[ifp->bssidx] = NULL;
	ndev->netdev_ops = NULL;
	free_netdev(ndev);
	return -EBADE;
}

struct brcmf_if *brcmf_add_if(struct brcmf_pub *drvr, s32 bssidx, s32 ifidx,
			      char *name, u8 *mac_addr)
{
	struct brcmf_if *ifp;
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, idx=%d, ifidx=%d\n", bssidx, ifidx);

	ifp = drvr->iflist[bssidx];
	/*
	 * Delete the existing interface before overwriting it
	 * in case we missed the BRCMF_E_IF_DEL event.
	 */
	if (ifp) {
		brcmf_err("ERROR: netdev:%s already exists\n",
			  ifp->ndev->name);
		if (ifidx) {
			netif_stop_queue(ifp->ndev);
			unregister_netdev(ifp->ndev);
			free_netdev(ifp->ndev);
			drvr->iflist[bssidx] = NULL;
		} else {
			brcmf_err("ignore IF event\n");
			return ERR_PTR(-EINVAL);
		}
	}

	if (!brcmf_p2p_enable && bssidx == 1) {
		/* this is P2P_DEVICE interface */
		brcmf_dbg(INFO, "allocate non-netdev interface\n");
		ifp = kzalloc(sizeof(*ifp), GFP_KERNEL);
		if (!ifp)
			return ERR_PTR(-ENOMEM);
	} else {
		brcmf_dbg(INFO, "allocate netdev interface\n");
		/* Allocate netdev, including space for private structure */
		ndev = alloc_netdev(sizeof(*ifp), name, ether_setup);
		if (!ndev)
			return ERR_PTR(-ENOMEM);

		ifp = netdev_priv(ndev);
		ifp->ndev = ndev;
	}

	ifp->drvr = drvr;
	drvr->iflist[bssidx] = ifp;
	ifp->ifidx = ifidx;
	ifp->bssidx = bssidx;

	init_waitqueue_head(&ifp->pend_8021x_wait);
	spin_lock_init(&ifp->netif_stop_lock);

	if (mac_addr != NULL)
		memcpy(ifp->mac_addr, mac_addr, ETH_ALEN);

	brcmf_dbg(TRACE, " ==== pid:%x, if:%s (%pM) created ===\n",
		  current->pid, name, ifp->mac_addr);

	return ifp;
}

void brcmf_del_if(struct brcmf_pub *drvr, s32 bssidx)
{
	struct brcmf_if *ifp;

	ifp = drvr->iflist[bssidx];
	drvr->iflist[bssidx] = NULL;
	if (!ifp) {
		brcmf_err("Null interface, idx=%d\n", bssidx);
		return;
	}
	brcmf_dbg(TRACE, "Enter, idx=%d, ifidx=%d\n", bssidx, ifp->ifidx);
	if (ifp->ndev) {
		if (bssidx == 0) {
			if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
				rtnl_lock();
				brcmf_netdev_stop(ifp->ndev);
				rtnl_unlock();
			}
		} else {
			netif_stop_queue(ifp->ndev);
		}

		if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
			cancel_work_sync(&ifp->setmacaddr_work);
			cancel_work_sync(&ifp->multicast_work);
		}
		/* unregister will take care of freeing it */
		unregister_netdev(ifp->ndev);
		if (bssidx == 0)
			brcmf_cfg80211_detach(drvr->config);
	} else {
		kfree(ifp);
	}
}

int brcmf_attach(uint bus_hdrlen, struct device *dev)
{
	struct brcmf_pub *drvr = NULL;
	int ret = 0;

	brcmf_dbg(TRACE, "Enter\n");

	/* Allocate primary brcmf_info */
	drvr = kzalloc(sizeof(struct brcmf_pub), GFP_ATOMIC);
	if (!drvr)
		return -ENOMEM;

	mutex_init(&drvr->proto_block);

	/* Link to bus module */
	drvr->hdrlen = bus_hdrlen;
	drvr->bus_if = dev_get_drvdata(dev);
	drvr->bus_if->drvr = drvr;

	/* create device debugfs folder */
	brcmf_debugfs_attach(drvr);

	/* Attach and link in the protocol */
	ret = brcmf_proto_attach(drvr);
	if (ret != 0) {
		brcmf_err("brcmf_prot_attach failed\n");
		goto fail;
	}

	/* attach firmware event handler */
	brcmf_fweh_attach(drvr);

	INIT_LIST_HEAD(&drvr->bus_if->dcmd_list);

	return ret;

fail:
	brcmf_detach(dev);

	return ret;
}

int brcmf_bus_start(struct device *dev)
{
	int ret = -1;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_if *ifp;
	struct brcmf_if *p2p_ifp;

	brcmf_dbg(TRACE, "\n");

	/* Bring up the bus */
	ret = brcmf_bus_init(bus_if);
	if (ret != 0) {
		brcmf_err("brcmf_sdbrcm_bus_init failed %d\n", ret);
		return ret;
	}

	/* add primary networking interface */
	ifp = brcmf_add_if(drvr, 0, 0, "wlan%d", NULL);
	if (IS_ERR(ifp))
		return PTR_ERR(ifp);

	if (brcmf_p2p_enable)
		p2p_ifp = brcmf_add_if(drvr, 1, 0, "p2p%d", NULL);
	else
		p2p_ifp = NULL;
	if (IS_ERR(p2p_ifp))
		p2p_ifp = NULL;

	/* signal bus ready */
	bus_if->state = BRCMF_BUS_DATA;

	/* Bus is ready, do any initialization */
	ret = brcmf_c_preinit_dcmds(ifp);
	if (ret < 0)
		goto fail;

	ret = brcmf_fws_init(drvr);
	if (ret < 0)
		goto fail;

	brcmf_fws_add_interface(ifp);

	drvr->config = brcmf_cfg80211_attach(drvr, bus_if->dev);
	if (drvr->config == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = brcmf_fweh_activate_events(ifp);
	if (ret < 0)
		goto fail;

	ret = brcmf_net_attach(ifp, false);
fail:
	if (ret < 0) {
		brcmf_err("failed: %d\n", ret);
		if (drvr->config)
			brcmf_cfg80211_detach(drvr->config);
		if (drvr->fws) {
			brcmf_fws_del_interface(ifp);
			brcmf_fws_deinit(drvr);
		}
		if (drvr->iflist[0]) {
			free_netdev(ifp->ndev);
			drvr->iflist[0] = NULL;
		}
		if (p2p_ifp) {
			free_netdev(p2p_ifp->ndev);
			drvr->iflist[1] = NULL;
		}
		return ret;
	}
	if ((brcmf_p2p_enable) && (p2p_ifp))
		if (brcmf_net_p2p_attach(p2p_ifp) < 0)
			brcmf_p2p_enable = 0;

	return 0;
}

static void brcmf_bus_detach(struct brcmf_pub *drvr)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (drvr) {
		/* Stop the protocol module */
		brcmf_proto_stop(drvr);

		/* Stop the bus module */
		brcmf_bus_stop(drvr->bus_if);
	}
}

void brcmf_dev_reset(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	if (drvr == NULL)
		return;

	if (drvr->iflist[0])
		brcmf_fil_cmd_int_set(drvr->iflist[0], BRCMF_C_TERMINATED, 1);
}

void brcmf_detach(struct device *dev)
{
	s32 i;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	if (drvr == NULL)
		return;

	/* stop firmware event handling */
	brcmf_fweh_detach(drvr);

	/* make sure primary interface removed last */
	for (i = BRCMF_MAX_IFS-1; i > -1; i--)
		if (drvr->iflist[i]) {
			brcmf_fws_del_interface(drvr->iflist[i]);
			brcmf_del_if(drvr, i);
		}

	brcmf_bus_detach(drvr);

	if (drvr->prot)
		brcmf_proto_detach(drvr);

	brcmf_fws_deinit(drvr);

	brcmf_debugfs_detach(drvr);
	bus_if->drvr = NULL;
	kfree(drvr);
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_if *ifp)
{
	return atomic_read(&ifp->pend_8021x_cnt);
}

int brcmf_netdev_wait_pend8021x(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	int err;

	err = wait_event_timeout(ifp->pend_8021x_wait,
				 !brcmf_get_pend_8021x_cnt(ifp),
				 msecs_to_jiffies(MAX_WAIT_FOR_8021X_TX));

	WARN_ON(!err);

	return !err;
}

/*
 * return chip id and rev of the device encoded in u32.
 */
u32 brcmf_get_chip_info(struct brcmf_if *ifp)
{
	struct brcmf_bus *bus = ifp->drvr->bus_if;

	return bus->chip << 4 | bus->chiprev;
}

static void brcmf_driver_register(struct work_struct *work)
{
#ifdef CONFIG_BRCMFMAC_SDIO
	brcmf_sdio_register();
#endif
#ifdef CONFIG_BRCMFMAC_USB
	brcmf_usb_register();
#endif
}
static DECLARE_WORK(brcmf_driver_work, brcmf_driver_register);

static int __init brcmfmac_module_init(void)
{
	brcmf_debugfs_init();
#ifdef CONFIG_BRCMFMAC_SDIO
	brcmf_sdio_init();
#endif
	if (!schedule_work(&brcmf_driver_work))
		return -EBUSY;

	return 0;
}

static void __exit brcmfmac_module_exit(void)
{
	cancel_work_sync(&brcmf_driver_work);

#ifdef CONFIG_BRCMFMAC_SDIO
	brcmf_sdio_exit();
#endif
#ifdef CONFIG_BRCMFMAC_USB
	brcmf_usb_exit();
#endif
	brcmf_debugfs_exit();
}

module_init(brcmfmac_module_init);
module_exit(brcmfmac_module_exit);

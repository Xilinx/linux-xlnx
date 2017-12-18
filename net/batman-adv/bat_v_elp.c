/* Copyright (C) 2011-2016 B.A.T.M.A.N. contributors:
 *
 * Linus Lüssing, Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "bat_v_elp.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "bat_algo.h"
#include "bat_v_ogm.h"
#include "hard-interface.h"
#include "log.h"
#include "originator.h"
#include "packet.h"
#include "routing.h"
#include "send.h"

/**
 * batadv_v_elp_start_timer - restart timer for ELP periodic work
 * @hard_iface: the interface for which the timer has to be reset
 */
static void batadv_v_elp_start_timer(struct batadv_hard_iface *hard_iface)
{
	unsigned int msecs;

	msecs = atomic_read(&hard_iface->bat_v.elp_interval) - BATADV_JITTER;
	msecs += prandom_u32() % (2 * BATADV_JITTER);

	queue_delayed_work(batadv_event_workqueue, &hard_iface->bat_v.elp_wq,
			   msecs_to_jiffies(msecs));
}

/**
 * batadv_v_elp_get_throughput - get the throughput towards a neighbour
 * @neigh: the neighbour for which the throughput has to be obtained
 *
 * Return: The throughput towards the given neighbour in multiples of 100kpbs
 *         (a value of '1' equals to 0.1Mbps, '10' equals 1Mbps, etc).
 */
static u32 batadv_v_elp_get_throughput(struct batadv_hardif_neigh_node *neigh)
{
	struct batadv_hard_iface *hard_iface = neigh->if_incoming;
	struct ethtool_link_ksettings link_settings;
	struct station_info sinfo;
	u32 throughput;
	int ret;

	/* if the user specified a customised value for this interface, then
	 * return it directly
	 */
	throughput =  atomic_read(&hard_iface->bat_v.throughput_override);
	if (throughput != 0)
		return throughput;

	/* if this is a wireless device, then ask its throughput through
	 * cfg80211 API
	 */
	if (batadv_is_wifi_netdev(hard_iface->net_dev)) {
		if (hard_iface->net_dev->ieee80211_ptr) {
			ret = cfg80211_get_station(hard_iface->net_dev,
						   neigh->addr, &sinfo);
			if (ret == -ENOENT) {
				/* Node is not associated anymore! It would be
				 * possible to delete this neighbor. For now set
				 * the throughput metric to 0.
				 */
				return 0;
			}
			if (!ret)
				return sinfo.expected_throughput / 100;
		}

		/* unsupported WiFi driver version */
		goto default_throughput;
	}

	/* if not a wifi interface, check if this device provides data via
	 * ethtool (e.g. an Ethernet adapter)
	 */
	memset(&link_settings, 0, sizeof(link_settings));
	rtnl_lock();
	ret = __ethtool_get_link_ksettings(hard_iface->net_dev, &link_settings);
	rtnl_unlock();
	if (ret == 0) {
		/* link characteristics might change over time */
		if (link_settings.base.duplex == DUPLEX_FULL)
			hard_iface->bat_v.flags |= BATADV_FULL_DUPLEX;
		else
			hard_iface->bat_v.flags &= ~BATADV_FULL_DUPLEX;

		throughput = link_settings.base.speed;
		if (throughput && (throughput != SPEED_UNKNOWN))
			return throughput * 10;
	}

default_throughput:
	if (!(hard_iface->bat_v.flags & BATADV_WARNING_DEFAULT)) {
		batadv_info(hard_iface->soft_iface,
			    "WiFi driver or ethtool info does not provide information about link speeds on interface %s, therefore defaulting to hardcoded throughput values of %u.%1u Mbps. Consider overriding the throughput manually or checking your driver.\n",
			    hard_iface->net_dev->name,
			    BATADV_THROUGHPUT_DEFAULT_VALUE / 10,
			    BATADV_THROUGHPUT_DEFAULT_VALUE % 10);
		hard_iface->bat_v.flags |= BATADV_WARNING_DEFAULT;
	}

	/* if none of the above cases apply, return the base_throughput */
	return BATADV_THROUGHPUT_DEFAULT_VALUE;
}

/**
 * batadv_v_elp_throughput_metric_update - worker updating the throughput metric
 *  of a single hop neighbour
 * @work: the work queue item
 */
void batadv_v_elp_throughput_metric_update(struct work_struct *work)
{
	struct batadv_hardif_neigh_node_bat_v *neigh_bat_v;
	struct batadv_hardif_neigh_node *neigh;

	neigh_bat_v = container_of(work, struct batadv_hardif_neigh_node_bat_v,
				   metric_work);
	neigh = container_of(neigh_bat_v, struct batadv_hardif_neigh_node,
			     bat_v);

	ewma_throughput_add(&neigh->bat_v.throughput,
			    batadv_v_elp_get_throughput(neigh));

	/* decrement refcounter to balance increment performed before scheduling
	 * this task
	 */
	batadv_hardif_neigh_put(neigh);
}

/**
 * batadv_v_elp_wifi_neigh_probe - send link probing packets to a neighbour
 * @neigh: the neighbour to probe
 *
 * Sends a predefined number of unicast wifi packets to a given neighbour in
 * order to trigger the throughput estimation on this link by the RC algorithm.
 * Packets are sent only if there there is not enough payload unicast traffic
 * towards this neighbour..
 *
 * Return: True on success and false in case of error during skb preparation.
 */
static bool
batadv_v_elp_wifi_neigh_probe(struct batadv_hardif_neigh_node *neigh)
{
	struct batadv_hard_iface *hard_iface = neigh->if_incoming;
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	unsigned long last_tx_diff;
	struct sk_buff *skb;
	int probe_len, i;
	int elp_skb_len;

	/* this probing routine is for Wifi neighbours only */
	if (!batadv_is_wifi_netdev(hard_iface->net_dev))
		return true;

	/* probe the neighbor only if no unicast packets have been sent
	 * to it in the last 100 milliseconds: this is the rate control
	 * algorithm sampling interval (minstrel). In this way, if not
	 * enough traffic has been sent to the neighbor, batman-adv can
	 * generate 2 probe packets and push the RC algorithm to perform
	 * the sampling
	 */
	last_tx_diff = jiffies_to_msecs(jiffies - neigh->bat_v.last_unicast_tx);
	if (last_tx_diff <= BATADV_ELP_PROBE_MAX_TX_DIFF)
		return true;

	probe_len = max_t(int, sizeof(struct batadv_elp_packet),
			  BATADV_ELP_MIN_PROBE_SIZE);

	for (i = 0; i < BATADV_ELP_PROBES_PER_NODE; i++) {
		elp_skb_len = hard_iface->bat_v.elp_skb->len;
		skb = skb_copy_expand(hard_iface->bat_v.elp_skb, 0,
				      probe_len - elp_skb_len,
				      GFP_ATOMIC);
		if (!skb)
			return false;

		/* Tell the skb to get as big as the allocated space (we want
		 * the packet to be exactly of that size to make the link
		 * throughput estimation effective.
		 */
		skb_put(skb, probe_len - hard_iface->bat_v.elp_skb->len);

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Sending unicast (probe) ELP packet on interface %s to %pM\n",
			   hard_iface->net_dev->name, neigh->addr);

		batadv_send_skb_packet(skb, hard_iface, neigh->addr);
	}

	return true;
}

/**
 * batadv_v_elp_periodic_work - ELP periodic task per interface
 * @work: work queue item
 *
 * Emits broadcast ELP message in regular intervals.
 */
static void batadv_v_elp_periodic_work(struct work_struct *work)
{
	struct batadv_hardif_neigh_node *hardif_neigh;
	struct batadv_hard_iface *hard_iface;
	struct batadv_hard_iface_bat_v *bat_v;
	struct batadv_elp_packet *elp_packet;
	struct batadv_priv *bat_priv;
	struct sk_buff *skb;
	u32 elp_interval;

	bat_v = container_of(work, struct batadv_hard_iface_bat_v, elp_wq.work);
	hard_iface = container_of(bat_v, struct batadv_hard_iface, bat_v);
	bat_priv = netdev_priv(hard_iface->soft_iface);

	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_DEACTIVATING)
		goto out;

	/* we are in the process of shutting this interface down */
	if ((hard_iface->if_status == BATADV_IF_NOT_IN_USE) ||
	    (hard_iface->if_status == BATADV_IF_TO_BE_REMOVED))
		goto out;

	/* the interface was enabled but may not be ready yet */
	if (hard_iface->if_status != BATADV_IF_ACTIVE)
		goto restart_timer;

	skb = skb_copy(hard_iface->bat_v.elp_skb, GFP_ATOMIC);
	if (!skb)
		goto restart_timer;

	elp_packet = (struct batadv_elp_packet *)skb->data;
	elp_packet->seqno = htonl(atomic_read(&hard_iface->bat_v.elp_seqno));
	elp_interval = atomic_read(&hard_iface->bat_v.elp_interval);
	elp_packet->elp_interval = htonl(elp_interval);

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Sending broadcast ELP packet on interface %s, seqno %u\n",
		   hard_iface->net_dev->name,
		   atomic_read(&hard_iface->bat_v.elp_seqno));

	batadv_send_broadcast_skb(skb, hard_iface);

	atomic_inc(&hard_iface->bat_v.elp_seqno);

	/* The throughput metric is updated on each sent packet. This way, if a
	 * node is dead and no longer sends packets, batman-adv is still able to
	 * react timely to its death.
	 *
	 * The throughput metric is updated by following these steps:
	 * 1) if the hard_iface is wifi => send a number of unicast ELPs for
	 *    probing/sampling to each neighbor
	 * 2) update the throughput metric value of each neighbor (note that the
	 *    value retrieved in this step might be 100ms old because the
	 *    probing packets at point 1) could still be in the HW queue)
	 */
	rcu_read_lock();
	hlist_for_each_entry_rcu(hardif_neigh, &hard_iface->neigh_list, list) {
		if (!batadv_v_elp_wifi_neigh_probe(hardif_neigh))
			/* if something goes wrong while probing, better to stop
			 * sending packets immediately and reschedule the task
			 */
			break;

		if (!kref_get_unless_zero(&hardif_neigh->refcount))
			continue;

		/* Reading the estimated throughput from cfg80211 is a task that
		 * may sleep and that is not allowed in an rcu protected
		 * context. Therefore schedule a task for that.
		 */
		queue_work(batadv_event_workqueue,
			   &hardif_neigh->bat_v.metric_work);
	}
	rcu_read_unlock();

restart_timer:
	batadv_v_elp_start_timer(hard_iface);
out:
	return;
}

/**
 * batadv_v_elp_iface_enable - setup the ELP interface private resources
 * @hard_iface: interface for which the data has to be prepared
 *
 * Return: 0 on success or a -ENOMEM in case of failure.
 */
int batadv_v_elp_iface_enable(struct batadv_hard_iface *hard_iface)
{
	struct batadv_elp_packet *elp_packet;
	unsigned char *elp_buff;
	u32 random_seqno;
	size_t size;
	int res = -ENOMEM;

	size = ETH_HLEN + NET_IP_ALIGN + BATADV_ELP_HLEN;
	hard_iface->bat_v.elp_skb = dev_alloc_skb(size);
	if (!hard_iface->bat_v.elp_skb)
		goto out;

	skb_reserve(hard_iface->bat_v.elp_skb, ETH_HLEN + NET_IP_ALIGN);
	elp_buff = skb_put(hard_iface->bat_v.elp_skb, BATADV_ELP_HLEN);
	elp_packet = (struct batadv_elp_packet *)elp_buff;
	memset(elp_packet, 0, BATADV_ELP_HLEN);

	elp_packet->packet_type = BATADV_ELP;
	elp_packet->version = BATADV_COMPAT_VERSION;

	/* randomize initial seqno to avoid collision */
	get_random_bytes(&random_seqno, sizeof(random_seqno));
	atomic_set(&hard_iface->bat_v.elp_seqno, random_seqno);

	/* assume full-duplex by default */
	hard_iface->bat_v.flags |= BATADV_FULL_DUPLEX;

	/* warn the user (again) if there is no throughput data is available */
	hard_iface->bat_v.flags &= ~BATADV_WARNING_DEFAULT;

	if (batadv_is_wifi_netdev(hard_iface->net_dev))
		hard_iface->bat_v.flags &= ~BATADV_FULL_DUPLEX;

	INIT_DELAYED_WORK(&hard_iface->bat_v.elp_wq,
			  batadv_v_elp_periodic_work);
	batadv_v_elp_start_timer(hard_iface);
	res = 0;

out:
	return res;
}

/**
 * batadv_v_elp_iface_disable - release ELP interface private resources
 * @hard_iface: interface for which the resources have to be released
 */
void batadv_v_elp_iface_disable(struct batadv_hard_iface *hard_iface)
{
	cancel_delayed_work_sync(&hard_iface->bat_v.elp_wq);

	dev_kfree_skb(hard_iface->bat_v.elp_skb);
	hard_iface->bat_v.elp_skb = NULL;
}

/**
 * batadv_v_elp_iface_activate - update the ELP buffer belonging to the given
 *  hard-interface
 * @primary_iface: the new primary interface
 * @hard_iface: interface holding the to-be-updated buffer
 */
void batadv_v_elp_iface_activate(struct batadv_hard_iface *primary_iface,
				 struct batadv_hard_iface *hard_iface)
{
	struct batadv_elp_packet *elp_packet;
	struct sk_buff *skb;

	if (!hard_iface->bat_v.elp_skb)
		return;

	skb = hard_iface->bat_v.elp_skb;
	elp_packet = (struct batadv_elp_packet *)skb->data;
	ether_addr_copy(elp_packet->orig,
			primary_iface->net_dev->dev_addr);
}

/**
 * batadv_v_elp_primary_iface_set - change internal data to reflect the new
 *  primary interface
 * @primary_iface: the new primary interface
 */
void batadv_v_elp_primary_iface_set(struct batadv_hard_iface *primary_iface)
{
	struct batadv_hard_iface *hard_iface;

	/* update orig field of every elp iface belonging to this mesh */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (primary_iface->soft_iface != hard_iface->soft_iface)
			continue;

		batadv_v_elp_iface_activate(primary_iface, hard_iface);
	}
	rcu_read_unlock();
}

/**
 * batadv_v_elp_neigh_update - update an ELP neighbour node
 * @bat_priv: the bat priv with all the soft interface information
 * @neigh_addr: the neighbour interface address
 * @if_incoming: the interface the packet was received through
 * @elp_packet: the received ELP packet
 *
 * Updates the ELP neighbour node state with the data received within the new
 * ELP packet.
 */
static void batadv_v_elp_neigh_update(struct batadv_priv *bat_priv,
				      u8 *neigh_addr,
				      struct batadv_hard_iface *if_incoming,
				      struct batadv_elp_packet *elp_packet)

{
	struct batadv_neigh_node *neigh;
	struct batadv_orig_node *orig_neigh;
	struct batadv_hardif_neigh_node *hardif_neigh;
	s32 seqno_diff;
	s32 elp_latest_seqno;

	orig_neigh = batadv_v_ogm_orig_get(bat_priv, elp_packet->orig);
	if (!orig_neigh)
		return;

	neigh = batadv_neigh_node_get_or_create(orig_neigh,
						if_incoming, neigh_addr);
	if (!neigh)
		goto orig_free;

	hardif_neigh = batadv_hardif_neigh_get(if_incoming, neigh_addr);
	if (!hardif_neigh)
		goto neigh_free;

	elp_latest_seqno = hardif_neigh->bat_v.elp_latest_seqno;
	seqno_diff = ntohl(elp_packet->seqno) - elp_latest_seqno;

	/* known or older sequence numbers are ignored. However always adopt
	 * if the router seems to have been restarted.
	 */
	if (seqno_diff < 1 && seqno_diff > -BATADV_ELP_MAX_AGE)
		goto hardif_free;

	neigh->last_seen = jiffies;
	hardif_neigh->last_seen = jiffies;
	hardif_neigh->bat_v.elp_latest_seqno = ntohl(elp_packet->seqno);
	hardif_neigh->bat_v.elp_interval = ntohl(elp_packet->elp_interval);

hardif_free:
	if (hardif_neigh)
		batadv_hardif_neigh_put(hardif_neigh);
neigh_free:
	if (neigh)
		batadv_neigh_node_put(neigh);
orig_free:
	if (orig_neigh)
		batadv_orig_node_put(orig_neigh);
}

/**
 * batadv_v_elp_packet_recv - main ELP packet handler
 * @skb: the received packet
 * @if_incoming: the interface this packet was received through
 *
 * Return: NET_RX_SUCCESS and consumes the skb if the packet was peoperly
 * processed or NET_RX_DROP in case of failure.
 */
int batadv_v_elp_packet_recv(struct sk_buff *skb,
			     struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_elp_packet *elp_packet;
	struct batadv_hard_iface *primary_if;
	struct ethhdr *ethhdr = (struct ethhdr *)skb_mac_header(skb);
	bool ret;

	ret = batadv_check_management_packet(skb, if_incoming, BATADV_ELP_HLEN);
	if (!ret)
		return NET_RX_DROP;

	if (batadv_is_my_mac(bat_priv, ethhdr->h_source))
		return NET_RX_DROP;

	/* did we receive a B.A.T.M.A.N. V ELP packet on an interface
	 * that does not have B.A.T.M.A.N. V ELP enabled ?
	 */
	if (strcmp(bat_priv->algo_ops->name, "BATMAN_V") != 0)
		return NET_RX_DROP;

	elp_packet = (struct batadv_elp_packet *)skb->data;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Received ELP packet from %pM seqno %u ORIG: %pM\n",
		   ethhdr->h_source, ntohl(elp_packet->seqno),
		   elp_packet->orig);

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	batadv_v_elp_neigh_update(bat_priv, ethhdr->h_source, if_incoming,
				  elp_packet);

out:
	if (primary_if)
		batadv_hardif_put(primary_if);
	consume_skb(skb);
	return NET_RX_SUCCESS;
}

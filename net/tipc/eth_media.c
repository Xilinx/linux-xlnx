/*
 * net/tipc/eth_media.c: Ethernet bearer support for TIPC
 *
 * Copyright (c) 2001-2007, Ericsson AB
 * Copyright (c) 2005-2008, 2011-2013, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "bearer.h"

#define MAX_ETH_MEDIA		MAX_BEARERS

#define ETH_ADDR_OFFSET	4	/* message header offset of MAC address */

/**
 * struct eth_media - Ethernet bearer data structure
 * @bearer: ptr to associated "generic" bearer structure
 * @dev: ptr to associated Ethernet network device
 * @tipc_packet_type: used in binding TIPC to Ethernet driver
 * @setup: work item used when enabling bearer
 * @cleanup: work item used when disabling bearer
 */
struct eth_media {
	struct tipc_bearer *bearer;
	struct net_device *dev;
	struct packet_type tipc_packet_type;
	struct work_struct setup;
	struct work_struct cleanup;
};

static struct tipc_media eth_media_info;
static struct eth_media eth_media_array[MAX_ETH_MEDIA];
static int eth_started;

static int recv_notification(struct notifier_block *nb, unsigned long evt,
			     void *dv);
/*
 * Network device notifier info
 */
static struct notifier_block notifier = {
	.notifier_call	= recv_notification,
	.priority	= 0
};

/**
 * eth_media_addr_set - initialize Ethernet media address structure
 *
 * Media-dependent "value" field stores MAC address in first 6 bytes
 * and zeroes out the remaining bytes.
 */
static void eth_media_addr_set(const struct tipc_bearer *tb_ptr,
			       struct tipc_media_addr *a, char *mac)
{
	memcpy(a->value, mac, ETH_ALEN);
	memset(a->value + ETH_ALEN, 0, sizeof(a->value) - ETH_ALEN);
	a->media_id = TIPC_MEDIA_TYPE_ETH;
	a->broadcast = !memcmp(mac, tb_ptr->bcast_addr.value, ETH_ALEN);
}

/**
 * send_msg - send a TIPC message out over an Ethernet interface
 */
static int send_msg(struct sk_buff *buf, struct tipc_bearer *tb_ptr,
		    struct tipc_media_addr *dest)
{
	struct sk_buff *clone;
	struct net_device *dev;
	int delta;

	clone = skb_clone(buf, GFP_ATOMIC);
	if (!clone)
		return 0;

	dev = ((struct eth_media *)(tb_ptr->usr_handle))->dev;
	delta = dev->hard_header_len - skb_headroom(buf);

	if ((delta > 0) &&
	    pskb_expand_head(clone, SKB_DATA_ALIGN(delta), 0, GFP_ATOMIC)) {
		kfree_skb(clone);
		return 0;
	}

	skb_reset_network_header(clone);
	clone->dev = dev;
	clone->protocol = htons(ETH_P_TIPC);
	dev_hard_header(clone, dev, ETH_P_TIPC, dest->value,
			dev->dev_addr, clone->len);
	dev_queue_xmit(clone);
	return 0;
}

/**
 * recv_msg - handle incoming TIPC message from an Ethernet interface
 *
 * Accept only packets explicitly sent to this node, or broadcast packets;
 * ignores packets sent using Ethernet multicast, and traffic sent to other
 * nodes (which can happen if interface is running in promiscuous mode).
 */
static int recv_msg(struct sk_buff *buf, struct net_device *dev,
		    struct packet_type *pt, struct net_device *orig_dev)
{
	struct eth_media *eb_ptr = (struct eth_media *)pt->af_packet_priv;

	if (!net_eq(dev_net(dev), &init_net)) {
		kfree_skb(buf);
		return NET_RX_DROP;
	}

	if (likely(eb_ptr->bearer)) {
		if (likely(buf->pkt_type <= PACKET_BROADCAST)) {
			buf->next = NULL;
			tipc_recv_msg(buf, eb_ptr->bearer);
			return NET_RX_SUCCESS;
		}
	}
	kfree_skb(buf);
	return NET_RX_DROP;
}

/**
 * setup_media - setup association between Ethernet bearer and interface
 */
static void setup_media(struct work_struct *work)
{
	struct eth_media *eb_ptr =
		container_of(work, struct eth_media, setup);

	dev_add_pack(&eb_ptr->tipc_packet_type);
}

/**
 * enable_media - attach TIPC bearer to an Ethernet interface
 */
static int enable_media(struct tipc_bearer *tb_ptr)
{
	struct net_device *dev;
	struct eth_media *eb_ptr = &eth_media_array[0];
	struct eth_media *stop = &eth_media_array[MAX_ETH_MEDIA];
	char *driver_name = strchr((const char *)tb_ptr->name, ':') + 1;
	int pending_dev = 0;

	/* Find unused Ethernet bearer structure */
	while (eb_ptr->dev) {
		if (!eb_ptr->bearer)
			pending_dev++;
		if (++eb_ptr == stop)
			return pending_dev ? -EAGAIN : -EDQUOT;
	}

	/* Find device with specified name */
	dev = dev_get_by_name(&init_net, driver_name);
	if (!dev)
		return -ENODEV;

	/* Create Ethernet bearer for device */
	eb_ptr->dev = dev;
	eb_ptr->tipc_packet_type.type = htons(ETH_P_TIPC);
	eb_ptr->tipc_packet_type.dev = dev;
	eb_ptr->tipc_packet_type.func = recv_msg;
	eb_ptr->tipc_packet_type.af_packet_priv = eb_ptr;
	INIT_LIST_HEAD(&(eb_ptr->tipc_packet_type.list));
	INIT_WORK(&eb_ptr->setup, setup_media);
	schedule_work(&eb_ptr->setup);

	/* Associate TIPC bearer with Ethernet bearer */
	eb_ptr->bearer = tb_ptr;
	tb_ptr->usr_handle = (void *)eb_ptr;
	memset(tb_ptr->bcast_addr.value, 0, sizeof(tb_ptr->bcast_addr.value));
	memcpy(tb_ptr->bcast_addr.value, dev->broadcast, ETH_ALEN);
	tb_ptr->bcast_addr.media_id = TIPC_MEDIA_TYPE_ETH;
	tb_ptr->bcast_addr.broadcast = 1;
	tb_ptr->mtu = dev->mtu;
	tb_ptr->blocked = 0;
	eth_media_addr_set(tb_ptr, &tb_ptr->addr, (char *)dev->dev_addr);
	return 0;
}

/**
 * cleanup_media - break association between Ethernet bearer and interface
 *
 * This routine must be invoked from a work queue because it can sleep.
 */
static void cleanup_media(struct work_struct *work)
{
	struct eth_media *eb_ptr =
		container_of(work, struct eth_media, cleanup);

	dev_remove_pack(&eb_ptr->tipc_packet_type);
	dev_put(eb_ptr->dev);
	eb_ptr->dev = NULL;
}

/**
 * disable_media - detach TIPC bearer from an Ethernet interface
 *
 * Mark Ethernet bearer as inactive so that incoming buffers are thrown away,
 * then get worker thread to complete bearer cleanup.  (Can't do cleanup
 * here because cleanup code needs to sleep and caller holds spinlocks.)
 */
static void disable_media(struct tipc_bearer *tb_ptr)
{
	struct eth_media *eb_ptr = (struct eth_media *)tb_ptr->usr_handle;

	eb_ptr->bearer = NULL;
	INIT_WORK(&eb_ptr->cleanup, cleanup_media);
	schedule_work(&eb_ptr->cleanup);
}

/**
 * recv_notification - handle device updates from OS
 *
 * Change the state of the Ethernet bearer (if any) associated with the
 * specified device.
 */
static int recv_notification(struct notifier_block *nb, unsigned long evt,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct eth_media *eb_ptr = &eth_media_array[0];
	struct eth_media *stop = &eth_media_array[MAX_ETH_MEDIA];

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	while ((eb_ptr->dev != dev)) {
		if (++eb_ptr == stop)
			return NOTIFY_DONE;	/* couldn't find device */
	}
	if (!eb_ptr->bearer)
		return NOTIFY_DONE;		/* bearer had been disabled */

	eb_ptr->bearer->mtu = dev->mtu;

	switch (evt) {
	case NETDEV_CHANGE:
		if (netif_carrier_ok(dev))
			tipc_continue(eb_ptr->bearer);
		else
			tipc_block_bearer(eb_ptr->bearer);
		break;
	case NETDEV_UP:
		tipc_continue(eb_ptr->bearer);
		break;
	case NETDEV_DOWN:
		tipc_block_bearer(eb_ptr->bearer);
		break;
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGEADDR:
		tipc_block_bearer(eb_ptr->bearer);
		tipc_continue(eb_ptr->bearer);
		break;
	case NETDEV_UNREGISTER:
	case NETDEV_CHANGENAME:
		tipc_disable_bearer(eb_ptr->bearer->name);
		break;
	}
	return NOTIFY_OK;
}

/**
 * eth_addr2str - convert Ethernet address to string
 */
static int eth_addr2str(struct tipc_media_addr *a, char *str_buf, int str_size)
{
	if (str_size < 18)	/* 18 = strlen("aa:bb:cc:dd:ee:ff\0") */
		return 1;

	sprintf(str_buf, "%pM", a->value);
	return 0;
}

/**
 * eth_str2addr - convert Ethernet address format to message header format
 */
static int eth_addr2msg(struct tipc_media_addr *a, char *msg_area)
{
	memset(msg_area, 0, TIPC_MEDIA_ADDR_SIZE);
	msg_area[TIPC_MEDIA_TYPE_OFFSET] = TIPC_MEDIA_TYPE_ETH;
	memcpy(msg_area + ETH_ADDR_OFFSET, a->value, ETH_ALEN);
	return 0;
}

/**
 * eth_str2addr - convert message header address format to Ethernet format
 */
static int eth_msg2addr(const struct tipc_bearer *tb_ptr,
			struct tipc_media_addr *a, char *msg_area)
{
	if (msg_area[TIPC_MEDIA_TYPE_OFFSET] != TIPC_MEDIA_TYPE_ETH)
		return 1;

	eth_media_addr_set(tb_ptr, a, msg_area + ETH_ADDR_OFFSET);
	return 0;
}

/*
 * Ethernet media registration info
 */
static struct tipc_media eth_media_info = {
	.send_msg	= send_msg,
	.enable_media	= enable_media,
	.disable_media	= disable_media,
	.addr2str	= eth_addr2str,
	.addr2msg	= eth_addr2msg,
	.msg2addr	= eth_msg2addr,
	.priority	= TIPC_DEF_LINK_PRI,
	.tolerance	= TIPC_DEF_LINK_TOL,
	.window		= TIPC_DEF_LINK_WIN,
	.type_id	= TIPC_MEDIA_TYPE_ETH,
	.name		= "eth"
};

/**
 * tipc_eth_media_start - activate Ethernet bearer support
 *
 * Register Ethernet media type with TIPC bearer code.  Also register
 * with OS for notifications about device state changes.
 */
int tipc_eth_media_start(void)
{
	int res;

	if (eth_started)
		return -EINVAL;

	res = tipc_register_media(&eth_media_info);
	if (res)
		return res;

	res = register_netdevice_notifier(&notifier);
	if (!res)
		eth_started = 1;
	return res;
}

/**
 * tipc_eth_media_stop - deactivate Ethernet bearer support
 */
void tipc_eth_media_stop(void)
{
	if (!eth_started)
		return;

	flush_scheduled_work();
	unregister_netdevice_notifier(&notifier);
	eth_started = 0;
}

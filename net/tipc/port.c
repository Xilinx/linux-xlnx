/*
 * net/tipc/port.c: TIPC port code
 *
 * Copyright (c) 1992-2007, Ericsson AB
 * Copyright (c) 2004-2008, 2010-2013, Wind River Systems
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
#include "config.h"
#include "port.h"
#include "name_table.h"

/* Connection management: */
#define PROBING_INTERVAL 3600000	/* [ms] => 1 h */
#define CONFIRMED 0
#define PROBING 1

#define MAX_REJECT_SIZE 1024

DEFINE_SPINLOCK(tipc_port_list_lock);

static LIST_HEAD(ports);
static void port_handle_node_down(unsigned long ref);
static struct sk_buff *port_build_self_abort_msg(struct tipc_port *, u32 err);
static struct sk_buff *port_build_peer_abort_msg(struct tipc_port *, u32 err);
static void port_timeout(unsigned long ref);


static u32 port_peernode(struct tipc_port *p_ptr)
{
	return msg_destnode(&p_ptr->phdr);
}

static u32 port_peerport(struct tipc_port *p_ptr)
{
	return msg_destport(&p_ptr->phdr);
}

/**
 * tipc_port_peer_msg - verify message was sent by connected port's peer
 *
 * Handles cases where the node's network address has changed from
 * the default of <0.0.0> to its configured setting.
 */
int tipc_port_peer_msg(struct tipc_port *p_ptr, struct tipc_msg *msg)
{
	u32 peernode;
	u32 orignode;

	if (msg_origport(msg) != port_peerport(p_ptr))
		return 0;

	orignode = msg_orignode(msg);
	peernode = port_peernode(p_ptr);
	return (orignode == peernode) ||
		(!orignode && (peernode == tipc_own_addr)) ||
		(!peernode && (orignode == tipc_own_addr));
}

/**
 * tipc_multicast - send a multicast message to local and remote destinations
 */
int tipc_multicast(u32 ref, struct tipc_name_seq const *seq,
		   struct iovec const *msg_sect, unsigned int len)
{
	struct tipc_msg *hdr;
	struct sk_buff *buf;
	struct sk_buff *ibuf = NULL;
	struct tipc_port_list dports = {0, NULL, };
	struct tipc_port *oport = tipc_port_deref(ref);
	int ext_targets;
	int res;

	if (unlikely(!oport))
		return -EINVAL;

	/* Create multicast message */
	hdr = &oport->phdr;
	msg_set_type(hdr, TIPC_MCAST_MSG);
	msg_set_lookup_scope(hdr, TIPC_CLUSTER_SCOPE);
	msg_set_destport(hdr, 0);
	msg_set_destnode(hdr, 0);
	msg_set_nametype(hdr, seq->type);
	msg_set_namelower(hdr, seq->lower);
	msg_set_nameupper(hdr, seq->upper);
	msg_set_hdr_sz(hdr, MCAST_H_SIZE);
	res = tipc_msg_build(hdr, msg_sect, len, MAX_MSG_SIZE, &buf);
	if (unlikely(!buf))
		return res;

	/* Figure out where to send multicast message */
	ext_targets = tipc_nametbl_mc_translate(seq->type, seq->lower, seq->upper,
						TIPC_NODE_SCOPE, &dports);

	/* Send message to destinations (duplicate it only if necessary) */
	if (ext_targets) {
		if (dports.count != 0) {
			ibuf = skb_copy(buf, GFP_ATOMIC);
			if (ibuf == NULL) {
				tipc_port_list_free(&dports);
				kfree_skb(buf);
				return -ENOMEM;
			}
		}
		res = tipc_bclink_send_msg(buf);
		if ((res < 0) && (dports.count != 0))
			kfree_skb(ibuf);
	} else {
		ibuf = buf;
	}

	if (res >= 0) {
		if (ibuf)
			tipc_port_recv_mcast(ibuf, &dports);
	} else {
		tipc_port_list_free(&dports);
	}
	return res;
}

/**
 * tipc_port_recv_mcast - deliver multicast message to all destination ports
 *
 * If there is no port list, perform a lookup to create one
 */
void tipc_port_recv_mcast(struct sk_buff *buf, struct tipc_port_list *dp)
{
	struct tipc_msg *msg;
	struct tipc_port_list dports = {0, NULL, };
	struct tipc_port_list *item = dp;
	int cnt = 0;

	msg = buf_msg(buf);

	/* Create destination port list, if one wasn't supplied */
	if (dp == NULL) {
		tipc_nametbl_mc_translate(msg_nametype(msg),
				     msg_namelower(msg),
				     msg_nameupper(msg),
				     TIPC_CLUSTER_SCOPE,
				     &dports);
		item = dp = &dports;
	}

	/* Deliver a copy of message to each destination port */
	if (dp->count != 0) {
		msg_set_destnode(msg, tipc_own_addr);
		if (dp->count == 1) {
			msg_set_destport(msg, dp->ports[0]);
			tipc_port_recv_msg(buf);
			tipc_port_list_free(dp);
			return;
		}
		for (; cnt < dp->count; cnt++) {
			int index = cnt % PLSIZE;
			struct sk_buff *b = skb_clone(buf, GFP_ATOMIC);

			if (b == NULL) {
				pr_warn("Unable to deliver multicast message(s)\n");
				goto exit;
			}
			if ((index == 0) && (cnt != 0))
				item = item->next;
			msg_set_destport(buf_msg(b), item->ports[index]);
			tipc_port_recv_msg(b);
		}
	}
exit:
	kfree_skb(buf);
	tipc_port_list_free(dp);
}

/**
 * tipc_createport - create a generic TIPC port
 *
 * Returns pointer to (locked) TIPC port, or NULL if unable to create it
 */
struct tipc_port *tipc_createport(struct sock *sk,
				  u32 (*dispatcher)(struct tipc_port *,
				  struct sk_buff *),
				  void (*wakeup)(struct tipc_port *),
				  const u32 importance)
{
	struct tipc_port *p_ptr;
	struct tipc_msg *msg;
	u32 ref;

	p_ptr = kzalloc(sizeof(*p_ptr), GFP_ATOMIC);
	if (!p_ptr) {
		pr_warn("Port creation failed, no memory\n");
		return NULL;
	}
	ref = tipc_ref_acquire(p_ptr, &p_ptr->lock);
	if (!ref) {
		pr_warn("Port creation failed, ref. table exhausted\n");
		kfree(p_ptr);
		return NULL;
	}

	p_ptr->sk = sk;
	p_ptr->max_pkt = MAX_PKT_DEFAULT;
	p_ptr->ref = ref;
	INIT_LIST_HEAD(&p_ptr->wait_list);
	INIT_LIST_HEAD(&p_ptr->subscription.nodesub_list);
	p_ptr->dispatcher = dispatcher;
	p_ptr->wakeup = wakeup;
	k_init_timer(&p_ptr->timer, (Handler)port_timeout, ref);
	INIT_LIST_HEAD(&p_ptr->publications);
	INIT_LIST_HEAD(&p_ptr->port_list);

	/*
	 * Must hold port list lock while initializing message header template
	 * to ensure a change to node's own network address doesn't result
	 * in template containing out-dated network address information
	 */
	spin_lock_bh(&tipc_port_list_lock);
	msg = &p_ptr->phdr;
	tipc_msg_init(msg, importance, TIPC_NAMED_MSG, NAMED_H_SIZE, 0);
	msg_set_origport(msg, ref);
	list_add_tail(&p_ptr->port_list, &ports);
	spin_unlock_bh(&tipc_port_list_lock);
	return p_ptr;
}

int tipc_deleteport(struct tipc_port *p_ptr)
{
	struct sk_buff *buf = NULL;

	tipc_withdraw(p_ptr, 0, NULL);

	spin_lock_bh(p_ptr->lock);
	tipc_ref_discard(p_ptr->ref);
	spin_unlock_bh(p_ptr->lock);

	k_cancel_timer(&p_ptr->timer);
	if (p_ptr->connected) {
		buf = port_build_peer_abort_msg(p_ptr, TIPC_ERR_NO_PORT);
		tipc_nodesub_unsubscribe(&p_ptr->subscription);
	}

	spin_lock_bh(&tipc_port_list_lock);
	list_del(&p_ptr->port_list);
	list_del(&p_ptr->wait_list);
	spin_unlock_bh(&tipc_port_list_lock);
	k_term_timer(&p_ptr->timer);
	kfree(p_ptr);
	tipc_net_route_msg(buf);
	return 0;
}

static int port_unreliable(struct tipc_port *p_ptr)
{
	return msg_src_droppable(&p_ptr->phdr);
}

int tipc_portunreliable(u32 ref, unsigned int *isunreliable)
{
	struct tipc_port *p_ptr;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	*isunreliable = port_unreliable(p_ptr);
	tipc_port_unlock(p_ptr);
	return 0;
}

int tipc_set_portunreliable(u32 ref, unsigned int isunreliable)
{
	struct tipc_port *p_ptr;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	msg_set_src_droppable(&p_ptr->phdr, (isunreliable != 0));
	tipc_port_unlock(p_ptr);
	return 0;
}

static int port_unreturnable(struct tipc_port *p_ptr)
{
	return msg_dest_droppable(&p_ptr->phdr);
}

int tipc_portunreturnable(u32 ref, unsigned int *isunrejectable)
{
	struct tipc_port *p_ptr;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	*isunrejectable = port_unreturnable(p_ptr);
	tipc_port_unlock(p_ptr);
	return 0;
}

int tipc_set_portunreturnable(u32 ref, unsigned int isunrejectable)
{
	struct tipc_port *p_ptr;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	msg_set_dest_droppable(&p_ptr->phdr, (isunrejectable != 0));
	tipc_port_unlock(p_ptr);
	return 0;
}

/*
 * port_build_proto_msg(): create connection protocol message for port
 *
 * On entry the port must be locked and connected.
 */
static struct sk_buff *port_build_proto_msg(struct tipc_port *p_ptr,
					    u32 type, u32 ack)
{
	struct sk_buff *buf;
	struct tipc_msg *msg;

	buf = tipc_buf_acquire(INT_H_SIZE);
	if (buf) {
		msg = buf_msg(buf);
		tipc_msg_init(msg, CONN_MANAGER, type, INT_H_SIZE,
			      port_peernode(p_ptr));
		msg_set_destport(msg, port_peerport(p_ptr));
		msg_set_origport(msg, p_ptr->ref);
		msg_set_msgcnt(msg, ack);
	}
	return buf;
}

int tipc_reject_msg(struct sk_buff *buf, u32 err)
{
	struct tipc_msg *msg = buf_msg(buf);
	struct sk_buff *rbuf;
	struct tipc_msg *rmsg;
	int hdr_sz;
	u32 imp;
	u32 data_sz = msg_data_sz(msg);
	u32 src_node;
	u32 rmsg_sz;

	/* discard rejected message if it shouldn't be returned to sender */
	if (WARN(!msg_isdata(msg),
		 "attempt to reject message with user=%u", msg_user(msg))) {
		dump_stack();
		goto exit;
	}
	if (msg_errcode(msg) || msg_dest_droppable(msg))
		goto exit;

	/*
	 * construct returned message by copying rejected message header and
	 * data (or subset), then updating header fields that need adjusting
	 */
	hdr_sz = msg_hdr_sz(msg);
	rmsg_sz = hdr_sz + min_t(u32, data_sz, MAX_REJECT_SIZE);

	rbuf = tipc_buf_acquire(rmsg_sz);
	if (rbuf == NULL)
		goto exit;

	rmsg = buf_msg(rbuf);
	skb_copy_to_linear_data(rbuf, msg, rmsg_sz);

	if (msg_connected(rmsg)) {
		imp = msg_importance(rmsg);
		if (imp < TIPC_CRITICAL_IMPORTANCE)
			msg_set_importance(rmsg, ++imp);
	}
	msg_set_non_seq(rmsg, 0);
	msg_set_size(rmsg, rmsg_sz);
	msg_set_errcode(rmsg, err);
	msg_set_prevnode(rmsg, tipc_own_addr);
	msg_swap_words(rmsg, 4, 5);
	if (!msg_short(rmsg))
		msg_swap_words(rmsg, 6, 7);

	/* send self-abort message when rejecting on a connected port */
	if (msg_connected(msg)) {
		struct tipc_port *p_ptr = tipc_port_lock(msg_destport(msg));

		if (p_ptr) {
			struct sk_buff *abuf = NULL;

			if (p_ptr->connected)
				abuf = port_build_self_abort_msg(p_ptr, err);
			tipc_port_unlock(p_ptr);
			tipc_net_route_msg(abuf);
		}
	}

	/* send returned message & dispose of rejected message */
	src_node = msg_prevnode(msg);
	if (in_own_node(src_node))
		tipc_port_recv_msg(rbuf);
	else
		tipc_link_send(rbuf, src_node, msg_link_selector(rmsg));
exit:
	kfree_skb(buf);
	return data_sz;
}

int tipc_port_reject_sections(struct tipc_port *p_ptr, struct tipc_msg *hdr,
			      struct iovec const *msg_sect, unsigned int len,
			      int err)
{
	struct sk_buff *buf;
	int res;

	res = tipc_msg_build(hdr, msg_sect, len, MAX_MSG_SIZE, &buf);
	if (!buf)
		return res;

	return tipc_reject_msg(buf, err);
}

static void port_timeout(unsigned long ref)
{
	struct tipc_port *p_ptr = tipc_port_lock(ref);
	struct sk_buff *buf = NULL;

	if (!p_ptr)
		return;

	if (!p_ptr->connected) {
		tipc_port_unlock(p_ptr);
		return;
	}

	/* Last probe answered ? */
	if (p_ptr->probing_state == PROBING) {
		buf = port_build_self_abort_msg(p_ptr, TIPC_ERR_NO_PORT);
	} else {
		buf = port_build_proto_msg(p_ptr, CONN_PROBE, 0);
		p_ptr->probing_state = PROBING;
		k_start_timer(&p_ptr->timer, p_ptr->probing_interval);
	}
	tipc_port_unlock(p_ptr);
	tipc_net_route_msg(buf);
}


static void port_handle_node_down(unsigned long ref)
{
	struct tipc_port *p_ptr = tipc_port_lock(ref);
	struct sk_buff *buf = NULL;

	if (!p_ptr)
		return;
	buf = port_build_self_abort_msg(p_ptr, TIPC_ERR_NO_NODE);
	tipc_port_unlock(p_ptr);
	tipc_net_route_msg(buf);
}


static struct sk_buff *port_build_self_abort_msg(struct tipc_port *p_ptr, u32 err)
{
	struct sk_buff *buf = port_build_peer_abort_msg(p_ptr, err);

	if (buf) {
		struct tipc_msg *msg = buf_msg(buf);
		msg_swap_words(msg, 4, 5);
		msg_swap_words(msg, 6, 7);
	}
	return buf;
}


static struct sk_buff *port_build_peer_abort_msg(struct tipc_port *p_ptr, u32 err)
{
	struct sk_buff *buf;
	struct tipc_msg *msg;
	u32 imp;

	if (!p_ptr->connected)
		return NULL;

	buf = tipc_buf_acquire(BASIC_H_SIZE);
	if (buf) {
		msg = buf_msg(buf);
		memcpy(msg, &p_ptr->phdr, BASIC_H_SIZE);
		msg_set_hdr_sz(msg, BASIC_H_SIZE);
		msg_set_size(msg, BASIC_H_SIZE);
		imp = msg_importance(msg);
		if (imp < TIPC_CRITICAL_IMPORTANCE)
			msg_set_importance(msg, ++imp);
		msg_set_errcode(msg, err);
	}
	return buf;
}

void tipc_port_recv_proto_msg(struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	struct tipc_port *p_ptr;
	struct sk_buff *r_buf = NULL;
	u32 destport = msg_destport(msg);
	int wakeable;

	/* Validate connection */
	p_ptr = tipc_port_lock(destport);
	if (!p_ptr || !p_ptr->connected || !tipc_port_peer_msg(p_ptr, msg)) {
		r_buf = tipc_buf_acquire(BASIC_H_SIZE);
		if (r_buf) {
			msg = buf_msg(r_buf);
			tipc_msg_init(msg, TIPC_HIGH_IMPORTANCE, TIPC_CONN_MSG,
				      BASIC_H_SIZE, msg_orignode(msg));
			msg_set_errcode(msg, TIPC_ERR_NO_PORT);
			msg_set_origport(msg, destport);
			msg_set_destport(msg, msg_origport(msg));
		}
		if (p_ptr)
			tipc_port_unlock(p_ptr);
		goto exit;
	}

	/* Process protocol message sent by peer */
	switch (msg_type(msg)) {
	case CONN_ACK:
		wakeable = tipc_port_congested(p_ptr) && p_ptr->congested &&
			p_ptr->wakeup;
		p_ptr->acked += msg_msgcnt(msg);
		if (!tipc_port_congested(p_ptr)) {
			p_ptr->congested = 0;
			if (wakeable)
				p_ptr->wakeup(p_ptr);
		}
		break;
	case CONN_PROBE:
		r_buf = port_build_proto_msg(p_ptr, CONN_PROBE_REPLY, 0);
		break;
	default:
		/* CONN_PROBE_REPLY or unrecognized - no action required */
		break;
	}
	p_ptr->probing_state = CONFIRMED;
	tipc_port_unlock(p_ptr);
exit:
	tipc_net_route_msg(r_buf);
	kfree_skb(buf);
}

static int port_print(struct tipc_port *p_ptr, char *buf, int len, int full_id)
{
	struct publication *publ;
	int ret;

	if (full_id)
		ret = tipc_snprintf(buf, len, "<%u.%u.%u:%u>:",
				    tipc_zone(tipc_own_addr),
				    tipc_cluster(tipc_own_addr),
				    tipc_node(tipc_own_addr), p_ptr->ref);
	else
		ret = tipc_snprintf(buf, len, "%-10u:", p_ptr->ref);

	if (p_ptr->connected) {
		u32 dport = port_peerport(p_ptr);
		u32 destnode = port_peernode(p_ptr);

		ret += tipc_snprintf(buf + ret, len - ret,
				     " connected to <%u.%u.%u:%u>",
				     tipc_zone(destnode),
				     tipc_cluster(destnode),
				     tipc_node(destnode), dport);
		if (p_ptr->conn_type != 0)
			ret += tipc_snprintf(buf + ret, len - ret,
					     " via {%u,%u}", p_ptr->conn_type,
					     p_ptr->conn_instance);
	} else if (p_ptr->published) {
		ret += tipc_snprintf(buf + ret, len - ret, " bound to");
		list_for_each_entry(publ, &p_ptr->publications, pport_list) {
			if (publ->lower == publ->upper)
				ret += tipc_snprintf(buf + ret, len - ret,
						     " {%u,%u}", publ->type,
						     publ->lower);
			else
				ret += tipc_snprintf(buf + ret, len - ret,
						     " {%u,%u,%u}", publ->type,
						     publ->lower, publ->upper);
		}
	}
	ret += tipc_snprintf(buf + ret, len - ret, "\n");
	return ret;
}

struct sk_buff *tipc_port_get_ports(void)
{
	struct sk_buff *buf;
	struct tlv_desc *rep_tlv;
	char *pb;
	int pb_len;
	struct tipc_port *p_ptr;
	int str_len = 0;

	buf = tipc_cfg_reply_alloc(TLV_SPACE(ULTRA_STRING_MAX_LEN));
	if (!buf)
		return NULL;
	rep_tlv = (struct tlv_desc *)buf->data;
	pb = TLV_DATA(rep_tlv);
	pb_len = ULTRA_STRING_MAX_LEN;

	spin_lock_bh(&tipc_port_list_lock);
	list_for_each_entry(p_ptr, &ports, port_list) {
		spin_lock_bh(p_ptr->lock);
		str_len += port_print(p_ptr, pb, pb_len, 0);
		spin_unlock_bh(p_ptr->lock);
	}
	spin_unlock_bh(&tipc_port_list_lock);
	str_len += 1;	/* for "\0" */
	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

void tipc_port_reinit(void)
{
	struct tipc_port *p_ptr;
	struct tipc_msg *msg;

	spin_lock_bh(&tipc_port_list_lock);
	list_for_each_entry(p_ptr, &ports, port_list) {
		msg = &p_ptr->phdr;
		msg_set_prevnode(msg, tipc_own_addr);
		msg_set_orignode(msg, tipc_own_addr);
	}
	spin_unlock_bh(&tipc_port_list_lock);
}

void tipc_acknowledge(u32 ref, u32 ack)
{
	struct tipc_port *p_ptr;
	struct sk_buff *buf = NULL;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return;
	if (p_ptr->connected) {
		p_ptr->conn_unacked -= ack;
		buf = port_build_proto_msg(p_ptr, CONN_ACK, ack);
	}
	tipc_port_unlock(p_ptr);
	tipc_net_route_msg(buf);
}

int tipc_portimportance(u32 ref, unsigned int *importance)
{
	struct tipc_port *p_ptr;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	*importance = (unsigned int)msg_importance(&p_ptr->phdr);
	tipc_port_unlock(p_ptr);
	return 0;
}

int tipc_set_portimportance(u32 ref, unsigned int imp)
{
	struct tipc_port *p_ptr;

	if (imp > TIPC_CRITICAL_IMPORTANCE)
		return -EINVAL;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	msg_set_importance(&p_ptr->phdr, (u32)imp);
	tipc_port_unlock(p_ptr);
	return 0;
}


int tipc_publish(struct tipc_port *p_ptr, unsigned int scope,
		 struct tipc_name_seq const *seq)
{
	struct publication *publ;
	u32 key;

	if (p_ptr->connected)
		return -EINVAL;
	key = p_ptr->ref + p_ptr->pub_count + 1;
	if (key == p_ptr->ref)
		return -EADDRINUSE;

	publ = tipc_nametbl_publish(seq->type, seq->lower, seq->upper,
				    scope, p_ptr->ref, key);
	if (publ) {
		list_add(&publ->pport_list, &p_ptr->publications);
		p_ptr->pub_count++;
		p_ptr->published = 1;
		return 0;
	}
	return -EINVAL;
}

int tipc_withdraw(struct tipc_port *p_ptr, unsigned int scope,
		  struct tipc_name_seq const *seq)
{
	struct publication *publ;
	struct publication *tpubl;
	int res = -EINVAL;

	if (!seq) {
		list_for_each_entry_safe(publ, tpubl,
					 &p_ptr->publications, pport_list) {
			tipc_nametbl_withdraw(publ->type, publ->lower,
					      publ->ref, publ->key);
		}
		res = 0;
	} else {
		list_for_each_entry_safe(publ, tpubl,
					 &p_ptr->publications, pport_list) {
			if (publ->scope != scope)
				continue;
			if (publ->type != seq->type)
				continue;
			if (publ->lower != seq->lower)
				continue;
			if (publ->upper != seq->upper)
				break;
			tipc_nametbl_withdraw(publ->type, publ->lower,
					      publ->ref, publ->key);
			res = 0;
			break;
		}
	}
	if (list_empty(&p_ptr->publications))
		p_ptr->published = 0;
	return res;
}

int tipc_connect(u32 ref, struct tipc_portid const *peer)
{
	struct tipc_port *p_ptr;
	int res;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	res = __tipc_connect(ref, p_ptr, peer);
	tipc_port_unlock(p_ptr);
	return res;
}

/*
 * __tipc_connect - connect to a remote peer
 *
 * Port must be locked.
 */
int __tipc_connect(u32 ref, struct tipc_port *p_ptr,
			struct tipc_portid const *peer)
{
	struct tipc_msg *msg;
	int res = -EINVAL;

	if (p_ptr->published || p_ptr->connected)
		goto exit;
	if (!peer->ref)
		goto exit;

	msg = &p_ptr->phdr;
	msg_set_destnode(msg, peer->node);
	msg_set_destport(msg, peer->ref);
	msg_set_type(msg, TIPC_CONN_MSG);
	msg_set_lookup_scope(msg, 0);
	msg_set_hdr_sz(msg, SHORT_H_SIZE);

	p_ptr->probing_interval = PROBING_INTERVAL;
	p_ptr->probing_state = CONFIRMED;
	p_ptr->connected = 1;
	k_start_timer(&p_ptr->timer, p_ptr->probing_interval);

	tipc_nodesub_subscribe(&p_ptr->subscription, peer->node,
			  (void *)(unsigned long)ref,
			  (net_ev_handler)port_handle_node_down);
	res = 0;
exit:
	p_ptr->max_pkt = tipc_link_get_max_pkt(peer->node, ref);
	return res;
}

/*
 * __tipc_disconnect - disconnect port from peer
 *
 * Port must be locked.
 */
int __tipc_disconnect(struct tipc_port *tp_ptr)
{
	int res;

	if (tp_ptr->connected) {
		tp_ptr->connected = 0;
		/* let timer expire on it's own to avoid deadlock! */
		tipc_nodesub_unsubscribe(&tp_ptr->subscription);
		res = 0;
	} else {
		res = -ENOTCONN;
	}
	return res;
}

/*
 * tipc_disconnect(): Disconnect port form peer.
 *                    This is a node local operation.
 */
int tipc_disconnect(u32 ref)
{
	struct tipc_port *p_ptr;
	int res;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	res = __tipc_disconnect(p_ptr);
	tipc_port_unlock(p_ptr);
	return res;
}

/*
 * tipc_shutdown(): Send a SHUTDOWN msg to peer and disconnect
 */
int tipc_shutdown(u32 ref)
{
	struct tipc_port *p_ptr;
	struct sk_buff *buf = NULL;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;

	buf = port_build_peer_abort_msg(p_ptr, TIPC_CONN_SHUTDOWN);
	tipc_port_unlock(p_ptr);
	tipc_net_route_msg(buf);
	return tipc_disconnect(ref);
}

/**
 * tipc_port_recv_msg - receive message from lower layer and deliver to port user
 */
int tipc_port_recv_msg(struct sk_buff *buf)
{
	struct tipc_port *p_ptr;
	struct tipc_msg *msg = buf_msg(buf);
	u32 destport = msg_destport(msg);
	u32 dsz = msg_data_sz(msg);
	u32 err;

	/* forward unresolved named message */
	if (unlikely(!destport)) {
		tipc_net_route_msg(buf);
		return dsz;
	}

	/* validate destination & pass to port, otherwise reject message */
	p_ptr = tipc_port_lock(destport);
	if (likely(p_ptr)) {
		err = p_ptr->dispatcher(p_ptr, buf);
		tipc_port_unlock(p_ptr);
		if (likely(!err))
			return dsz;
	} else {
		err = TIPC_ERR_NO_PORT;
	}

	return tipc_reject_msg(buf, err);
}

/*
 *  tipc_port_recv_sections(): Concatenate and deliver sectioned
 *                        message for this node.
 */
static int tipc_port_recv_sections(struct tipc_port *sender,
				   struct iovec const *msg_sect,
				   unsigned int len)
{
	struct sk_buff *buf;
	int res;

	res = tipc_msg_build(&sender->phdr, msg_sect, len, MAX_MSG_SIZE, &buf);
	if (likely(buf))
		tipc_port_recv_msg(buf);
	return res;
}

/**
 * tipc_send - send message sections on connection
 */
int tipc_send(u32 ref, struct iovec const *msg_sect, unsigned int len)
{
	struct tipc_port *p_ptr;
	u32 destnode;
	int res;

	p_ptr = tipc_port_deref(ref);
	if (!p_ptr || !p_ptr->connected)
		return -EINVAL;

	p_ptr->congested = 1;
	if (!tipc_port_congested(p_ptr)) {
		destnode = port_peernode(p_ptr);
		if (likely(!in_own_node(destnode)))
			res = tipc_link_send_sections_fast(p_ptr, msg_sect,
							   len, destnode);
		else
			res = tipc_port_recv_sections(p_ptr, msg_sect, len);

		if (likely(res != -ELINKCONG)) {
			p_ptr->congested = 0;
			if (res > 0)
				p_ptr->sent++;
			return res;
		}
	}
	if (port_unreliable(p_ptr)) {
		p_ptr->congested = 0;
		return len;
	}
	return -ELINKCONG;
}

/**
 * tipc_send2name - send message sections to port name
 */
int tipc_send2name(u32 ref, struct tipc_name const *name, unsigned int domain,
		   struct iovec const *msg_sect, unsigned int len)
{
	struct tipc_port *p_ptr;
	struct tipc_msg *msg;
	u32 destnode = domain;
	u32 destport;
	int res;

	p_ptr = tipc_port_deref(ref);
	if (!p_ptr || p_ptr->connected)
		return -EINVAL;

	msg = &p_ptr->phdr;
	msg_set_type(msg, TIPC_NAMED_MSG);
	msg_set_hdr_sz(msg, NAMED_H_SIZE);
	msg_set_nametype(msg, name->type);
	msg_set_nameinst(msg, name->instance);
	msg_set_lookup_scope(msg, tipc_addr_scope(domain));
	destport = tipc_nametbl_translate(name->type, name->instance, &destnode);
	msg_set_destnode(msg, destnode);
	msg_set_destport(msg, destport);

	if (likely(destport || destnode)) {
		if (likely(in_own_node(destnode)))
			res = tipc_port_recv_sections(p_ptr, msg_sect, len);
		else if (tipc_own_addr)
			res = tipc_link_send_sections_fast(p_ptr, msg_sect,
							   len, destnode);
		else
			res = tipc_port_reject_sections(p_ptr, msg, msg_sect,
							len, TIPC_ERR_NO_NODE);
		if (likely(res != -ELINKCONG)) {
			if (res > 0)
				p_ptr->sent++;
			return res;
		}
		if (port_unreliable(p_ptr)) {
			return len;
		}
		return -ELINKCONG;
	}
	return tipc_port_reject_sections(p_ptr, msg, msg_sect, len,
					 TIPC_ERR_NO_NAME);
}

/**
 * tipc_send2port - send message sections to port identity
 */
int tipc_send2port(u32 ref, struct tipc_portid const *dest,
		   struct iovec const *msg_sect, unsigned int len)
{
	struct tipc_port *p_ptr;
	struct tipc_msg *msg;
	int res;

	p_ptr = tipc_port_deref(ref);
	if (!p_ptr || p_ptr->connected)
		return -EINVAL;

	msg = &p_ptr->phdr;
	msg_set_type(msg, TIPC_DIRECT_MSG);
	msg_set_lookup_scope(msg, 0);
	msg_set_destnode(msg, dest->node);
	msg_set_destport(msg, dest->ref);
	msg_set_hdr_sz(msg, BASIC_H_SIZE);

	if (in_own_node(dest->node))
		res =  tipc_port_recv_sections(p_ptr, msg_sect, len);
	else if (tipc_own_addr)
		res = tipc_link_send_sections_fast(p_ptr, msg_sect, len,
						   dest->node);
	else
		res = tipc_port_reject_sections(p_ptr, msg, msg_sect, len,
						TIPC_ERR_NO_NODE);
	if (likely(res != -ELINKCONG)) {
		if (res > 0)
			p_ptr->sent++;
		return res;
	}
	if (port_unreliable(p_ptr)) {
		return len;
	}
	return -ELINKCONG;
}

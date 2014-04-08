/*
 * net/tipc/port.h: Include file for TIPC port code
 *
 * Copyright (c) 1994-2007, Ericsson AB
 * Copyright (c) 2004-2007, 2010-2013, Wind River Systems
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

#ifndef _TIPC_PORT_H
#define _TIPC_PORT_H

#include "ref.h"
#include "net.h"
#include "msg.h"
#include "node_subscr.h"

#define TIPC_FLOW_CONTROL_WIN 512
#define CONN_OVERLOAD_LIMIT	((TIPC_FLOW_CONTROL_WIN * 2 + 1) * \
				SKB_TRUESIZE(TIPC_MAX_USER_MSG_SIZE))

/**
 * struct tipc_port - TIPC port structure
 * @sk: pointer to socket handle
 * @lock: pointer to spinlock for controlling access to port
 * @connected: non-zero if port is currently connected to a peer port
 * @conn_type: TIPC type used when connection was established
 * @conn_instance: TIPC instance used when connection was established
 * @conn_unacked: number of unacknowledged messages received from peer port
 * @published: non-zero if port has one or more associated names
 * @congested: non-zero if cannot send because of link or port congestion
 * @max_pkt: maximum packet size "hint" used when building messages sent by port
 * @ref: unique reference to port in TIPC object registry
 * @phdr: preformatted message header used when sending messages
 * @port_list: adjacent ports in TIPC's global list of ports
 * @dispatcher: ptr to routine which handles received messages
 * @wakeup: ptr to routine to call when port is no longer congested
 * @wait_list: adjacent ports in list of ports waiting on link congestion
 * @waiting_pkts:
 * @sent: # of non-empty messages sent by port
 * @acked: # of non-empty message acknowledgements from connected port's peer
 * @publications: list of publications for port
 * @pub_count: total # of publications port has made during its lifetime
 * @probing_state:
 * @probing_interval:
 * @timer_ref:
 * @subscription: "node down" subscription used to terminate failed connections
 */
struct tipc_port {
	struct sock *sk;
	spinlock_t *lock;
	int connected;
	u32 conn_type;
	u32 conn_instance;
	u32 conn_unacked;
	int published;
	u32 congested;
	u32 max_pkt;
	u32 ref;
	struct tipc_msg phdr;
	struct list_head port_list;
	u32 (*dispatcher)(struct tipc_port *, struct sk_buff *);
	void (*wakeup)(struct tipc_port *);
	struct list_head wait_list;
	u32 waiting_pkts;
	u32 sent;
	u32 acked;
	struct list_head publications;
	u32 pub_count;
	u32 probing_state;
	u32 probing_interval;
	struct timer_list timer;
	struct tipc_node_subscr subscription;
};

extern spinlock_t tipc_port_list_lock;
struct tipc_port_list;

/*
 * TIPC port manipulation routines
 */
struct tipc_port *tipc_createport(struct sock *sk,
				  u32 (*dispatcher)(struct tipc_port *,
				  struct sk_buff *),
				  void (*wakeup)(struct tipc_port *),
				  const u32 importance);

int tipc_reject_msg(struct sk_buff *buf, u32 err);

void tipc_acknowledge(u32 port_ref, u32 ack);

int tipc_deleteport(struct tipc_port *p_ptr);

int tipc_portimportance(u32 portref, unsigned int *importance);
int tipc_set_portimportance(u32 portref, unsigned int importance);

int tipc_portunreliable(u32 portref, unsigned int *isunreliable);
int tipc_set_portunreliable(u32 portref, unsigned int isunreliable);

int tipc_portunreturnable(u32 portref, unsigned int *isunreturnable);
int tipc_set_portunreturnable(u32 portref, unsigned int isunreturnable);

int tipc_publish(struct tipc_port *p_ptr, unsigned int scope,
		 struct tipc_name_seq const *name_seq);
int tipc_withdraw(struct tipc_port *p_ptr, unsigned int scope,
		  struct tipc_name_seq const *name_seq);

int tipc_connect(u32 portref, struct tipc_portid const *port);

int tipc_disconnect(u32 portref);

int tipc_shutdown(u32 ref);


/*
 * The following routines require that the port be locked on entry
 */
int __tipc_disconnect(struct tipc_port *tp_ptr);
int __tipc_connect(u32 ref, struct tipc_port *p_ptr,
		   struct tipc_portid const *peer);
int tipc_port_peer_msg(struct tipc_port *p_ptr, struct tipc_msg *msg);

/*
 * TIPC messaging routines
 */
int tipc_port_recv_msg(struct sk_buff *buf);
int tipc_send(u32 portref, struct iovec const *msg_sect, unsigned int len);

int tipc_send2name(u32 portref, struct tipc_name const *name, u32 domain,
		   struct iovec const *msg_sect, unsigned int len);

int tipc_send2port(u32 portref, struct tipc_portid const *dest,
		   struct iovec const *msg_sect, unsigned int len);

int tipc_multicast(u32 portref, struct tipc_name_seq const *seq,
		   struct iovec const *msg, unsigned int len);

int tipc_port_reject_sections(struct tipc_port *p_ptr, struct tipc_msg *hdr,
			      struct iovec const *msg_sect, unsigned int len,
			      int err);
struct sk_buff *tipc_port_get_ports(void);
void tipc_port_recv_proto_msg(struct sk_buff *buf);
void tipc_port_recv_mcast(struct sk_buff *buf, struct tipc_port_list *dp);
void tipc_port_reinit(void);

/**
 * tipc_port_lock - lock port instance referred to and return its pointer
 */
static inline struct tipc_port *tipc_port_lock(u32 ref)
{
	return (struct tipc_port *)tipc_ref_lock(ref);
}

/**
 * tipc_port_unlock - unlock a port instance
 *
 * Can use pointer instead of tipc_ref_unlock() since port is already locked.
 */
static inline void tipc_port_unlock(struct tipc_port *p_ptr)
{
	spin_unlock_bh(p_ptr->lock);
}

static inline struct tipc_port *tipc_port_deref(u32 ref)
{
	return (struct tipc_port *)tipc_ref_deref(ref);
}

static inline int tipc_port_congested(struct tipc_port *p_ptr)
{
	return (p_ptr->sent - p_ptr->acked) >= (TIPC_FLOW_CONTROL_WIN * 2);
}

#endif

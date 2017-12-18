/* RxRPC virtual connection handler, common bits.
 *
 * Copyright (C) 2007, 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include "ar-internal.h"

/*
 * Time till a connection expires after last use (in seconds).
 */
unsigned int rxrpc_connection_expiry = 10 * 60;

static void rxrpc_connection_reaper(struct work_struct *work);

LIST_HEAD(rxrpc_connections);
LIST_HEAD(rxrpc_connection_proc_list);
DEFINE_RWLOCK(rxrpc_connection_lock);
static DECLARE_DELAYED_WORK(rxrpc_connection_reap, rxrpc_connection_reaper);

static void rxrpc_destroy_connection(struct rcu_head *);

/*
 * allocate a new connection
 */
struct rxrpc_connection *rxrpc_alloc_connection(gfp_t gfp)
{
	struct rxrpc_connection *conn;

	_enter("");

	conn = kzalloc(sizeof(struct rxrpc_connection), gfp);
	if (conn) {
		INIT_LIST_HEAD(&conn->cache_link);
		spin_lock_init(&conn->channel_lock);
		INIT_LIST_HEAD(&conn->waiting_calls);
		INIT_WORK(&conn->processor, &rxrpc_process_connection);
		INIT_LIST_HEAD(&conn->proc_link);
		INIT_LIST_HEAD(&conn->link);
		skb_queue_head_init(&conn->rx_queue);
		conn->security = &rxrpc_no_security;
		spin_lock_init(&conn->state_lock);
		conn->debug_id = atomic_inc_return(&rxrpc_debug_id);
		conn->size_align = 4;
		conn->idle_timestamp = jiffies;
	}

	_leave(" = %p{%d}", conn, conn ? conn->debug_id : 0);
	return conn;
}

/*
 * Look up a connection in the cache by protocol parameters.
 *
 * If successful, a pointer to the connection is returned, but no ref is taken.
 * NULL is returned if there is no match.
 *
 * The caller must be holding the RCU read lock.
 */
struct rxrpc_connection *rxrpc_find_connection_rcu(struct rxrpc_local *local,
						   struct sk_buff *skb)
{
	struct rxrpc_connection *conn;
	struct rxrpc_conn_proto k;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct sockaddr_rxrpc srx;
	struct rxrpc_peer *peer;

	_enter(",%x", sp->hdr.cid & RXRPC_CIDMASK);

	if (rxrpc_extract_addr_from_skb(&srx, skb) < 0)
		goto not_found;

	k.epoch	= sp->hdr.epoch;
	k.cid	= sp->hdr.cid & RXRPC_CIDMASK;

	/* We may have to handle mixing IPv4 and IPv6 */
	if (srx.transport.family != local->srx.transport.family) {
		pr_warn_ratelimited("AF_RXRPC: Protocol mismatch %u not %u\n",
				    srx.transport.family,
				    local->srx.transport.family);
		goto not_found;
	}

	k.epoch	= sp->hdr.epoch;
	k.cid	= sp->hdr.cid & RXRPC_CIDMASK;

	if (sp->hdr.flags & RXRPC_CLIENT_INITIATED) {
		/* We need to look up service connections by the full protocol
		 * parameter set.  We look up the peer first as an intermediate
		 * step and then the connection from the peer's tree.
		 */
		peer = rxrpc_lookup_peer_rcu(local, &srx);
		if (!peer)
			goto not_found;
		conn = rxrpc_find_service_conn_rcu(peer, skb);
		if (!conn || atomic_read(&conn->usage) == 0)
			goto not_found;
		_leave(" = %p", conn);
		return conn;
	} else {
		/* Look up client connections by connection ID alone as their
		 * IDs are unique for this machine.
		 */
		conn = idr_find(&rxrpc_client_conn_ids,
				sp->hdr.cid >> RXRPC_CIDSHIFT);
		if (!conn || atomic_read(&conn->usage) == 0) {
			_debug("no conn");
			goto not_found;
		}

		if (conn->proto.epoch != k.epoch ||
		    conn->params.local != local)
			goto not_found;

		peer = conn->params.peer;
		switch (srx.transport.family) {
		case AF_INET:
			if (peer->srx.transport.sin.sin_port !=
			    srx.transport.sin.sin_port ||
			    peer->srx.transport.sin.sin_addr.s_addr !=
			    srx.transport.sin.sin_addr.s_addr)
				goto not_found;
			break;
#ifdef CONFIG_AF_RXRPC_IPV6
		case AF_INET6:
			if (peer->srx.transport.sin6.sin6_port !=
			    srx.transport.sin6.sin6_port ||
			    memcmp(&peer->srx.transport.sin6.sin6_addr,
				   &srx.transport.sin6.sin6_addr,
				   sizeof(struct in6_addr)) != 0)
				goto not_found;
			break;
#endif
		default:
			BUG();
		}

		_leave(" = %p", conn);
		return conn;
	}

not_found:
	_leave(" = NULL");
	return NULL;
}

/*
 * Disconnect a call and clear any channel it occupies when that call
 * terminates.  The caller must hold the channel_lock and must release the
 * call's ref on the connection.
 */
void __rxrpc_disconnect_call(struct rxrpc_connection *conn,
			     struct rxrpc_call *call)
{
	struct rxrpc_channel *chan =
		&conn->channels[call->cid & RXRPC_CHANNELMASK];

	_enter("%d,%x", conn->debug_id, call->cid);

	if (rcu_access_pointer(chan->call) == call) {
		/* Save the result of the call so that we can repeat it if necessary
		 * through the channel, whilst disposing of the actual call record.
		 */
		chan->last_service_id = call->service_id;
		if (call->abort_code) {
			chan->last_abort = call->abort_code;
			chan->last_type = RXRPC_PACKET_TYPE_ABORT;
		} else {
			chan->last_seq = call->rx_hard_ack;
			chan->last_type = RXRPC_PACKET_TYPE_ACK;
		}
		/* Sync with rxrpc_conn_retransmit(). */
		smp_wmb();
		chan->last_call = chan->call_id;
		chan->call_id = chan->call_counter;

		rcu_assign_pointer(chan->call, NULL);
	}

	_leave("");
}

/*
 * Disconnect a call and clear any channel it occupies when that call
 * terminates.
 */
void rxrpc_disconnect_call(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = call->conn;

	spin_lock_bh(&conn->params.peer->lock);
	hlist_del_init(&call->error_link);
	spin_unlock_bh(&conn->params.peer->lock);

	if (rxrpc_is_client_call(call))
		return rxrpc_disconnect_client_call(call);

	spin_lock(&conn->channel_lock);
	__rxrpc_disconnect_call(conn, call);
	spin_unlock(&conn->channel_lock);

	call->conn = NULL;
	conn->idle_timestamp = jiffies;
	rxrpc_put_connection(conn);
}

/*
 * Kill off a connection.
 */
void rxrpc_kill_connection(struct rxrpc_connection *conn)
{
	ASSERT(!rcu_access_pointer(conn->channels[0].call) &&
	       !rcu_access_pointer(conn->channels[1].call) &&
	       !rcu_access_pointer(conn->channels[2].call) &&
	       !rcu_access_pointer(conn->channels[3].call));
	ASSERT(list_empty(&conn->cache_link));

	write_lock(&rxrpc_connection_lock);
	list_del_init(&conn->proc_link);
	write_unlock(&rxrpc_connection_lock);

	/* Drain the Rx queue.  Note that even though we've unpublished, an
	 * incoming packet could still be being added to our Rx queue, so we
	 * will need to drain it again in the RCU cleanup handler.
	 */
	rxrpc_purge_queue(&conn->rx_queue);

	/* Leave final destruction to RCU.  The connection processor work item
	 * must carry a ref on the connection to prevent us getting here whilst
	 * it is queued or running.
	 */
	call_rcu(&conn->rcu, rxrpc_destroy_connection);
}

/*
 * Queue a connection's work processor, getting a ref to pass to the work
 * queue.
 */
bool rxrpc_queue_conn(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	int n = __atomic_add_unless(&conn->usage, 1, 0);
	if (n == 0)
		return false;
	if (rxrpc_queue_work(&conn->processor))
		trace_rxrpc_conn(conn, rxrpc_conn_queued, n + 1, here);
	else
		rxrpc_put_connection(conn);
	return true;
}

/*
 * Note the re-emergence of a connection.
 */
void rxrpc_see_connection(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	if (conn) {
		int n = atomic_read(&conn->usage);

		trace_rxrpc_conn(conn, rxrpc_conn_seen, n, here);
	}
}

/*
 * Get a ref on a connection.
 */
void rxrpc_get_connection(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(&conn->usage);

	trace_rxrpc_conn(conn, rxrpc_conn_got, n, here);
}

/*
 * Try to get a ref on a connection.
 */
struct rxrpc_connection *
rxrpc_get_connection_maybe(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);

	if (conn) {
		int n = __atomic_add_unless(&conn->usage, 1, 0);
		if (n > 0)
			trace_rxrpc_conn(conn, rxrpc_conn_got, n + 1, here);
		else
			conn = NULL;
	}
	return conn;
}

/*
 * Release a service connection
 */
void rxrpc_put_service_conn(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	int n;

	n = atomic_dec_return(&conn->usage);
	trace_rxrpc_conn(conn, rxrpc_conn_put_service, n, here);
	ASSERTCMP(n, >=, 0);
	if (n == 0)
		rxrpc_queue_delayed_work(&rxrpc_connection_reap, 0);
}

/*
 * destroy a virtual connection
 */
static void rxrpc_destroy_connection(struct rcu_head *rcu)
{
	struct rxrpc_connection *conn =
		container_of(rcu, struct rxrpc_connection, rcu);

	_enter("{%d,u=%d}", conn->debug_id, atomic_read(&conn->usage));

	ASSERTCMP(atomic_read(&conn->usage), ==, 0);

	_net("DESTROY CONN %d", conn->debug_id);

	rxrpc_purge_queue(&conn->rx_queue);

	conn->security->clear(conn);
	key_put(conn->params.key);
	key_put(conn->server_key);
	rxrpc_put_peer(conn->params.peer);
	rxrpc_put_local(conn->params.local);

	kfree(conn);
	_leave("");
}

/*
 * reap dead service connections
 */
static void rxrpc_connection_reaper(struct work_struct *work)
{
	struct rxrpc_connection *conn, *_p;
	unsigned long reap_older_than, earliest, idle_timestamp, now;

	LIST_HEAD(graveyard);

	_enter("");

	now = jiffies;
	reap_older_than = now - rxrpc_connection_expiry * HZ;
	earliest = ULONG_MAX;

	write_lock(&rxrpc_connection_lock);
	list_for_each_entry_safe(conn, _p, &rxrpc_connections, link) {
		ASSERTCMP(atomic_read(&conn->usage), >, 0);
		if (likely(atomic_read(&conn->usage) > 1))
			continue;
		if (conn->state == RXRPC_CONN_SERVICE_PREALLOC)
			continue;

		idle_timestamp = READ_ONCE(conn->idle_timestamp);
		_debug("reap CONN %d { u=%d,t=%ld }",
		       conn->debug_id, atomic_read(&conn->usage),
		       (long)reap_older_than - (long)idle_timestamp);

		if (time_after(idle_timestamp, reap_older_than)) {
			if (time_before(idle_timestamp, earliest))
				earliest = idle_timestamp;
			continue;
		}

		/* The usage count sits at 1 whilst the object is unused on the
		 * list; we reduce that to 0 to make the object unavailable.
		 */
		if (atomic_cmpxchg(&conn->usage, 1, 0) != 1)
			continue;

		if (rxrpc_conn_is_client(conn))
			BUG();
		else
			rxrpc_unpublish_service_conn(conn);

		list_move_tail(&conn->link, &graveyard);
	}
	write_unlock(&rxrpc_connection_lock);

	if (earliest != ULONG_MAX) {
		_debug("reschedule reaper %ld", (long) earliest - now);
		ASSERT(time_after(earliest, now));
		rxrpc_queue_delayed_work(&rxrpc_connection_reap,
					 earliest - now);
	}

	while (!list_empty(&graveyard)) {
		conn = list_entry(graveyard.next, struct rxrpc_connection,
				  link);
		list_del_init(&conn->link);

		ASSERTCMP(atomic_read(&conn->usage), ==, 0);
		rxrpc_kill_connection(conn);
	}

	_leave("");
}

/*
 * preemptively destroy all the service connection records rather than
 * waiting for them to time out
 */
void __exit rxrpc_destroy_all_connections(void)
{
	struct rxrpc_connection *conn, *_p;
	bool leak = false;

	_enter("");

	rxrpc_destroy_all_client_connections();

	rxrpc_connection_expiry = 0;
	cancel_delayed_work(&rxrpc_connection_reap);
	rxrpc_queue_delayed_work(&rxrpc_connection_reap, 0);
	flush_workqueue(rxrpc_workqueue);

	write_lock(&rxrpc_connection_lock);
	list_for_each_entry_safe(conn, _p, &rxrpc_connections, link) {
		pr_err("AF_RXRPC: Leaked conn %p {%d}\n",
		       conn, atomic_read(&conn->usage));
		leak = true;
	}
	write_unlock(&rxrpc_connection_lock);
	BUG_ON(leak);

	ASSERT(list_empty(&rxrpc_connection_proc_list));

	/* Make sure the local and peer records pinned by any dying connections
	 * are released.
	 */
	rcu_barrier();
	rxrpc_destroy_client_conn_ids();

	_leave("");
}

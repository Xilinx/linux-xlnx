/* Client connection-specific management code.
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 *
 *
 * Client connections need to be cached for a little while after they've made a
 * call so as to handle retransmitted DATA packets in case the server didn't
 * receive the final ACK or terminating ABORT we sent it.
 *
 * Client connections can be in one of a number of cache states:
 *
 *  (1) INACTIVE - The connection is not held in any list and may not have been
 *      exposed to the world.  If it has been previously exposed, it was
 *      discarded from the idle list after expiring.
 *
 *  (2) WAITING - The connection is waiting for the number of client conns to
 *      drop below the maximum capacity.  Calls may be in progress upon it from
 *      when it was active and got culled.
 *
 *	The connection is on the rxrpc_waiting_client_conns list which is kept
 *	in to-be-granted order.  Culled conns with waiters go to the back of
 *	the queue just like new conns.
 *
 *  (3) ACTIVE - The connection has at least one call in progress upon it, it
 *      may freely grant available channels to new calls and calls may be
 *      waiting on it for channels to become available.
 *
 *	The connection is on the rxrpc_active_client_conns list which is kept
 *	in activation order for culling purposes.
 *
 *	rxrpc_nr_active_client_conns is held incremented also.
 *
 *  (4) CULLED - The connection got summarily culled to try and free up
 *      capacity.  Calls currently in progress on the connection are allowed to
 *      continue, but new calls will have to wait.  There can be no waiters in
 *      this state - the conn would have to go to the WAITING state instead.
 *
 *  (5) IDLE - The connection has no calls in progress upon it and must have
 *      been exposed to the world (ie. the EXPOSED flag must be set).  When it
 *      expires, the EXPOSED flag is cleared and the connection transitions to
 *      the INACTIVE state.
 *
 *	The connection is on the rxrpc_idle_client_conns list which is kept in
 *	order of how soon they'll expire.
 *
 * There are flags of relevance to the cache:
 *
 *  (1) EXPOSED - The connection ID got exposed to the world.  If this flag is
 *      set, an extra ref is added to the connection preventing it from being
 *      reaped when it has no calls outstanding.  This flag is cleared and the
 *      ref dropped when a conn is discarded from the idle list.
 *
 *      This allows us to move terminal call state retransmission to the
 *      connection and to discard the call immediately we think it is done
 *      with.  It also give us a chance to reuse the connection.
 *
 *  (2) DONT_REUSE - The connection should be discarded as soon as possible and
 *      should not be reused.  This is set when an exclusive connection is used
 *      or a call ID counter overflows.
 *
 * The caching state may only be changed if the cache lock is held.
 *
 * There are two idle client connection expiry durations.  If the total number
 * of connections is below the reap threshold, we use the normal duration; if
 * it's above, we use the fast duration.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/timer.h>
#include "ar-internal.h"

__read_mostly unsigned int rxrpc_max_client_connections = 1000;
__read_mostly unsigned int rxrpc_reap_client_connections = 900;
__read_mostly unsigned int rxrpc_conn_idle_client_expiry = 2 * 60 * HZ;
__read_mostly unsigned int rxrpc_conn_idle_client_fast_expiry = 2 * HZ;

static unsigned int rxrpc_nr_client_conns;
static unsigned int rxrpc_nr_active_client_conns;
static __read_mostly bool rxrpc_kill_all_client_conns;

static DEFINE_SPINLOCK(rxrpc_client_conn_cache_lock);
static DEFINE_SPINLOCK(rxrpc_client_conn_discard_mutex);
static LIST_HEAD(rxrpc_waiting_client_conns);
static LIST_HEAD(rxrpc_active_client_conns);
static LIST_HEAD(rxrpc_idle_client_conns);

/*
 * We use machine-unique IDs for our client connections.
 */
DEFINE_IDR(rxrpc_client_conn_ids);
static DEFINE_SPINLOCK(rxrpc_conn_id_lock);

static void rxrpc_cull_active_client_conns(void);
static void rxrpc_discard_expired_client_conns(struct work_struct *);

static DECLARE_DELAYED_WORK(rxrpc_client_conn_reap,
			    rxrpc_discard_expired_client_conns);

const char rxrpc_conn_cache_states[RXRPC_CONN__NR_CACHE_STATES][5] = {
	[RXRPC_CONN_CLIENT_INACTIVE]	= "Inac",
	[RXRPC_CONN_CLIENT_WAITING]	= "Wait",
	[RXRPC_CONN_CLIENT_ACTIVE]	= "Actv",
	[RXRPC_CONN_CLIENT_CULLED]	= "Cull",
	[RXRPC_CONN_CLIENT_IDLE]	= "Idle",
};

/*
 * Get a connection ID and epoch for a client connection from the global pool.
 * The connection struct pointer is then recorded in the idr radix tree.  The
 * epoch doesn't change until the client is rebooted (or, at least, unless the
 * module is unloaded).
 */
static int rxrpc_get_client_connection_id(struct rxrpc_connection *conn,
					  gfp_t gfp)
{
	int id;

	_enter("");

	idr_preload(gfp);
	spin_lock(&rxrpc_conn_id_lock);

	id = idr_alloc_cyclic(&rxrpc_client_conn_ids, conn,
			      1, 0x40000000, GFP_NOWAIT);
	if (id < 0)
		goto error;

	spin_unlock(&rxrpc_conn_id_lock);
	idr_preload_end();

	conn->proto.epoch = rxrpc_epoch;
	conn->proto.cid = id << RXRPC_CIDSHIFT;
	set_bit(RXRPC_CONN_HAS_IDR, &conn->flags);
	_leave(" [CID %x]", conn->proto.cid);
	return 0;

error:
	spin_unlock(&rxrpc_conn_id_lock);
	idr_preload_end();
	_leave(" = %d", id);
	return id;
}

/*
 * Release a connection ID for a client connection from the global pool.
 */
static void rxrpc_put_client_connection_id(struct rxrpc_connection *conn)
{
	if (test_bit(RXRPC_CONN_HAS_IDR, &conn->flags)) {
		spin_lock(&rxrpc_conn_id_lock);
		idr_remove(&rxrpc_client_conn_ids,
			   conn->proto.cid >> RXRPC_CIDSHIFT);
		spin_unlock(&rxrpc_conn_id_lock);
	}
}

/*
 * Destroy the client connection ID tree.
 */
void rxrpc_destroy_client_conn_ids(void)
{
	struct rxrpc_connection *conn;
	int id;

	if (!idr_is_empty(&rxrpc_client_conn_ids)) {
		idr_for_each_entry(&rxrpc_client_conn_ids, conn, id) {
			pr_err("AF_RXRPC: Leaked client conn %p {%d}\n",
			       conn, atomic_read(&conn->usage));
		}
		BUG();
	}

	idr_destroy(&rxrpc_client_conn_ids);
}

/*
 * Allocate a client connection.
 */
static struct rxrpc_connection *
rxrpc_alloc_client_connection(struct rxrpc_conn_parameters *cp, gfp_t gfp)
{
	struct rxrpc_connection *conn;
	int ret;

	_enter("");

	conn = rxrpc_alloc_connection(gfp);
	if (!conn) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	atomic_set(&conn->usage, 1);
	if (cp->exclusive)
		__set_bit(RXRPC_CONN_DONT_REUSE, &conn->flags);

	conn->params		= *cp;
	conn->out_clientflag	= RXRPC_CLIENT_INITIATED;
	conn->state		= RXRPC_CONN_CLIENT;

	ret = rxrpc_get_client_connection_id(conn, gfp);
	if (ret < 0)
		goto error_0;

	ret = rxrpc_init_client_conn_security(conn);
	if (ret < 0)
		goto error_1;

	ret = conn->security->prime_packet_security(conn);
	if (ret < 0)
		goto error_2;

	write_lock(&rxrpc_connection_lock);
	list_add_tail(&conn->proc_link, &rxrpc_connection_proc_list);
	write_unlock(&rxrpc_connection_lock);

	/* We steal the caller's peer ref. */
	cp->peer = NULL;
	rxrpc_get_local(conn->params.local);
	key_get(conn->params.key);

	trace_rxrpc_conn(conn, rxrpc_conn_new_client, atomic_read(&conn->usage),
			 __builtin_return_address(0));
	trace_rxrpc_client(conn, -1, rxrpc_client_alloc);
	_leave(" = %p", conn);
	return conn;

error_2:
	conn->security->clear(conn);
error_1:
	rxrpc_put_client_connection_id(conn);
error_0:
	kfree(conn);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * Determine if a connection may be reused.
 */
static bool rxrpc_may_reuse_conn(struct rxrpc_connection *conn)
{
	int id_cursor, id, distance, limit;

	if (test_bit(RXRPC_CONN_DONT_REUSE, &conn->flags))
		goto dont_reuse;

	if (conn->proto.epoch != rxrpc_epoch)
		goto mark_dont_reuse;

	/* The IDR tree gets very expensive on memory if the connection IDs are
	 * widely scattered throughout the number space, so we shall want to
	 * kill off connections that, say, have an ID more than about four
	 * times the maximum number of client conns away from the current
	 * allocation point to try and keep the IDs concentrated.
	 */
	id_cursor = READ_ONCE(rxrpc_client_conn_ids.cur);
	id = conn->proto.cid >> RXRPC_CIDSHIFT;
	distance = id - id_cursor;
	if (distance < 0)
		distance = -distance;
	limit = round_up(rxrpc_max_client_connections, IDR_SIZE) * 4;
	if (distance > limit)
		goto mark_dont_reuse;

	return true;

mark_dont_reuse:
	set_bit(RXRPC_CONN_DONT_REUSE, &conn->flags);
dont_reuse:
	return false;
}

/*
 * Create or find a client connection to use for a call.
 *
 * If we return with a connection, the call will be on its waiting list.  It's
 * left to the caller to assign a channel and wake up the call.
 */
static int rxrpc_get_client_conn(struct rxrpc_call *call,
				 struct rxrpc_conn_parameters *cp,
				 struct sockaddr_rxrpc *srx,
				 gfp_t gfp)
{
	struct rxrpc_connection *conn, *candidate = NULL;
	struct rxrpc_local *local = cp->local;
	struct rb_node *p, **pp, *parent;
	long diff;
	int ret = -ENOMEM;

	_enter("{%d,%lx},", call->debug_id, call->user_call_ID);

	cp->peer = rxrpc_lookup_peer(cp->local, srx, gfp);
	if (!cp->peer)
		goto error;

	/* If the connection is not meant to be exclusive, search the available
	 * connections to see if the connection we want to use already exists.
	 */
	if (!cp->exclusive) {
		_debug("search 1");
		spin_lock(&local->client_conns_lock);
		p = local->client_conns.rb_node;
		while (p) {
			conn = rb_entry(p, struct rxrpc_connection, client_node);

#define cmp(X) ((long)conn->params.X - (long)cp->X)
			diff = (cmp(peer) ?:
				cmp(key) ?:
				cmp(security_level));
#undef cmp
			if (diff < 0) {
				p = p->rb_left;
			} else if (diff > 0) {
				p = p->rb_right;
			} else {
				if (rxrpc_may_reuse_conn(conn) &&
				    rxrpc_get_connection_maybe(conn))
					goto found_extant_conn;
				/* The connection needs replacing.  It's better
				 * to effect that when we have something to
				 * replace it with so that we don't have to
				 * rebalance the tree twice.
				 */
				break;
			}
		}
		spin_unlock(&local->client_conns_lock);
	}

	/* There wasn't a connection yet or we need an exclusive connection.
	 * We need to create a candidate and then potentially redo the search
	 * in case we're racing with another thread also trying to connect on a
	 * shareable connection.
	 */
	_debug("new conn");
	candidate = rxrpc_alloc_client_connection(cp, gfp);
	if (IS_ERR(candidate)) {
		ret = PTR_ERR(candidate);
		goto error_peer;
	}

	/* Add the call to the new connection's waiting list in case we're
	 * going to have to wait for the connection to come live.  It's our
	 * connection, so we want first dibs on the channel slots.  We would
	 * normally have to take channel_lock but we do this before anyone else
	 * can see the connection.
	 */
	list_add_tail(&call->chan_wait_link, &candidate->waiting_calls);

	if (cp->exclusive) {
		call->conn = candidate;
		call->security_ix = candidate->security_ix;
		_leave(" = 0 [exclusive %d]", candidate->debug_id);
		return 0;
	}

	/* Publish the new connection for userspace to find.  We need to redo
	 * the search before doing this lest we race with someone else adding a
	 * conflicting instance.
	 */
	_debug("search 2");
	spin_lock(&local->client_conns_lock);

	pp = &local->client_conns.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		conn = rb_entry(parent, struct rxrpc_connection, client_node);

#define cmp(X) ((long)conn->params.X - (long)candidate->params.X)
		diff = (cmp(peer) ?:
			cmp(key) ?:
			cmp(security_level));
#undef cmp
		if (diff < 0) {
			pp = &(*pp)->rb_left;
		} else if (diff > 0) {
			pp = &(*pp)->rb_right;
		} else {
			if (rxrpc_may_reuse_conn(conn) &&
			    rxrpc_get_connection_maybe(conn))
				goto found_extant_conn;
			/* The old connection is from an outdated epoch. */
			_debug("replace conn");
			clear_bit(RXRPC_CONN_IN_CLIENT_CONNS, &conn->flags);
			rb_replace_node(&conn->client_node,
					&candidate->client_node,
					&local->client_conns);
			trace_rxrpc_client(conn, -1, rxrpc_client_replace);
			goto candidate_published;
		}
	}

	_debug("new conn");
	rb_link_node(&candidate->client_node, parent, pp);
	rb_insert_color(&candidate->client_node, &local->client_conns);

candidate_published:
	set_bit(RXRPC_CONN_IN_CLIENT_CONNS, &candidate->flags);
	call->conn = candidate;
	call->security_ix = candidate->security_ix;
	spin_unlock(&local->client_conns_lock);
	_leave(" = 0 [new %d]", candidate->debug_id);
	return 0;

	/* We come here if we found a suitable connection already in existence.
	 * Discard any candidate we may have allocated, and try to get a
	 * channel on this one.
	 */
found_extant_conn:
	_debug("found conn");
	spin_unlock(&local->client_conns_lock);

	if (candidate) {
		trace_rxrpc_client(candidate, -1, rxrpc_client_duplicate);
		rxrpc_put_connection(candidate);
		candidate = NULL;
	}

	spin_lock(&conn->channel_lock);
	call->conn = conn;
	call->security_ix = conn->security_ix;
	list_add(&call->chan_wait_link, &conn->waiting_calls);
	spin_unlock(&conn->channel_lock);
	_leave(" = 0 [extant %d]", conn->debug_id);
	return 0;

error_peer:
	rxrpc_put_peer(cp->peer);
	cp->peer = NULL;
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Activate a connection.
 */
static void rxrpc_activate_conn(struct rxrpc_connection *conn)
{
	trace_rxrpc_client(conn, -1, rxrpc_client_to_active);
	conn->cache_state = RXRPC_CONN_CLIENT_ACTIVE;
	rxrpc_nr_active_client_conns++;
	list_move_tail(&conn->cache_link, &rxrpc_active_client_conns);
}

/*
 * Attempt to animate a connection for a new call.
 *
 * If it's not exclusive, the connection is in the endpoint tree, and we're in
 * the conn's list of those waiting to grab a channel.  There is, however, a
 * limit on the number of live connections allowed at any one time, so we may
 * have to wait for capacity to become available.
 *
 * Note that a connection on the waiting queue might *also* have active
 * channels if it has been culled to make space and then re-requested by a new
 * call.
 */
static void rxrpc_animate_client_conn(struct rxrpc_connection *conn)
{
	unsigned int nr_conns;

	_enter("%d,%d", conn->debug_id, conn->cache_state);

	if (conn->cache_state == RXRPC_CONN_CLIENT_ACTIVE)
		goto out;

	spin_lock(&rxrpc_client_conn_cache_lock);

	nr_conns = rxrpc_nr_client_conns;
	if (!test_and_set_bit(RXRPC_CONN_COUNTED, &conn->flags)) {
		trace_rxrpc_client(conn, -1, rxrpc_client_count);
		rxrpc_nr_client_conns = nr_conns + 1;
	}

	switch (conn->cache_state) {
	case RXRPC_CONN_CLIENT_ACTIVE:
	case RXRPC_CONN_CLIENT_WAITING:
		break;

	case RXRPC_CONN_CLIENT_INACTIVE:
	case RXRPC_CONN_CLIENT_CULLED:
	case RXRPC_CONN_CLIENT_IDLE:
		if (nr_conns >= rxrpc_max_client_connections)
			goto wait_for_capacity;
		goto activate_conn;

	default:
		BUG();
	}

out_unlock:
	spin_unlock(&rxrpc_client_conn_cache_lock);
out:
	_leave(" [%d]", conn->cache_state);
	return;

activate_conn:
	_debug("activate");
	rxrpc_activate_conn(conn);
	goto out_unlock;

wait_for_capacity:
	_debug("wait");
	trace_rxrpc_client(conn, -1, rxrpc_client_to_waiting);
	conn->cache_state = RXRPC_CONN_CLIENT_WAITING;
	list_move_tail(&conn->cache_link, &rxrpc_waiting_client_conns);
	goto out_unlock;
}

/*
 * Deactivate a channel.
 */
static void rxrpc_deactivate_one_channel(struct rxrpc_connection *conn,
					 unsigned int channel)
{
	struct rxrpc_channel *chan = &conn->channels[channel];

	rcu_assign_pointer(chan->call, NULL);
	conn->active_chans &= ~(1 << channel);
}

/*
 * Assign a channel to the call at the front of the queue and wake the call up.
 * We don't increment the callNumber counter until this number has been exposed
 * to the world.
 */
static void rxrpc_activate_one_channel(struct rxrpc_connection *conn,
				       unsigned int channel)
{
	struct rxrpc_channel *chan = &conn->channels[channel];
	struct rxrpc_call *call = list_entry(conn->waiting_calls.next,
					     struct rxrpc_call, chan_wait_link);
	u32 call_id = chan->call_counter + 1;

	trace_rxrpc_client(conn, channel, rxrpc_client_chan_activate);

	write_lock_bh(&call->state_lock);
	call->state = RXRPC_CALL_CLIENT_SEND_REQUEST;
	write_unlock_bh(&call->state_lock);

	rxrpc_see_call(call);
	list_del_init(&call->chan_wait_link);
	conn->active_chans |= 1 << channel;
	call->peer	= rxrpc_get_peer(conn->params.peer);
	call->cid	= conn->proto.cid | channel;
	call->call_id	= call_id;

	_net("CONNECT call %08x:%08x as call %d on conn %d",
	     call->cid, call->call_id, call->debug_id, conn->debug_id);

	/* Paired with the read barrier in rxrpc_wait_for_channel().  This
	 * orders cid and epoch in the connection wrt to call_id without the
	 * need to take the channel_lock.
	 *
	 * We provisionally assign a callNumber at this point, but we don't
	 * confirm it until the call is about to be exposed.
	 *
	 * TODO: Pair with a barrier in the data_ready handler when that looks
	 * at the call ID through a connection channel.
	 */
	smp_wmb();
	chan->call_id	= call_id;
	rcu_assign_pointer(chan->call, call);
	wake_up(&call->waitq);
}

/*
 * Assign channels and callNumbers to waiting calls with channel_lock
 * held by caller.
 */
static void rxrpc_activate_channels_locked(struct rxrpc_connection *conn)
{
	u8 avail, mask;

	switch (conn->cache_state) {
	case RXRPC_CONN_CLIENT_ACTIVE:
		mask = RXRPC_ACTIVE_CHANS_MASK;
		break;
	default:
		return;
	}

	while (!list_empty(&conn->waiting_calls) &&
	       (avail = ~conn->active_chans,
		avail &= mask,
		avail != 0))
		rxrpc_activate_one_channel(conn, __ffs(avail));
}

/*
 * Assign channels and callNumbers to waiting calls.
 */
static void rxrpc_activate_channels(struct rxrpc_connection *conn)
{
	_enter("%d", conn->debug_id);

	trace_rxrpc_client(conn, -1, rxrpc_client_activate_chans);

	if (conn->active_chans == RXRPC_ACTIVE_CHANS_MASK)
		return;

	spin_lock(&conn->channel_lock);
	rxrpc_activate_channels_locked(conn);
	spin_unlock(&conn->channel_lock);
	_leave("");
}

/*
 * Wait for a callNumber and a channel to be granted to a call.
 */
static int rxrpc_wait_for_channel(struct rxrpc_call *call, gfp_t gfp)
{
	int ret = 0;

	_enter("%d", call->debug_id);

	if (!call->call_id) {
		DECLARE_WAITQUEUE(myself, current);

		if (!gfpflags_allow_blocking(gfp)) {
			ret = -EAGAIN;
			goto out;
		}

		add_wait_queue_exclusive(&call->waitq, &myself);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (call->call_id)
				break;
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}
			schedule();
		}
		remove_wait_queue(&call->waitq, &myself);
		__set_current_state(TASK_RUNNING);
	}

	/* Paired with the write barrier in rxrpc_activate_one_channel(). */
	smp_rmb();

out:
	_leave(" = %d", ret);
	return ret;
}

/*
 * find a connection for a call
 * - called in process context with IRQs enabled
 */
int rxrpc_connect_call(struct rxrpc_call *call,
		       struct rxrpc_conn_parameters *cp,
		       struct sockaddr_rxrpc *srx,
		       gfp_t gfp)
{
	int ret;

	_enter("{%d,%lx},", call->debug_id, call->user_call_ID);

	rxrpc_discard_expired_client_conns(NULL);
	rxrpc_cull_active_client_conns();

	ret = rxrpc_get_client_conn(call, cp, srx, gfp);
	if (ret < 0)
		return ret;

	rxrpc_animate_client_conn(call->conn);
	rxrpc_activate_channels(call->conn);

	ret = rxrpc_wait_for_channel(call, gfp);
	if (ret < 0)
		rxrpc_disconnect_client_call(call);

	_leave(" = %d", ret);
	return ret;
}

/*
 * Note that a connection is about to be exposed to the world.  Once it is
 * exposed, we maintain an extra ref on it that stops it from being summarily
 * discarded before it's (a) had a chance to deal with retransmission and (b)
 * had a chance at re-use (the per-connection security negotiation is
 * expensive).
 */
static void rxrpc_expose_client_conn(struct rxrpc_connection *conn,
				     unsigned int channel)
{
	if (!test_and_set_bit(RXRPC_CONN_EXPOSED, &conn->flags)) {
		trace_rxrpc_client(conn, channel, rxrpc_client_exposed);
		rxrpc_get_connection(conn);
	}
}

/*
 * Note that a call, and thus a connection, is about to be exposed to the
 * world.
 */
void rxrpc_expose_client_call(struct rxrpc_call *call)
{
	unsigned int channel = call->cid & RXRPC_CHANNELMASK;
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_channel *chan = &conn->channels[channel];

	if (!test_and_set_bit(RXRPC_CALL_EXPOSED, &call->flags)) {
		/* Mark the call ID as being used.  If the callNumber counter
		 * exceeds ~2 billion, we kill the connection after its
		 * outstanding calls have finished so that the counter doesn't
		 * wrap.
		 */
		chan->call_counter++;
		if (chan->call_counter >= INT_MAX)
			set_bit(RXRPC_CONN_DONT_REUSE, &conn->flags);
		rxrpc_expose_client_conn(conn, channel);
	}
}

/*
 * Disconnect a client call.
 */
void rxrpc_disconnect_client_call(struct rxrpc_call *call)
{
	unsigned int channel = call->cid & RXRPC_CHANNELMASK;
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_channel *chan = &conn->channels[channel];

	trace_rxrpc_client(conn, channel, rxrpc_client_chan_disconnect);
	call->conn = NULL;

	spin_lock(&conn->channel_lock);

	/* Calls that have never actually been assigned a channel can simply be
	 * discarded.  If the conn didn't get used either, it will follow
	 * immediately unless someone else grabs it in the meantime.
	 */
	if (!list_empty(&call->chan_wait_link)) {
		_debug("call is waiting");
		ASSERTCMP(call->call_id, ==, 0);
		ASSERT(!test_bit(RXRPC_CALL_EXPOSED, &call->flags));
		list_del_init(&call->chan_wait_link);

		trace_rxrpc_client(conn, channel, rxrpc_client_chan_unstarted);

		/* We must deactivate or idle the connection if it's now
		 * waiting for nothing.
		 */
		spin_lock(&rxrpc_client_conn_cache_lock);
		if (conn->cache_state == RXRPC_CONN_CLIENT_WAITING &&
		    list_empty(&conn->waiting_calls) &&
		    !conn->active_chans)
			goto idle_connection;
		goto out;
	}

	ASSERTCMP(rcu_access_pointer(chan->call), ==, call);

	/* If a client call was exposed to the world, we save the result for
	 * retransmission.
	 *
	 * We use a barrier here so that the call number and abort code can be
	 * read without needing to take a lock.
	 *
	 * TODO: Make the incoming packet handler check this and handle
	 * terminal retransmission without requiring access to the call.
	 */
	if (test_bit(RXRPC_CALL_EXPOSED, &call->flags)) {
		_debug("exposed %u,%u", call->call_id, call->abort_code);
		__rxrpc_disconnect_call(conn, call);
	}

	/* See if we can pass the channel directly to another call. */
	if (conn->cache_state == RXRPC_CONN_CLIENT_ACTIVE &&
	    !list_empty(&conn->waiting_calls)) {
		trace_rxrpc_client(conn, channel, rxrpc_client_chan_pass);
		rxrpc_activate_one_channel(conn, channel);
		goto out_2;
	}

	/* Things are more complex and we need the cache lock.  We might be
	 * able to simply idle the conn or it might now be lurking on the wait
	 * list.  It might even get moved back to the active list whilst we're
	 * waiting for the lock.
	 */
	spin_lock(&rxrpc_client_conn_cache_lock);

	switch (conn->cache_state) {
	case RXRPC_CONN_CLIENT_ACTIVE:
		if (list_empty(&conn->waiting_calls)) {
			rxrpc_deactivate_one_channel(conn, channel);
			if (!conn->active_chans) {
				rxrpc_nr_active_client_conns--;
				goto idle_connection;
			}
			goto out;
		}

		trace_rxrpc_client(conn, channel, rxrpc_client_chan_pass);
		rxrpc_activate_one_channel(conn, channel);
		goto out;

	case RXRPC_CONN_CLIENT_CULLED:
		rxrpc_deactivate_one_channel(conn, channel);
		ASSERT(list_empty(&conn->waiting_calls));
		if (!conn->active_chans)
			goto idle_connection;
		goto out;

	case RXRPC_CONN_CLIENT_WAITING:
		rxrpc_deactivate_one_channel(conn, channel);
		goto out;

	default:
		BUG();
	}

out:
	spin_unlock(&rxrpc_client_conn_cache_lock);
out_2:
	spin_unlock(&conn->channel_lock);
	rxrpc_put_connection(conn);
	_leave("");
	return;

idle_connection:
	/* As no channels remain active, the connection gets deactivated
	 * immediately or moved to the idle list for a short while.
	 */
	if (test_bit(RXRPC_CONN_EXPOSED, &conn->flags)) {
		trace_rxrpc_client(conn, channel, rxrpc_client_to_idle);
		conn->idle_timestamp = jiffies;
		conn->cache_state = RXRPC_CONN_CLIENT_IDLE;
		list_move_tail(&conn->cache_link, &rxrpc_idle_client_conns);
		if (rxrpc_idle_client_conns.next == &conn->cache_link &&
		    !rxrpc_kill_all_client_conns)
			queue_delayed_work(rxrpc_workqueue,
					   &rxrpc_client_conn_reap,
					   rxrpc_conn_idle_client_expiry);
	} else {
		trace_rxrpc_client(conn, channel, rxrpc_client_to_inactive);
		conn->cache_state = RXRPC_CONN_CLIENT_INACTIVE;
		list_del_init(&conn->cache_link);
	}
	goto out;
}

/*
 * Clean up a dead client connection.
 */
static struct rxrpc_connection *
rxrpc_put_one_client_conn(struct rxrpc_connection *conn)
{
	struct rxrpc_connection *next = NULL;
	struct rxrpc_local *local = conn->params.local;
	unsigned int nr_conns;

	trace_rxrpc_client(conn, -1, rxrpc_client_cleanup);

	if (test_bit(RXRPC_CONN_IN_CLIENT_CONNS, &conn->flags)) {
		spin_lock(&local->client_conns_lock);
		if (test_and_clear_bit(RXRPC_CONN_IN_CLIENT_CONNS,
				       &conn->flags))
			rb_erase(&conn->client_node, &local->client_conns);
		spin_unlock(&local->client_conns_lock);
	}

	rxrpc_put_client_connection_id(conn);

	ASSERTCMP(conn->cache_state, ==, RXRPC_CONN_CLIENT_INACTIVE);

	if (test_bit(RXRPC_CONN_COUNTED, &conn->flags)) {
		trace_rxrpc_client(conn, -1, rxrpc_client_uncount);
		spin_lock(&rxrpc_client_conn_cache_lock);
		nr_conns = --rxrpc_nr_client_conns;

		if (nr_conns < rxrpc_max_client_connections &&
		    !list_empty(&rxrpc_waiting_client_conns)) {
			next = list_entry(rxrpc_waiting_client_conns.next,
					  struct rxrpc_connection, cache_link);
			rxrpc_get_connection(next);
			rxrpc_activate_conn(next);
		}

		spin_unlock(&rxrpc_client_conn_cache_lock);
	}

	rxrpc_kill_connection(conn);
	if (next)
		rxrpc_activate_channels(next);

	/* We need to get rid of the temporary ref we took upon next, but we
	 * can't call rxrpc_put_connection() recursively.
	 */
	return next;
}

/*
 * Clean up a dead client connections.
 */
void rxrpc_put_client_conn(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	int n;

	do {
		n = atomic_dec_return(&conn->usage);
		trace_rxrpc_conn(conn, rxrpc_conn_put_client, n, here);
		if (n > 0)
			return;
		ASSERTCMP(n, >=, 0);

		conn = rxrpc_put_one_client_conn(conn);
	} while (conn);
}

/*
 * Kill the longest-active client connections to make room for new ones.
 */
static void rxrpc_cull_active_client_conns(void)
{
	struct rxrpc_connection *conn;
	unsigned int nr_conns = rxrpc_nr_client_conns;
	unsigned int nr_active, limit;

	_enter("");

	ASSERTCMP(nr_conns, >=, 0);
	if (nr_conns < rxrpc_max_client_connections) {
		_leave(" [ok]");
		return;
	}
	limit = rxrpc_reap_client_connections;

	spin_lock(&rxrpc_client_conn_cache_lock);
	nr_active = rxrpc_nr_active_client_conns;

	while (nr_active > limit) {
		ASSERT(!list_empty(&rxrpc_active_client_conns));
		conn = list_entry(rxrpc_active_client_conns.next,
				  struct rxrpc_connection, cache_link);
		ASSERTCMP(conn->cache_state, ==, RXRPC_CONN_CLIENT_ACTIVE);

		if (list_empty(&conn->waiting_calls)) {
			trace_rxrpc_client(conn, -1, rxrpc_client_to_culled);
			conn->cache_state = RXRPC_CONN_CLIENT_CULLED;
			list_del_init(&conn->cache_link);
		} else {
			trace_rxrpc_client(conn, -1, rxrpc_client_to_waiting);
			conn->cache_state = RXRPC_CONN_CLIENT_WAITING;
			list_move_tail(&conn->cache_link,
				       &rxrpc_waiting_client_conns);
		}

		nr_active--;
	}

	rxrpc_nr_active_client_conns = nr_active;
	spin_unlock(&rxrpc_client_conn_cache_lock);
	ASSERTCMP(nr_active, >=, 0);
	_leave(" [culled]");
}

/*
 * Discard expired client connections from the idle list.  Each conn in the
 * idle list has been exposed and holds an extra ref because of that.
 *
 * This may be called from conn setup or from a work item so cannot be
 * considered non-reentrant.
 */
static void rxrpc_discard_expired_client_conns(struct work_struct *work)
{
	struct rxrpc_connection *conn;
	unsigned long expiry, conn_expires_at, now;
	unsigned int nr_conns;
	bool did_discard = false;

	_enter("%c", work ? 'w' : 'n');

	if (list_empty(&rxrpc_idle_client_conns)) {
		_leave(" [empty]");
		return;
	}

	/* Don't double up on the discarding */
	if (!spin_trylock(&rxrpc_client_conn_discard_mutex)) {
		_leave(" [already]");
		return;
	}

	/* We keep an estimate of what the number of conns ought to be after
	 * we've discarded some so that we don't overdo the discarding.
	 */
	nr_conns = rxrpc_nr_client_conns;

next:
	spin_lock(&rxrpc_client_conn_cache_lock);

	if (list_empty(&rxrpc_idle_client_conns))
		goto out;

	conn = list_entry(rxrpc_idle_client_conns.next,
			  struct rxrpc_connection, cache_link);
	ASSERT(test_bit(RXRPC_CONN_EXPOSED, &conn->flags));

	if (!rxrpc_kill_all_client_conns) {
		/* If the number of connections is over the reap limit, we
		 * expedite discard by reducing the expiry timeout.  We must,
		 * however, have at least a short grace period to be able to do
		 * final-ACK or ABORT retransmission.
		 */
		expiry = rxrpc_conn_idle_client_expiry;
		if (nr_conns > rxrpc_reap_client_connections)
			expiry = rxrpc_conn_idle_client_fast_expiry;

		conn_expires_at = conn->idle_timestamp + expiry;

		now = READ_ONCE(jiffies);
		if (time_after(conn_expires_at, now))
			goto not_yet_expired;
	}

	trace_rxrpc_client(conn, -1, rxrpc_client_discard);
	if (!test_and_clear_bit(RXRPC_CONN_EXPOSED, &conn->flags))
		BUG();
	conn->cache_state = RXRPC_CONN_CLIENT_INACTIVE;
	list_del_init(&conn->cache_link);

	spin_unlock(&rxrpc_client_conn_cache_lock);

	/* When we cleared the EXPOSED flag, we took on responsibility for the
	 * reference that that had on the usage count.  We deal with that here.
	 * If someone re-sets the flag and re-gets the ref, that's fine.
	 */
	rxrpc_put_connection(conn);
	did_discard = true;
	nr_conns--;
	goto next;

not_yet_expired:
	/* The connection at the front of the queue hasn't yet expired, so
	 * schedule the work item for that point if we discarded something.
	 *
	 * We don't worry if the work item is already scheduled - it can look
	 * after rescheduling itself at a later time.  We could cancel it, but
	 * then things get messier.
	 */
	_debug("not yet");
	if (!rxrpc_kill_all_client_conns)
		queue_delayed_work(rxrpc_workqueue,
				   &rxrpc_client_conn_reap,
				   conn_expires_at - now);

out:
	spin_unlock(&rxrpc_client_conn_cache_lock);
	spin_unlock(&rxrpc_client_conn_discard_mutex);
	_leave("");
}

/*
 * Preemptively destroy all the client connection records rather than waiting
 * for them to time out
 */
void __exit rxrpc_destroy_all_client_connections(void)
{
	_enter("");

	spin_lock(&rxrpc_client_conn_cache_lock);
	rxrpc_kill_all_client_conns = true;
	spin_unlock(&rxrpc_client_conn_cache_lock);

	cancel_delayed_work(&rxrpc_client_conn_reap);

	if (!queue_delayed_work(rxrpc_workqueue, &rxrpc_client_conn_reap, 0))
		_debug("destroy: queue failed");

	_leave("");
}

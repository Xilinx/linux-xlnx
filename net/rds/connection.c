/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/inet_hashtables.h>

#include "rds.h"
#include "loop.h"

#define RDS_CONNECTION_HASH_BITS 12
#define RDS_CONNECTION_HASH_ENTRIES (1 << RDS_CONNECTION_HASH_BITS)
#define RDS_CONNECTION_HASH_MASK (RDS_CONNECTION_HASH_ENTRIES - 1)

/* converting this to RCU is a chore for another day.. */
static DEFINE_SPINLOCK(rds_conn_lock);
static unsigned long rds_conn_count;
static struct hlist_head rds_conn_hash[RDS_CONNECTION_HASH_ENTRIES];
static struct kmem_cache *rds_conn_slab;

static struct hlist_head *rds_conn_bucket(__be32 laddr, __be32 faddr)
{
	static u32 rds_hash_secret __read_mostly;

	unsigned long hash;

	net_get_random_once(&rds_hash_secret, sizeof(rds_hash_secret));

	/* Pass NULL, don't need struct net for hash */
	hash = __inet_ehashfn(be32_to_cpu(laddr), 0,
			      be32_to_cpu(faddr), 0,
			      rds_hash_secret);
	return &rds_conn_hash[hash & RDS_CONNECTION_HASH_MASK];
}

#define rds_conn_info_set(var, test, suffix) do {		\
	if (test)						\
		var |= RDS_INFO_CONNECTION_FLAG_##suffix;	\
} while (0)

/* rcu read lock must be held or the connection spinlock */
static struct rds_connection *rds_conn_lookup(struct net *net,
					      struct hlist_head *head,
					      __be32 laddr, __be32 faddr,
					      struct rds_transport *trans)
{
	struct rds_connection *conn, *ret = NULL;

	hlist_for_each_entry_rcu(conn, head, c_hash_node) {
		if (conn->c_faddr == faddr && conn->c_laddr == laddr &&
		    conn->c_trans == trans && net == rds_conn_net(conn)) {
			ret = conn;
			break;
		}
	}
	rdsdebug("returning conn %p for %pI4 -> %pI4\n", ret,
		 &laddr, &faddr);
	return ret;
}

/*
 * This is called by transports as they're bringing down a connection.
 * It clears partial message state so that the transport can start sending
 * and receiving over this connection again in the future.  It is up to
 * the transport to have serialized this call with its send and recv.
 */
static void rds_conn_path_reset(struct rds_conn_path *cp)
{
	struct rds_connection *conn = cp->cp_conn;

	rdsdebug("connection %pI4 to %pI4 reset\n",
	  &conn->c_laddr, &conn->c_faddr);

	rds_stats_inc(s_conn_reset);
	rds_send_path_reset(cp);
	cp->cp_flags = 0;

	/* Do not clear next_rx_seq here, else we cannot distinguish
	 * retransmitted packets from new packets, and will hand all
	 * of them to the application. That is not consistent with the
	 * reliability guarantees of RDS. */
}

static void __rds_conn_path_init(struct rds_connection *conn,
				 struct rds_conn_path *cp, bool is_outgoing)
{
	spin_lock_init(&cp->cp_lock);
	cp->cp_next_tx_seq = 1;
	init_waitqueue_head(&cp->cp_waitq);
	INIT_LIST_HEAD(&cp->cp_send_queue);
	INIT_LIST_HEAD(&cp->cp_retrans);

	cp->cp_conn = conn;
	atomic_set(&cp->cp_state, RDS_CONN_DOWN);
	cp->cp_send_gen = 0;
	/* cp_outgoing is per-path. So we can only set it here
	 * for the single-path transports.
	 */
	if (!conn->c_trans->t_mp_capable)
		cp->cp_outgoing = (is_outgoing ? 1 : 0);
	cp->cp_reconnect_jiffies = 0;
	INIT_DELAYED_WORK(&cp->cp_send_w, rds_send_worker);
	INIT_DELAYED_WORK(&cp->cp_recv_w, rds_recv_worker);
	INIT_DELAYED_WORK(&cp->cp_conn_w, rds_connect_worker);
	INIT_WORK(&cp->cp_down_w, rds_shutdown_worker);
	mutex_init(&cp->cp_cm_lock);
	cp->cp_flags = 0;
}

/*
 * There is only every one 'conn' for a given pair of addresses in the
 * system at a time.  They contain messages to be retransmitted and so
 * span the lifetime of the actual underlying transport connections.
 *
 * For now they are not garbage collected once they're created.  They
 * are torn down as the module is removed, if ever.
 */
static struct rds_connection *__rds_conn_create(struct net *net,
						__be32 laddr, __be32 faddr,
				       struct rds_transport *trans, gfp_t gfp,
				       int is_outgoing)
{
	struct rds_connection *conn, *parent = NULL;
	struct hlist_head *head = rds_conn_bucket(laddr, faddr);
	struct rds_transport *loop_trans;
	unsigned long flags;
	int ret, i;

	rcu_read_lock();
	conn = rds_conn_lookup(net, head, laddr, faddr, trans);
	if (conn && conn->c_loopback && conn->c_trans != &rds_loop_transport &&
	    laddr == faddr && !is_outgoing) {
		/* This is a looped back IB connection, and we're
		 * called by the code handling the incoming connect.
		 * We need a second connection object into which we
		 * can stick the other QP. */
		parent = conn;
		conn = parent->c_passive;
	}
	rcu_read_unlock();
	if (conn)
		goto out;

	conn = kmem_cache_zalloc(rds_conn_slab, gfp);
	if (!conn) {
		conn = ERR_PTR(-ENOMEM);
		goto out;
	}

	INIT_HLIST_NODE(&conn->c_hash_node);
	conn->c_laddr = laddr;
	conn->c_faddr = faddr;

	rds_conn_net_set(conn, net);

	ret = rds_cong_get_maps(conn);
	if (ret) {
		kmem_cache_free(rds_conn_slab, conn);
		conn = ERR_PTR(ret);
		goto out;
	}

	/*
	 * This is where a connection becomes loopback.  If *any* RDS sockets
	 * can bind to the destination address then we'd rather the messages
	 * flow through loopback rather than either transport.
	 */
	loop_trans = rds_trans_get_preferred(net, faddr);
	if (loop_trans) {
		rds_trans_put(loop_trans);
		conn->c_loopback = 1;
		if (is_outgoing && trans->t_prefer_loopback) {
			/* "outgoing" connection - and the transport
			 * says it wants the connection handled by the
			 * loopback transport. This is what TCP does.
			 */
			trans = &rds_loop_transport;
		}
	}

	conn->c_trans = trans;

	init_waitqueue_head(&conn->c_hs_waitq);
	for (i = 0; i < RDS_MPATH_WORKERS; i++) {
		__rds_conn_path_init(conn, &conn->c_path[i],
				     is_outgoing);
		conn->c_path[i].cp_index = i;
	}
	ret = trans->conn_alloc(conn, gfp);
	if (ret) {
		kmem_cache_free(rds_conn_slab, conn);
		conn = ERR_PTR(ret);
		goto out;
	}

	rdsdebug("allocated conn %p for %pI4 -> %pI4 over %s %s\n",
	  conn, &laddr, &faddr,
	  trans->t_name ? trans->t_name : "[unknown]",
	  is_outgoing ? "(outgoing)" : "");

	/*
	 * Since we ran without holding the conn lock, someone could
	 * have created the same conn (either normal or passive) in the
	 * interim. We check while holding the lock. If we won, we complete
	 * init and return our conn. If we lost, we rollback and return the
	 * other one.
	 */
	spin_lock_irqsave(&rds_conn_lock, flags);
	if (parent) {
		/* Creating passive conn */
		if (parent->c_passive) {
			trans->conn_free(conn->c_path[0].cp_transport_data);
			kmem_cache_free(rds_conn_slab, conn);
			conn = parent->c_passive;
		} else {
			parent->c_passive = conn;
			rds_cong_add_conn(conn);
			rds_conn_count++;
		}
	} else {
		/* Creating normal conn */
		struct rds_connection *found;

		found = rds_conn_lookup(net, head, laddr, faddr, trans);
		if (found) {
			struct rds_conn_path *cp;
			int i;

			for (i = 0; i < RDS_MPATH_WORKERS; i++) {
				cp = &conn->c_path[i];
				/* The ->conn_alloc invocation may have
				 * allocated resource for all paths, so all
				 * of them may have to be freed here.
				 */
				if (cp->cp_transport_data)
					trans->conn_free(cp->cp_transport_data);
			}
			kmem_cache_free(rds_conn_slab, conn);
			conn = found;
		} else {
			hlist_add_head_rcu(&conn->c_hash_node, head);
			rds_cong_add_conn(conn);
			rds_conn_count++;
		}
	}
	spin_unlock_irqrestore(&rds_conn_lock, flags);

out:
	return conn;
}

struct rds_connection *rds_conn_create(struct net *net,
				       __be32 laddr, __be32 faddr,
				       struct rds_transport *trans, gfp_t gfp)
{
	return __rds_conn_create(net, laddr, faddr, trans, gfp, 0);
}
EXPORT_SYMBOL_GPL(rds_conn_create);

struct rds_connection *rds_conn_create_outgoing(struct net *net,
						__be32 laddr, __be32 faddr,
				       struct rds_transport *trans, gfp_t gfp)
{
	return __rds_conn_create(net, laddr, faddr, trans, gfp, 1);
}
EXPORT_SYMBOL_GPL(rds_conn_create_outgoing);

void rds_conn_shutdown(struct rds_conn_path *cp)
{
	struct rds_connection *conn = cp->cp_conn;

	/* shut it down unless it's down already */
	if (!rds_conn_path_transition(cp, RDS_CONN_DOWN, RDS_CONN_DOWN)) {
		/*
		 * Quiesce the connection mgmt handlers before we start tearing
		 * things down. We don't hold the mutex for the entire
		 * duration of the shutdown operation, else we may be
		 * deadlocking with the CM handler. Instead, the CM event
		 * handler is supposed to check for state DISCONNECTING
		 */
		mutex_lock(&cp->cp_cm_lock);
		if (!rds_conn_path_transition(cp, RDS_CONN_UP,
					      RDS_CONN_DISCONNECTING) &&
		    !rds_conn_path_transition(cp, RDS_CONN_ERROR,
					      RDS_CONN_DISCONNECTING)) {
			rds_conn_path_error(cp,
					    "shutdown called in state %d\n",
					    atomic_read(&cp->cp_state));
			mutex_unlock(&cp->cp_cm_lock);
			return;
		}
		mutex_unlock(&cp->cp_cm_lock);

		wait_event(cp->cp_waitq,
			   !test_bit(RDS_IN_XMIT, &cp->cp_flags));
		wait_event(cp->cp_waitq,
			   !test_bit(RDS_RECV_REFILL, &cp->cp_flags));

		conn->c_trans->conn_path_shutdown(cp);
		rds_conn_path_reset(cp);

		if (!rds_conn_path_transition(cp, RDS_CONN_DISCONNECTING,
					      RDS_CONN_DOWN)) {
			/* This can happen - eg when we're in the middle of tearing
			 * down the connection, and someone unloads the rds module.
			 * Quite reproduceable with loopback connections.
			 * Mostly harmless.
			 */
			rds_conn_path_error(cp, "%s: failed to transition "
					    "to state DOWN, current state "
					    "is %d\n", __func__,
					    atomic_read(&cp->cp_state));
			return;
		}
	}

	/* Then reconnect if it's still live.
	 * The passive side of an IB loopback connection is never added
	 * to the conn hash, so we never trigger a reconnect on this
	 * conn - the reconnect is always triggered by the active peer. */
	cancel_delayed_work_sync(&cp->cp_conn_w);
	rcu_read_lock();
	if (!hlist_unhashed(&conn->c_hash_node)) {
		rcu_read_unlock();
		rds_queue_reconnect(cp);
	} else {
		rcu_read_unlock();
	}
}

/* destroy a single rds_conn_path. rds_conn_destroy() iterates over
 * all paths using rds_conn_path_destroy()
 */
static void rds_conn_path_destroy(struct rds_conn_path *cp)
{
	struct rds_message *rm, *rtmp;

	if (!cp->cp_transport_data)
		return;

	rds_conn_path_drop(cp);
	flush_work(&cp->cp_down_w);

	/* make sure lingering queued work won't try to ref the conn */
	cancel_delayed_work_sync(&cp->cp_send_w);
	cancel_delayed_work_sync(&cp->cp_recv_w);

	/* tear down queued messages */
	list_for_each_entry_safe(rm, rtmp,
				 &cp->cp_send_queue,
				 m_conn_item) {
		list_del_init(&rm->m_conn_item);
		BUG_ON(!list_empty(&rm->m_sock_item));
		rds_message_put(rm);
	}
	if (cp->cp_xmit_rm)
		rds_message_put(cp->cp_xmit_rm);

	cp->cp_conn->c_trans->conn_free(cp->cp_transport_data);
}

/*
 * Stop and free a connection.
 *
 * This can only be used in very limited circumstances.  It assumes that once
 * the conn has been shutdown that no one else is referencing the connection.
 * We can only ensure this in the rmmod path in the current code.
 */
void rds_conn_destroy(struct rds_connection *conn)
{
	unsigned long flags;
	int i;
	struct rds_conn_path *cp;

	rdsdebug("freeing conn %p for %pI4 -> "
		 "%pI4\n", conn, &conn->c_laddr,
		 &conn->c_faddr);

	/* Ensure conn will not be scheduled for reconnect */
	spin_lock_irq(&rds_conn_lock);
	hlist_del_init_rcu(&conn->c_hash_node);
	spin_unlock_irq(&rds_conn_lock);
	synchronize_rcu();

	/* shut the connection down */
	for (i = 0; i < RDS_MPATH_WORKERS; i++) {
		cp = &conn->c_path[i];
		rds_conn_path_destroy(cp);
		BUG_ON(!list_empty(&cp->cp_retrans));
	}

	/*
	 * The congestion maps aren't freed up here.  They're
	 * freed by rds_cong_exit() after all the connections
	 * have been freed.
	 */
	rds_cong_remove_conn(conn);

	kmem_cache_free(rds_conn_slab, conn);

	spin_lock_irqsave(&rds_conn_lock, flags);
	rds_conn_count--;
	spin_unlock_irqrestore(&rds_conn_lock, flags);
}
EXPORT_SYMBOL_GPL(rds_conn_destroy);

static void rds_conn_message_info(struct socket *sock, unsigned int len,
				  struct rds_info_iterator *iter,
				  struct rds_info_lengths *lens,
				  int want_send)
{
	struct hlist_head *head;
	struct list_head *list;
	struct rds_connection *conn;
	struct rds_message *rm;
	unsigned int total = 0;
	unsigned long flags;
	size_t i;
	int j;

	len /= sizeof(struct rds_info_message);

	rcu_read_lock();

	for (i = 0, head = rds_conn_hash; i < ARRAY_SIZE(rds_conn_hash);
	     i++, head++) {
		hlist_for_each_entry_rcu(conn, head, c_hash_node) {
			struct rds_conn_path *cp;

			for (j = 0; j < RDS_MPATH_WORKERS; j++) {
				cp = &conn->c_path[j];
				if (want_send)
					list = &cp->cp_send_queue;
				else
					list = &cp->cp_retrans;

				spin_lock_irqsave(&cp->cp_lock, flags);

				/* XXX too lazy to maintain counts.. */
				list_for_each_entry(rm, list, m_conn_item) {
					total++;
					if (total <= len)
						rds_inc_info_copy(&rm->m_inc,
								  iter,
								  conn->c_laddr,
								  conn->c_faddr,
								  0);
				}

				spin_unlock_irqrestore(&cp->cp_lock, flags);
				if (!conn->c_trans->t_mp_capable)
					break;
			}
		}
	}
	rcu_read_unlock();

	lens->nr = total;
	lens->each = sizeof(struct rds_info_message);
}

static void rds_conn_message_info_send(struct socket *sock, unsigned int len,
				       struct rds_info_iterator *iter,
				       struct rds_info_lengths *lens)
{
	rds_conn_message_info(sock, len, iter, lens, 1);
}

static void rds_conn_message_info_retrans(struct socket *sock,
					  unsigned int len,
					  struct rds_info_iterator *iter,
					  struct rds_info_lengths *lens)
{
	rds_conn_message_info(sock, len, iter, lens, 0);
}

void rds_for_each_conn_info(struct socket *sock, unsigned int len,
			  struct rds_info_iterator *iter,
			  struct rds_info_lengths *lens,
			  int (*visitor)(struct rds_connection *, void *),
			  size_t item_len)
{
	uint64_t buffer[(item_len + 7) / 8];
	struct hlist_head *head;
	struct rds_connection *conn;
	size_t i;

	rcu_read_lock();

	lens->nr = 0;
	lens->each = item_len;

	for (i = 0, head = rds_conn_hash; i < ARRAY_SIZE(rds_conn_hash);
	     i++, head++) {
		hlist_for_each_entry_rcu(conn, head, c_hash_node) {

			/* XXX no c_lock usage.. */
			if (!visitor(conn, buffer))
				continue;

			/* We copy as much as we can fit in the buffer,
			 * but we count all items so that the caller
			 * can resize the buffer. */
			if (len >= item_len) {
				rds_info_copy(iter, buffer, item_len);
				len -= item_len;
			}
			lens->nr++;
		}
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(rds_for_each_conn_info);

void rds_walk_conn_path_info(struct socket *sock, unsigned int len,
			     struct rds_info_iterator *iter,
			     struct rds_info_lengths *lens,
			     int (*visitor)(struct rds_conn_path *, void *),
			     size_t item_len)
{
	u64  buffer[(item_len + 7) / 8];
	struct hlist_head *head;
	struct rds_connection *conn;
	size_t i;
	int j;

	rcu_read_lock();

	lens->nr = 0;
	lens->each = item_len;

	for (i = 0, head = rds_conn_hash; i < ARRAY_SIZE(rds_conn_hash);
	     i++, head++) {
		hlist_for_each_entry_rcu(conn, head, c_hash_node) {
			struct rds_conn_path *cp;

			for (j = 0; j < RDS_MPATH_WORKERS; j++) {
				cp = &conn->c_path[j];

				/* XXX no cp_lock usage.. */
				if (!visitor(cp, buffer))
					continue;
				if (!conn->c_trans->t_mp_capable)
					break;
			}

			/* We copy as much as we can fit in the buffer,
			 * but we count all items so that the caller
			 * can resize the buffer.
			 */
			if (len >= item_len) {
				rds_info_copy(iter, buffer, item_len);
				len -= item_len;
			}
			lens->nr++;
		}
	}
	rcu_read_unlock();
}

static int rds_conn_info_visitor(struct rds_conn_path *cp, void *buffer)
{
	struct rds_info_connection *cinfo = buffer;

	cinfo->next_tx_seq = cp->cp_next_tx_seq;
	cinfo->next_rx_seq = cp->cp_next_rx_seq;
	cinfo->laddr = cp->cp_conn->c_laddr;
	cinfo->faddr = cp->cp_conn->c_faddr;
	strncpy(cinfo->transport, cp->cp_conn->c_trans->t_name,
		sizeof(cinfo->transport));
	cinfo->flags = 0;

	rds_conn_info_set(cinfo->flags, test_bit(RDS_IN_XMIT, &cp->cp_flags),
			  SENDING);
	/* XXX Future: return the state rather than these funky bits */
	rds_conn_info_set(cinfo->flags,
			  atomic_read(&cp->cp_state) == RDS_CONN_CONNECTING,
			  CONNECTING);
	rds_conn_info_set(cinfo->flags,
			  atomic_read(&cp->cp_state) == RDS_CONN_UP,
			  CONNECTED);
	return 1;
}

static void rds_conn_info(struct socket *sock, unsigned int len,
			  struct rds_info_iterator *iter,
			  struct rds_info_lengths *lens)
{
	rds_walk_conn_path_info(sock, len, iter, lens,
				rds_conn_info_visitor,
				sizeof(struct rds_info_connection));
}

int rds_conn_init(void)
{
	rds_conn_slab = kmem_cache_create("rds_connection",
					  sizeof(struct rds_connection),
					  0, 0, NULL);
	if (!rds_conn_slab)
		return -ENOMEM;

	rds_info_register_func(RDS_INFO_CONNECTIONS, rds_conn_info);
	rds_info_register_func(RDS_INFO_SEND_MESSAGES,
			       rds_conn_message_info_send);
	rds_info_register_func(RDS_INFO_RETRANS_MESSAGES,
			       rds_conn_message_info_retrans);

	return 0;
}

void rds_conn_exit(void)
{
	rds_loop_exit();

	WARN_ON(!hlist_empty(rds_conn_hash));

	kmem_cache_destroy(rds_conn_slab);

	rds_info_deregister_func(RDS_INFO_CONNECTIONS, rds_conn_info);
	rds_info_deregister_func(RDS_INFO_SEND_MESSAGES,
				 rds_conn_message_info_send);
	rds_info_deregister_func(RDS_INFO_RETRANS_MESSAGES,
				 rds_conn_message_info_retrans);
}

/*
 * Force a disconnect
 */
void rds_conn_path_drop(struct rds_conn_path *cp)
{
	atomic_set(&cp->cp_state, RDS_CONN_ERROR);
	queue_work(rds_wq, &cp->cp_down_w);
}
EXPORT_SYMBOL_GPL(rds_conn_path_drop);

void rds_conn_drop(struct rds_connection *conn)
{
	WARN_ON(conn->c_trans->t_mp_capable);
	rds_conn_path_drop(&conn->c_path[0]);
}
EXPORT_SYMBOL_GPL(rds_conn_drop);

/*
 * If the connection is down, trigger a connect. We may have scheduled a
 * delayed reconnect however - in this case we should not interfere.
 */
void rds_conn_path_connect_if_down(struct rds_conn_path *cp)
{
	if (rds_conn_path_state(cp) == RDS_CONN_DOWN &&
	    !test_and_set_bit(RDS_RECONNECT_PENDING, &cp->cp_flags))
		queue_delayed_work(rds_wq, &cp->cp_conn_w, 0);
}

void rds_conn_connect_if_down(struct rds_connection *conn)
{
	WARN_ON(conn->c_trans->t_mp_capable);
	rds_conn_path_connect_if_down(&conn->c_path[0]);
}
EXPORT_SYMBOL_GPL(rds_conn_connect_if_down);

/*
 * An error occurred on the connection
 */
void
__rds_conn_error(struct rds_connection *conn, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);

	rds_conn_drop(conn);
}

void
__rds_conn_path_error(struct rds_conn_path *cp, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);

	rds_conn_path_drop(cp);
}

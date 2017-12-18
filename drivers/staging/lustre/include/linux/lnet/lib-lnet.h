/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Seagate, Inc.
 *
 * lnet/include/lnet/lib-lnet.h
 */

#ifndef __LNET_LIB_LNET_H__
#define __LNET_LIB_LNET_H__

#include "../libcfs/libcfs.h"
#include "api.h"
#include "lnet.h"
#include "lib-types.h"
#include "lib-dlc.h"

extern lnet_t	the_lnet;	/* THE network */

#if (BITS_PER_LONG == 32)
/* 2 CPTs, allowing more CPTs might make us under memory pressure */
#define LNET_CPT_MAX_BITS	1

#else /* 64-bit system */
/*
 * 256 CPTs for thousands of CPUs, allowing more CPTs might make us
 * under risk of consuming all lh_cookie.
 */
#define LNET_CPT_MAX_BITS	8
#endif /* BITS_PER_LONG == 32 */

/* max allowed CPT number */
#define LNET_CPT_MAX		(1 << LNET_CPT_MAX_BITS)

#define LNET_CPT_NUMBER		(the_lnet.ln_cpt_number)
#define LNET_CPT_BITS		(the_lnet.ln_cpt_bits)
#define LNET_CPT_MASK		((1ULL << LNET_CPT_BITS) - 1)

/** exclusive lock */
#define LNET_LOCK_EX		CFS_PERCPT_LOCK_EX

static inline int lnet_is_route_alive(lnet_route_t *route)
{
	/* gateway is down */
	if (!route->lr_gateway->lp_alive)
		return 0;
	/* no NI status, assume it's alive */
	if ((route->lr_gateway->lp_ping_feats &
	     LNET_PING_FEAT_NI_STATUS) == 0)
		return 1;
	/* has NI status, check # down NIs */
	return route->lr_downis == 0;
}

static inline int lnet_is_wire_handle_none(lnet_handle_wire_t *wh)
{
	return (wh->wh_interface_cookie == LNET_WIRE_HANDLE_COOKIE_NONE &&
		wh->wh_object_cookie == LNET_WIRE_HANDLE_COOKIE_NONE);
}

static inline int lnet_md_exhausted(lnet_libmd_t *md)
{
	return (!md->md_threshold ||
		((md->md_options & LNET_MD_MAX_SIZE) &&
		 md->md_offset + md->md_max_size > md->md_length));
}

static inline int lnet_md_unlinkable(lnet_libmd_t *md)
{
	/*
	 * Should unlink md when its refcount is 0 and either:
	 *  - md has been flagged for deletion (by auto unlink or
	 *    LNetM[DE]Unlink, in the latter case md may not be exhausted).
	 *  - auto unlink is on and md is exhausted.
	 */
	if (md->md_refcount)
		return 0;

	if (md->md_flags & LNET_MD_FLAG_ZOMBIE)
		return 1;

	return ((md->md_flags & LNET_MD_FLAG_AUTO_UNLINK) &&
		lnet_md_exhausted(md));
}

#define lnet_cpt_table()	(the_lnet.ln_cpt_table)
#define lnet_cpt_current()	cfs_cpt_current(the_lnet.ln_cpt_table, 1)

static inline int
lnet_cpt_of_cookie(__u64 cookie)
{
	unsigned int cpt = (cookie >> LNET_COOKIE_TYPE_BITS) & LNET_CPT_MASK;

	/*
	 * LNET_CPT_NUMBER doesn't have to be power2, which means we can
	 * get illegal cpt from it's invalid cookie
	 */
	return cpt < LNET_CPT_NUMBER ? cpt : cpt % LNET_CPT_NUMBER;
}

static inline void
lnet_res_lock(int cpt)
{
	cfs_percpt_lock(the_lnet.ln_res_lock, cpt);
}

static inline void
lnet_res_unlock(int cpt)
{
	cfs_percpt_unlock(the_lnet.ln_res_lock, cpt);
}

static inline int
lnet_res_lock_current(void)
{
	int cpt = lnet_cpt_current();

	lnet_res_lock(cpt);
	return cpt;
}

static inline void
lnet_net_lock(int cpt)
{
	cfs_percpt_lock(the_lnet.ln_net_lock, cpt);
}

static inline void
lnet_net_unlock(int cpt)
{
	cfs_percpt_unlock(the_lnet.ln_net_lock, cpt);
}

static inline int
lnet_net_lock_current(void)
{
	int cpt = lnet_cpt_current();

	lnet_net_lock(cpt);
	return cpt;
}

#define LNET_LOCK()		lnet_net_lock(LNET_LOCK_EX)
#define LNET_UNLOCK()		lnet_net_unlock(LNET_LOCK_EX)

#define lnet_ptl_lock(ptl)	spin_lock(&(ptl)->ptl_lock)
#define lnet_ptl_unlock(ptl)	spin_unlock(&(ptl)->ptl_lock)
#define lnet_eq_wait_lock()	spin_lock(&the_lnet.ln_eq_wait_lock)
#define lnet_eq_wait_unlock()	spin_unlock(&the_lnet.ln_eq_wait_lock)
#define lnet_ni_lock(ni)	spin_lock(&(ni)->ni_lock)
#define lnet_ni_unlock(ni)	spin_unlock(&(ni)->ni_lock)

#define MAX_PORTALS		64

static inline lnet_eq_t *
lnet_eq_alloc(void)
{
	lnet_eq_t *eq;

	LIBCFS_ALLOC(eq, sizeof(*eq));
	return eq;
}

static inline void
lnet_eq_free(lnet_eq_t *eq)
{
	LIBCFS_FREE(eq, sizeof(*eq));
}

static inline lnet_libmd_t *
lnet_md_alloc(lnet_md_t *umd)
{
	lnet_libmd_t *md;
	unsigned int size;
	unsigned int niov;

	if (umd->options & LNET_MD_KIOV) {
		niov = umd->length;
		size = offsetof(lnet_libmd_t, md_iov.kiov[niov]);
	} else {
		niov = umd->options & LNET_MD_IOVEC ? umd->length : 1;
		size = offsetof(lnet_libmd_t, md_iov.iov[niov]);
	}

	LIBCFS_ALLOC(md, size);

	if (md) {
		/* Set here in case of early free */
		md->md_options = umd->options;
		md->md_niov = niov;
		INIT_LIST_HEAD(&md->md_list);
	}

	return md;
}

static inline void
lnet_md_free(lnet_libmd_t *md)
{
	unsigned int size;

	if (md->md_options & LNET_MD_KIOV)
		size = offsetof(lnet_libmd_t, md_iov.kiov[md->md_niov]);
	else
		size = offsetof(lnet_libmd_t, md_iov.iov[md->md_niov]);

	LIBCFS_FREE(md, size);
}

static inline lnet_me_t *
lnet_me_alloc(void)
{
	lnet_me_t *me;

	LIBCFS_ALLOC(me, sizeof(*me));
	return me;
}

static inline void
lnet_me_free(lnet_me_t *me)
{
	LIBCFS_FREE(me, sizeof(*me));
}

static inline lnet_msg_t *
lnet_msg_alloc(void)
{
	lnet_msg_t *msg;

	LIBCFS_ALLOC(msg, sizeof(*msg));

	/* no need to zero, LIBCFS_ALLOC does for us */
	return msg;
}

static inline void
lnet_msg_free(lnet_msg_t *msg)
{
	LASSERT(!msg->msg_onactivelist);
	LIBCFS_FREE(msg, sizeof(*msg));
}

lnet_libhandle_t *lnet_res_lh_lookup(struct lnet_res_container *rec,
				     __u64 cookie);
void lnet_res_lh_initialize(struct lnet_res_container *rec,
			    lnet_libhandle_t *lh);
static inline void
lnet_res_lh_invalidate(lnet_libhandle_t *lh)
{
	/* NB: cookie is still useful, don't reset it */
	list_del(&lh->lh_hash_chain);
}

static inline void
lnet_eq2handle(lnet_handle_eq_t *handle, lnet_eq_t *eq)
{
	if (!eq) {
		LNetInvalidateHandle(handle);
		return;
	}

	handle->cookie = eq->eq_lh.lh_cookie;
}

static inline lnet_eq_t *
lnet_handle2eq(lnet_handle_eq_t *handle)
{
	lnet_libhandle_t *lh;

	lh = lnet_res_lh_lookup(&the_lnet.ln_eq_container, handle->cookie);
	if (!lh)
		return NULL;

	return lh_entry(lh, lnet_eq_t, eq_lh);
}

static inline void
lnet_md2handle(lnet_handle_md_t *handle, lnet_libmd_t *md)
{
	handle->cookie = md->md_lh.lh_cookie;
}

static inline lnet_libmd_t *
lnet_handle2md(lnet_handle_md_t *handle)
{
	/* ALWAYS called with resource lock held */
	lnet_libhandle_t *lh;
	int cpt;

	cpt = lnet_cpt_of_cookie(handle->cookie);
	lh = lnet_res_lh_lookup(the_lnet.ln_md_containers[cpt],
				handle->cookie);
	if (!lh)
		return NULL;

	return lh_entry(lh, lnet_libmd_t, md_lh);
}

static inline lnet_libmd_t *
lnet_wire_handle2md(lnet_handle_wire_t *wh)
{
	/* ALWAYS called with resource lock held */
	lnet_libhandle_t *lh;
	int cpt;

	if (wh->wh_interface_cookie != the_lnet.ln_interface_cookie)
		return NULL;

	cpt = lnet_cpt_of_cookie(wh->wh_object_cookie);
	lh = lnet_res_lh_lookup(the_lnet.ln_md_containers[cpt],
				wh->wh_object_cookie);
	if (!lh)
		return NULL;

	return lh_entry(lh, lnet_libmd_t, md_lh);
}

static inline void
lnet_me2handle(lnet_handle_me_t *handle, lnet_me_t *me)
{
	handle->cookie = me->me_lh.lh_cookie;
}

static inline lnet_me_t *
lnet_handle2me(lnet_handle_me_t *handle)
{
	/* ALWAYS called with resource lock held */
	lnet_libhandle_t *lh;
	int cpt;

	cpt = lnet_cpt_of_cookie(handle->cookie);
	lh = lnet_res_lh_lookup(the_lnet.ln_me_containers[cpt],
				handle->cookie);
	if (!lh)
		return NULL;

	return lh_entry(lh, lnet_me_t, me_lh);
}

static inline void
lnet_peer_addref_locked(lnet_peer_t *lp)
{
	LASSERT(lp->lp_refcount > 0);
	lp->lp_refcount++;
}

void lnet_destroy_peer_locked(lnet_peer_t *lp);

static inline void
lnet_peer_decref_locked(lnet_peer_t *lp)
{
	LASSERT(lp->lp_refcount > 0);
	lp->lp_refcount--;
	if (!lp->lp_refcount)
		lnet_destroy_peer_locked(lp);
}

static inline int
lnet_isrouter(lnet_peer_t *lp)
{
	return lp->lp_rtr_refcount ? 1 : 0;
}

static inline void
lnet_ni_addref_locked(lnet_ni_t *ni, int cpt)
{
	LASSERT(cpt >= 0 && cpt < LNET_CPT_NUMBER);
	LASSERT(*ni->ni_refs[cpt] >= 0);

	(*ni->ni_refs[cpt])++;
}

static inline void
lnet_ni_addref(lnet_ni_t *ni)
{
	lnet_net_lock(0);
	lnet_ni_addref_locked(ni, 0);
	lnet_net_unlock(0);
}

static inline void
lnet_ni_decref_locked(lnet_ni_t *ni, int cpt)
{
	LASSERT(cpt >= 0 && cpt < LNET_CPT_NUMBER);
	LASSERT(*ni->ni_refs[cpt] > 0);

	(*ni->ni_refs[cpt])--;
}

static inline void
lnet_ni_decref(lnet_ni_t *ni)
{
	lnet_net_lock(0);
	lnet_ni_decref_locked(ni, 0);
	lnet_net_unlock(0);
}

void lnet_ni_free(lnet_ni_t *ni);
lnet_ni_t *
lnet_ni_alloc(__u32 net, struct cfs_expr_list *el, struct list_head *nilist);

static inline int
lnet_nid2peerhash(lnet_nid_t nid)
{
	return hash_long(nid, LNET_PEER_HASH_BITS);
}

static inline struct list_head *
lnet_net2rnethash(__u32 net)
{
	return &the_lnet.ln_remote_nets_hash[(LNET_NETNUM(net) +
		LNET_NETTYP(net)) &
		((1U << the_lnet.ln_remote_nets_hbits) - 1)];
}

extern lnd_t the_lolnd;
extern int avoid_asym_router_failure;

int lnet_cpt_of_nid_locked(lnet_nid_t nid);
int lnet_cpt_of_nid(lnet_nid_t nid);
lnet_ni_t *lnet_nid2ni_locked(lnet_nid_t nid, int cpt);
lnet_ni_t *lnet_net2ni_locked(__u32 net, int cpt);
lnet_ni_t *lnet_net2ni(__u32 net);

extern int portal_rotor;

int lnet_lib_init(void);
void lnet_lib_exit(void);

int lnet_notify(lnet_ni_t *ni, lnet_nid_t peer, int alive, unsigned long when);
void lnet_notify_locked(lnet_peer_t *lp, int notifylnd, int alive,
			unsigned long when);
int lnet_add_route(__u32 net, __u32 hops, lnet_nid_t gateway_nid,
		   unsigned int priority);
int lnet_check_routes(void);
int lnet_del_route(__u32 net, lnet_nid_t gw_nid);
void lnet_destroy_routes(void);
int lnet_get_route(int idx, __u32 *net, __u32 *hops,
		   lnet_nid_t *gateway, __u32 *alive, __u32 *priority);
int lnet_get_rtr_pool_cfg(int idx, struct lnet_ioctl_pool_cfg *pool_cfg);

void lnet_router_debugfs_init(void);
void lnet_router_debugfs_fini(void);
int  lnet_rtrpools_alloc(int im_a_router);
void lnet_destroy_rtrbuf(lnet_rtrbuf_t *rb, int npages);
int lnet_rtrpools_adjust(int tiny, int small, int large);
int lnet_rtrpools_enable(void);
void lnet_rtrpools_disable(void);
void lnet_rtrpools_free(int keep_pools);
lnet_remotenet_t *lnet_find_net_locked(__u32 net);
int lnet_dyn_add_ni(lnet_pid_t requested_pid,
		    struct lnet_ioctl_config_data *conf);
int lnet_dyn_del_ni(__u32 net);
int lnet_clear_lazy_portal(struct lnet_ni *ni, int portal, char *reason);

int lnet_islocalnid(lnet_nid_t nid);
int lnet_islocalnet(__u32 net);

void lnet_msg_attach_md(lnet_msg_t *msg, lnet_libmd_t *md,
			unsigned int offset, unsigned int mlen);
void lnet_msg_detach_md(lnet_msg_t *msg, int status);
void lnet_build_unlink_event(lnet_libmd_t *md, lnet_event_t *ev);
void lnet_build_msg_event(lnet_msg_t *msg, lnet_event_kind_t ev_type);
void lnet_msg_commit(lnet_msg_t *msg, int cpt);
void lnet_msg_decommit(lnet_msg_t *msg, int cpt, int status);

void lnet_eq_enqueue_event(lnet_eq_t *eq, lnet_event_t *ev);
void lnet_prep_send(lnet_msg_t *msg, int type, lnet_process_id_t target,
		    unsigned int offset, unsigned int len);
int lnet_send(lnet_nid_t nid, lnet_msg_t *msg, lnet_nid_t rtr_nid);
void lnet_return_tx_credits_locked(lnet_msg_t *msg);
void lnet_return_rx_credits_locked(lnet_msg_t *msg);
void lnet_schedule_blocked_locked(lnet_rtrbufpool_t *rbp);
void lnet_drop_routed_msgs_locked(struct list_head *list, int cpt);

/* portals functions */
/* portals attributes */
static inline int
lnet_ptl_is_lazy(lnet_portal_t *ptl)
{
	return !!(ptl->ptl_options & LNET_PTL_LAZY);
}

static inline int
lnet_ptl_is_unique(lnet_portal_t *ptl)
{
	return !!(ptl->ptl_options & LNET_PTL_MATCH_UNIQUE);
}

static inline int
lnet_ptl_is_wildcard(lnet_portal_t *ptl)
{
	return !!(ptl->ptl_options & LNET_PTL_MATCH_WILDCARD);
}

static inline void
lnet_ptl_setopt(lnet_portal_t *ptl, int opt)
{
	ptl->ptl_options |= opt;
}

static inline void
lnet_ptl_unsetopt(lnet_portal_t *ptl, int opt)
{
	ptl->ptl_options &= ~opt;
}

/* match-table functions */
struct list_head *lnet_mt_match_head(struct lnet_match_table *mtable,
				     lnet_process_id_t id, __u64 mbits);
struct lnet_match_table *lnet_mt_of_attach(unsigned int index,
					   lnet_process_id_t id, __u64 mbits,
					   __u64 ignore_bits,
					   lnet_ins_pos_t pos);
int lnet_mt_match_md(struct lnet_match_table *mtable,
		     struct lnet_match_info *info, struct lnet_msg *msg);

/* portals match/attach functions */
void lnet_ptl_attach_md(lnet_me_t *me, lnet_libmd_t *md,
			struct list_head *matches, struct list_head *drops);
void lnet_ptl_detach_md(lnet_me_t *me, lnet_libmd_t *md);
int lnet_ptl_match_md(struct lnet_match_info *info, struct lnet_msg *msg);

/* initialized and finalize portals */
int lnet_portals_create(void);
void lnet_portals_destroy(void);

/* message functions */
int lnet_parse(lnet_ni_t *ni, lnet_hdr_t *hdr,
	       lnet_nid_t fromnid, void *private, int rdma_req);
int lnet_parse_local(lnet_ni_t *ni, lnet_msg_t *msg);
int lnet_parse_forward_locked(lnet_ni_t *ni, lnet_msg_t *msg);

void lnet_recv(lnet_ni_t *ni, void *private, lnet_msg_t *msg, int delayed,
	       unsigned int offset, unsigned int mlen, unsigned int rlen);
void lnet_ni_recv(lnet_ni_t *ni, void *private, lnet_msg_t *msg,
		  int delayed, unsigned int offset,
		  unsigned int mlen, unsigned int rlen);

lnet_msg_t *lnet_create_reply_msg(lnet_ni_t *ni, lnet_msg_t *get_msg);
void lnet_set_reply_msg_len(lnet_ni_t *ni, lnet_msg_t *msg, unsigned int len);

void lnet_finalize(lnet_ni_t *ni, lnet_msg_t *msg, int rc);

void lnet_drop_message(lnet_ni_t *ni, int cpt, void *private,
		       unsigned int nob);
void lnet_drop_delayed_msg_list(struct list_head *head, char *reason);
void lnet_recv_delayed_msg_list(struct list_head *head);

int lnet_msg_container_setup(struct lnet_msg_container *container, int cpt);
void lnet_msg_container_cleanup(struct lnet_msg_container *container);
void lnet_msg_containers_destroy(void);
int lnet_msg_containers_create(void);

char *lnet_msgtyp2str(int type);
void lnet_print_hdr(lnet_hdr_t *hdr);
int lnet_fail_nid(lnet_nid_t nid, unsigned int threshold);

/** \addtogroup lnet_fault_simulation @{ */

int lnet_fault_ctl(int cmd, struct libcfs_ioctl_data *data);
int lnet_fault_init(void);
void lnet_fault_fini(void);

bool lnet_drop_rule_match(lnet_hdr_t *hdr);

int lnet_delay_rule_add(struct lnet_fault_attr *attr);
int lnet_delay_rule_del(lnet_nid_t src, lnet_nid_t dst, bool shutdown);
int lnet_delay_rule_list(int pos, struct lnet_fault_attr *attr,
			 struct lnet_fault_stat *stat);
void lnet_delay_rule_reset(void);
void lnet_delay_rule_check(void);
bool lnet_delay_rule_match_locked(lnet_hdr_t *hdr, struct lnet_msg *msg);

/** @} lnet_fault_simulation */

void lnet_counters_get(lnet_counters_t *counters);
void lnet_counters_reset(void);

unsigned int lnet_iov_nob(unsigned int niov, struct kvec *iov);
int lnet_extract_iov(int dst_niov, struct kvec *dst,
		     int src_niov, const struct kvec *src,
		      unsigned int offset, unsigned int len);

unsigned int lnet_kiov_nob(unsigned int niov, lnet_kiov_t *iov);
int lnet_extract_kiov(int dst_niov, lnet_kiov_t *dst,
		      int src_niov, const lnet_kiov_t *src,
		      unsigned int offset, unsigned int len);

void lnet_copy_iov2iter(struct iov_iter *to,
			unsigned int nsiov, const struct kvec *siov,
			unsigned int soffset, unsigned int nob);
void lnet_copy_kiov2iter(struct iov_iter *to,
			 unsigned int nkiov, const lnet_kiov_t *kiov,
			 unsigned int kiovoffset, unsigned int nob);

void lnet_me_unlink(lnet_me_t *me);

void lnet_md_unlink(lnet_libmd_t *md);
void lnet_md_deconstruct(lnet_libmd_t *lmd, lnet_md_t *umd);

void lnet_register_lnd(lnd_t *lnd);
void lnet_unregister_lnd(lnd_t *lnd);

int lnet_connect(struct socket **sockp, lnet_nid_t peer_nid,
		 __u32 local_ip, __u32 peer_ip, int peer_port);
void lnet_connect_console_error(int rc, lnet_nid_t peer_nid,
				__u32 peer_ip, int port);
int lnet_count_acceptor_nis(void);
int lnet_acceptor_timeout(void);
int lnet_acceptor_port(void);

int lnet_count_acceptor_nis(void);
int lnet_acceptor_port(void);

int lnet_acceptor_start(void);
void lnet_acceptor_stop(void);

int lnet_ipif_query(char *name, int *up, __u32 *ip, __u32 *mask);
int lnet_ipif_enumerate(char ***names);
void lnet_ipif_free_enumeration(char **names, int n);
int lnet_sock_setbuf(struct socket *socket, int txbufsize, int rxbufsize);
int lnet_sock_getbuf(struct socket *socket, int *txbufsize, int *rxbufsize);
int lnet_sock_getaddr(struct socket *socket, bool remote, __u32 *ip, int *port);
int lnet_sock_write(struct socket *sock, void *buffer, int nob, int timeout);
int lnet_sock_read(struct socket *sock, void *buffer, int nob, int timeout);

int lnet_sock_listen(struct socket **sockp, __u32 ip, int port, int backlog);
int lnet_sock_accept(struct socket **newsockp, struct socket *sock);
int lnet_sock_connect(struct socket **sockp, int *fatal,
		      __u32 local_ip, int local_port,
		      __u32 peer_ip, int peer_port);
void libcfs_sock_release(struct socket *sock);

int lnet_peers_start_down(void);
int lnet_peer_buffer_credits(lnet_ni_t *ni);

int lnet_router_checker_start(void);
void lnet_router_checker_stop(void);
void lnet_router_ni_update_locked(lnet_peer_t *gw, __u32 net);
void lnet_swap_pinginfo(lnet_ping_info_t *info);

int lnet_parse_ip2nets(char **networksp, char *ip2nets);
int lnet_parse_routes(char *route_str, int *im_a_router);
int lnet_parse_networks(struct list_head *nilist, char *networks);
int lnet_net_unique(__u32 net, struct list_head *nilist);

int lnet_nid2peer_locked(lnet_peer_t **lpp, lnet_nid_t nid, int cpt);
lnet_peer_t *lnet_find_peer_locked(struct lnet_peer_table *ptable,
				   lnet_nid_t nid);
void lnet_peer_tables_cleanup(lnet_ni_t *ni);
void lnet_peer_tables_destroy(void);
int lnet_peer_tables_create(void);
void lnet_debug_peer(lnet_nid_t nid);
int lnet_get_peer_info(__u32 peer_index, __u64 *nid,
		       char alivness[LNET_MAX_STR_LEN],
		       __u32 *cpt_iter, __u32 *refcount,
		       __u32 *ni_peer_tx_credits, __u32 *peer_tx_credits,
		       __u32 *peer_rtr_credits, __u32 *peer_min_rtr_credtis,
		       __u32 *peer_tx_qnob);

static inline void
lnet_peer_set_alive(lnet_peer_t *lp)
{
	lp->lp_last_query = jiffies;
	lp->lp_last_alive = jiffies;
	if (!lp->lp_alive)
		lnet_notify_locked(lp, 0, 1, lp->lp_last_alive);
}

#endif

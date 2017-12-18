/*
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_SUNRPC_XPRT_RDMA_H
#define _LINUX_SUNRPC_XPRT_RDMA_H

#include <linux/wait.h> 		/* wait_queue_head_t, etc */
#include <linux/spinlock.h> 		/* spinlock_t, etc */
#include <linux/atomic.h>			/* atomic_t, etc */
#include <linux/workqueue.h>		/* struct work_struct */

#include <rdma/rdma_cm.h>		/* RDMA connection api */
#include <rdma/ib_verbs.h>		/* RDMA verbs api */

#include <linux/sunrpc/clnt.h> 		/* rpc_xprt */
#include <linux/sunrpc/rpc_rdma.h> 	/* RPC/RDMA protocol */
#include <linux/sunrpc/xprtrdma.h> 	/* xprt parameters */

#define RDMA_RESOLVE_TIMEOUT	(5000)	/* 5 seconds */
#define RDMA_CONNECT_RETRY_MAX	(2)	/* retries if no listener backlog */

#define RPCRDMA_BIND_TO		(60U * HZ)
#define RPCRDMA_INIT_REEST_TO	(5U * HZ)
#define RPCRDMA_MAX_REEST_TO	(30U * HZ)
#define RPCRDMA_IDLE_DISC_TO	(5U * 60 * HZ)

/*
 * Interface Adapter -- one per transport instance
 */
struct rpcrdma_ia {
	const struct rpcrdma_memreg_ops	*ri_ops;
	struct ib_device	*ri_device;
	struct rdma_cm_id 	*ri_id;
	struct ib_pd		*ri_pd;
	struct completion	ri_done;
	int			ri_async_rc;
	unsigned int		ri_max_segs;
	unsigned int		ri_max_frmr_depth;
	unsigned int		ri_max_inline_write;
	unsigned int		ri_max_inline_read;
	bool			ri_reminv_expected;
	struct ib_qp_attr	ri_qp_attr;
	struct ib_qp_init_attr	ri_qp_init_attr;
};

/*
 * RDMA Endpoint -- one per transport instance
 */

struct rpcrdma_ep {
	atomic_t		rep_cqcount;
	int			rep_cqinit;
	int			rep_connected;
	struct ib_qp_init_attr	rep_attr;
	wait_queue_head_t 	rep_connect_wait;
	struct rpcrdma_connect_private	rep_cm_private;
	struct rdma_conn_param	rep_remote_cma;
	struct sockaddr_storage	rep_remote_addr;
	struct delayed_work	rep_connect_worker;
};

#define INIT_CQCOUNT(ep) atomic_set(&(ep)->rep_cqcount, (ep)->rep_cqinit)
#define DECR_CQCOUNT(ep) atomic_sub_return(1, &(ep)->rep_cqcount)

/* Pre-allocate extra Work Requests for handling backward receives
 * and sends. This is a fixed value because the Work Queues are
 * allocated when the forward channel is set up.
 */
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
#define RPCRDMA_BACKWARD_WRS		(8)
#else
#define RPCRDMA_BACKWARD_WRS		(0)
#endif

/* Registered buffer -- registered kmalloc'd memory for RDMA SEND/RECV
 *
 * The below structure appears at the front of a large region of kmalloc'd
 * memory, which always starts on a good alignment boundary.
 */

struct rpcrdma_regbuf {
	struct ib_sge		rg_iov;
	struct ib_device	*rg_device;
	enum dma_data_direction	rg_direction;
	__be32			rg_base[0] __attribute__ ((aligned(256)));
};

static inline u64
rdmab_addr(struct rpcrdma_regbuf *rb)
{
	return rb->rg_iov.addr;
}

static inline u32
rdmab_length(struct rpcrdma_regbuf *rb)
{
	return rb->rg_iov.length;
}

static inline u32
rdmab_lkey(struct rpcrdma_regbuf *rb)
{
	return rb->rg_iov.lkey;
}

static inline struct rpcrdma_msg *
rdmab_to_msg(struct rpcrdma_regbuf *rb)
{
	return (struct rpcrdma_msg *)rb->rg_base;
}

#define RPCRDMA_DEF_GFP		(GFP_NOIO | __GFP_NOWARN)

/* To ensure a transport can always make forward progress,
 * the number of RDMA segments allowed in header chunk lists
 * is capped at 8. This prevents less-capable devices and
 * memory registrations from overrunning the Send buffer
 * while building chunk lists.
 *
 * Elements of the Read list take up more room than the
 * Write list or Reply chunk. 8 read segments means the Read
 * list (or Write list or Reply chunk) cannot consume more
 * than
 *
 * ((8 + 2) * read segment size) + 1 XDR words, or 244 bytes.
 *
 * And the fixed part of the header is another 24 bytes.
 *
 * The smallest inline threshold is 1024 bytes, ensuring that
 * at least 750 bytes are available for RPC messages.
 */
enum {
	RPCRDMA_MAX_HDR_SEGS = 8,
	RPCRDMA_HDRBUF_SIZE = 256,
};

/*
 * struct rpcrdma_rep -- this structure encapsulates state required to recv
 * and complete a reply, asychronously. It needs several pieces of
 * state:
 *   o recv buffer (posted to provider)
 *   o ib_sge (also donated to provider)
 *   o status of reply (length, success or not)
 *   o bookkeeping state to get run by reply handler (list, etc)
 *
 * These are allocated during initialization, per-transport instance.
 *
 * N of these are associated with a transport instance, and stored in
 * struct rpcrdma_buffer. N is the max number of outstanding requests.
 */

struct rpcrdma_rep {
	struct ib_cqe		rr_cqe;
	unsigned int		rr_len;
	int			rr_wc_flags;
	u32			rr_inv_rkey;
	struct ib_device	*rr_device;
	struct rpcrdma_xprt	*rr_rxprt;
	struct work_struct	rr_work;
	struct list_head	rr_list;
	struct ib_recv_wr	rr_recv_wr;
	struct rpcrdma_regbuf	*rr_rdmabuf;
};

#define RPCRDMA_BAD_LEN		(~0U)

/*
 * struct rpcrdma_mw - external memory region metadata
 *
 * An external memory region is any buffer or page that is registered
 * on the fly (ie, not pre-registered).
 *
 * Each rpcrdma_buffer has a list of free MWs anchored in rb_mws. During
 * call_allocate, rpcrdma_buffer_get() assigns one to each segment in
 * an rpcrdma_req. Then rpcrdma_register_external() grabs these to keep
 * track of registration metadata while each RPC is pending.
 * rpcrdma_deregister_external() uses this metadata to unmap and
 * release these resources when an RPC is complete.
 */
enum rpcrdma_frmr_state {
	FRMR_IS_INVALID,	/* ready to be used */
	FRMR_IS_VALID,		/* in use */
	FRMR_FLUSHED_FR,	/* flushed FASTREG WR */
	FRMR_FLUSHED_LI,	/* flushed LOCALINV WR */
};

struct rpcrdma_frmr {
	struct ib_mr			*fr_mr;
	struct ib_cqe			fr_cqe;
	enum rpcrdma_frmr_state		fr_state;
	struct completion		fr_linv_done;
	union {
		struct ib_reg_wr	fr_regwr;
		struct ib_send_wr	fr_invwr;
	};
};

struct rpcrdma_fmr {
	struct ib_fmr		*fm_mr;
	u64			*fm_physaddrs;
};

struct rpcrdma_mw {
	struct list_head	mw_list;
	struct scatterlist	*mw_sg;
	int			mw_nents;
	enum dma_data_direction	mw_dir;
	union {
		struct rpcrdma_fmr	fmr;
		struct rpcrdma_frmr	frmr;
	};
	struct rpcrdma_xprt	*mw_xprt;
	u32			mw_handle;
	u32			mw_length;
	u64			mw_offset;
	struct list_head	mw_all;
};

/*
 * struct rpcrdma_req -- structure central to the request/reply sequence.
 *
 * N of these are associated with a transport instance, and stored in
 * struct rpcrdma_buffer. N is the max number of outstanding requests.
 *
 * It includes pre-registered buffer memory for send AND recv.
 * The recv buffer, however, is not owned by this structure, and
 * is "donated" to the hardware when a recv is posted. When a
 * reply is handled, the recv buffer used is given back to the
 * struct rpcrdma_req associated with the request.
 *
 * In addition to the basic memory, this structure includes an array
 * of iovs for send operations. The reason is that the iovs passed to
 * ib_post_{send,recv} must not be modified until the work request
 * completes.
 */

/* Maximum number of page-sized "segments" per chunk list to be
 * registered or invalidated. Must handle a Reply chunk:
 */
enum {
	RPCRDMA_MAX_IOV_SEGS	= 3,
	RPCRDMA_MAX_DATA_SEGS	= ((1 * 1024 * 1024) / PAGE_SIZE) + 1,
	RPCRDMA_MAX_SEGS	= RPCRDMA_MAX_DATA_SEGS +
				  RPCRDMA_MAX_IOV_SEGS,
};

struct rpcrdma_mr_seg {		/* chunk descriptors */
	u32		mr_len;		/* length of chunk or segment */
	struct page	*mr_page;	/* owning page, if any */
	char		*mr_offset;	/* kva if no page, else offset */
};

/* Reserve enough Send SGEs to send a maximum size inline request:
 * - RPC-over-RDMA header
 * - xdr_buf head iovec
 * - RPCRDMA_MAX_INLINE bytes, possibly unaligned, in pages
 * - xdr_buf tail iovec
 */
enum {
	RPCRDMA_MAX_SEND_PAGES = PAGE_SIZE + RPCRDMA_MAX_INLINE - 1,
	RPCRDMA_MAX_PAGE_SGES = (RPCRDMA_MAX_SEND_PAGES >> PAGE_SHIFT) + 1,
	RPCRDMA_MAX_SEND_SGES = 1 + 1 + RPCRDMA_MAX_PAGE_SGES + 1,
};

struct rpcrdma_buffer;
struct rpcrdma_req {
	struct list_head	rl_free;
	unsigned int		rl_mapped_sges;
	unsigned int		rl_connect_cookie;
	struct rpcrdma_buffer	*rl_buffer;
	struct rpcrdma_rep	*rl_reply;
	struct ib_send_wr	rl_send_wr;
	struct ib_sge		rl_send_sge[RPCRDMA_MAX_SEND_SGES];
	struct rpcrdma_regbuf	*rl_rdmabuf;	/* xprt header */
	struct rpcrdma_regbuf	*rl_sendbuf;	/* rq_snd_buf */
	struct rpcrdma_regbuf	*rl_recvbuf;	/* rq_rcv_buf */

	struct ib_cqe		rl_cqe;
	struct list_head	rl_all;
	bool			rl_backchannel;

	struct list_head	rl_registered;	/* registered segments */
	struct rpcrdma_mr_seg	rl_segments[RPCRDMA_MAX_SEGS];
};

static inline void
rpcrdma_set_xprtdata(struct rpc_rqst *rqst, struct rpcrdma_req *req)
{
	rqst->rq_xprtdata = req;
}

static inline struct rpcrdma_req *
rpcr_to_rdmar(struct rpc_rqst *rqst)
{
	return rqst->rq_xprtdata;
}

/*
 * struct rpcrdma_buffer -- holds list/queue of pre-registered memory for
 * inline requests/replies, and client/server credits.
 *
 * One of these is associated with a transport instance
 */
struct rpcrdma_buffer {
	spinlock_t		rb_mwlock;	/* protect rb_mws list */
	struct list_head	rb_mws;
	struct list_head	rb_all;
	char			*rb_pool;

	spinlock_t		rb_lock;	/* protect buf lists */
	int			rb_send_count, rb_recv_count;
	struct list_head	rb_send_bufs;
	struct list_head	rb_recv_bufs;
	u32			rb_max_requests;
	atomic_t		rb_credits;	/* most recent credit grant */

	u32			rb_bc_srv_max_requests;
	spinlock_t		rb_reqslock;	/* protect rb_allreqs */
	struct list_head	rb_allreqs;

	u32			rb_bc_max_requests;

	spinlock_t		rb_recovery_lock; /* protect rb_stale_mrs */
	struct list_head	rb_stale_mrs;
	struct delayed_work	rb_recovery_worker;
	struct delayed_work	rb_refresh_worker;
};
#define rdmab_to_ia(b) (&container_of((b), struct rpcrdma_xprt, rx_buf)->rx_ia)

/*
 * Internal structure for transport instance creation. This
 * exists primarily for modularity.
 *
 * This data should be set with mount options
 */
struct rpcrdma_create_data_internal {
	struct sockaddr_storage	addr;	/* RDMA server address */
	unsigned int	max_requests;	/* max requests (slots) in flight */
	unsigned int	rsize;		/* mount rsize - max read hdr+data */
	unsigned int	wsize;		/* mount wsize - max write hdr+data */
	unsigned int	inline_rsize;	/* max non-rdma read data payload */
	unsigned int	inline_wsize;	/* max non-rdma write data payload */
	unsigned int	padding;	/* non-rdma write header padding */
};

/*
 * Statistics for RPCRDMA
 */
struct rpcrdma_stats {
	unsigned long		read_chunk_count;
	unsigned long		write_chunk_count;
	unsigned long		reply_chunk_count;

	unsigned long long	total_rdma_request;
	unsigned long long	total_rdma_reply;

	unsigned long long	pullup_copy_count;
	unsigned long long	fixup_copy_count;
	unsigned long		hardway_register_count;
	unsigned long		failed_marshal_count;
	unsigned long		bad_reply_count;
	unsigned long		nomsg_call_count;
	unsigned long		bcall_count;
	unsigned long		mrs_recovered;
	unsigned long		mrs_orphaned;
	unsigned long		mrs_allocated;
	unsigned long		local_inv_needed;
};

/*
 * Per-registration mode operations
 */
struct rpcrdma_xprt;
struct rpcrdma_memreg_ops {
	int		(*ro_map)(struct rpcrdma_xprt *,
				  struct rpcrdma_mr_seg *, int, bool,
				  struct rpcrdma_mw **);
	void		(*ro_unmap_sync)(struct rpcrdma_xprt *,
					 struct rpcrdma_req *);
	void		(*ro_unmap_safe)(struct rpcrdma_xprt *,
					 struct rpcrdma_req *, bool);
	void		(*ro_recover_mr)(struct rpcrdma_mw *);
	int		(*ro_open)(struct rpcrdma_ia *,
				   struct rpcrdma_ep *,
				   struct rpcrdma_create_data_internal *);
	size_t		(*ro_maxpages)(struct rpcrdma_xprt *);
	int		(*ro_init_mr)(struct rpcrdma_ia *,
				      struct rpcrdma_mw *);
	void		(*ro_release_mr)(struct rpcrdma_mw *);
	const char	*ro_displayname;
	const int	ro_send_w_inv_ok;
};

extern const struct rpcrdma_memreg_ops rpcrdma_fmr_memreg_ops;
extern const struct rpcrdma_memreg_ops rpcrdma_frwr_memreg_ops;

/*
 * RPCRDMA transport -- encapsulates the structures above for
 * integration with RPC.
 *
 * The contained structures are embedded, not pointers,
 * for convenience. This structure need not be visible externally.
 *
 * It is allocated and initialized during mount, and released
 * during unmount.
 */
struct rpcrdma_xprt {
	struct rpc_xprt		rx_xprt;
	struct rpcrdma_ia	rx_ia;
	struct rpcrdma_ep	rx_ep;
	struct rpcrdma_buffer	rx_buf;
	struct rpcrdma_create_data_internal rx_data;
	struct delayed_work	rx_connect_worker;
	struct rpcrdma_stats	rx_stats;
};

#define rpcx_to_rdmax(x) container_of(x, struct rpcrdma_xprt, rx_xprt)
#define rpcx_to_rdmad(x) (rpcx_to_rdmax(x)->rx_data)

/* Setting this to 0 ensures interoperability with early servers.
 * Setting this to 1 enhances certain unaligned read/write performance.
 * Default is 0, see sysctl entry and rpc_rdma.c rpcrdma_convert_iovs() */
extern int xprt_rdma_pad_optimize;

/*
 * Interface Adapter calls - xprtrdma/verbs.c
 */
int rpcrdma_ia_open(struct rpcrdma_xprt *, struct sockaddr *, int);
void rpcrdma_ia_close(struct rpcrdma_ia *);
bool frwr_is_supported(struct rpcrdma_ia *);
bool fmr_is_supported(struct rpcrdma_ia *);

/*
 * Endpoint calls - xprtrdma/verbs.c
 */
int rpcrdma_ep_create(struct rpcrdma_ep *, struct rpcrdma_ia *,
				struct rpcrdma_create_data_internal *);
void rpcrdma_ep_destroy(struct rpcrdma_ep *, struct rpcrdma_ia *);
int rpcrdma_ep_connect(struct rpcrdma_ep *, struct rpcrdma_ia *);
void rpcrdma_ep_disconnect(struct rpcrdma_ep *, struct rpcrdma_ia *);

int rpcrdma_ep_post(struct rpcrdma_ia *, struct rpcrdma_ep *,
				struct rpcrdma_req *);
int rpcrdma_ep_post_recv(struct rpcrdma_ia *, struct rpcrdma_rep *);

/*
 * Buffer calls - xprtrdma/verbs.c
 */
struct rpcrdma_req *rpcrdma_create_req(struct rpcrdma_xprt *);
struct rpcrdma_rep *rpcrdma_create_rep(struct rpcrdma_xprt *);
void rpcrdma_destroy_req(struct rpcrdma_req *);
int rpcrdma_buffer_create(struct rpcrdma_xprt *);
void rpcrdma_buffer_destroy(struct rpcrdma_buffer *);

struct rpcrdma_mw *rpcrdma_get_mw(struct rpcrdma_xprt *);
void rpcrdma_put_mw(struct rpcrdma_xprt *, struct rpcrdma_mw *);
struct rpcrdma_req *rpcrdma_buffer_get(struct rpcrdma_buffer *);
void rpcrdma_buffer_put(struct rpcrdma_req *);
void rpcrdma_recv_buffer_get(struct rpcrdma_req *);
void rpcrdma_recv_buffer_put(struct rpcrdma_rep *);

void rpcrdma_defer_mr_recovery(struct rpcrdma_mw *);

struct rpcrdma_regbuf *rpcrdma_alloc_regbuf(size_t, enum dma_data_direction,
					    gfp_t);
bool __rpcrdma_dma_map_regbuf(struct rpcrdma_ia *, struct rpcrdma_regbuf *);
void rpcrdma_free_regbuf(struct rpcrdma_regbuf *);

static inline bool
rpcrdma_regbuf_is_mapped(struct rpcrdma_regbuf *rb)
{
	return rb->rg_device != NULL;
}

static inline bool
rpcrdma_dma_map_regbuf(struct rpcrdma_ia *ia, struct rpcrdma_regbuf *rb)
{
	if (likely(rpcrdma_regbuf_is_mapped(rb)))
		return true;
	return __rpcrdma_dma_map_regbuf(ia, rb);
}

int rpcrdma_ep_post_extra_recv(struct rpcrdma_xprt *, unsigned int);

int rpcrdma_alloc_wq(void);
void rpcrdma_destroy_wq(void);

/*
 * Wrappers for chunk registration, shared by read/write chunk code.
 */

static inline enum dma_data_direction
rpcrdma_data_dir(bool writing)
{
	return writing ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
}

/*
 * RPC/RDMA connection management calls - xprtrdma/rpc_rdma.c
 */
void rpcrdma_connect_worker(struct work_struct *);
void rpcrdma_conn_func(struct rpcrdma_ep *);
void rpcrdma_reply_handler(struct work_struct *);

/*
 * RPC/RDMA protocol calls - xprtrdma/rpc_rdma.c
 */

enum rpcrdma_chunktype {
	rpcrdma_noch = 0,
	rpcrdma_readch,
	rpcrdma_areadch,
	rpcrdma_writech,
	rpcrdma_replych
};

bool rpcrdma_prepare_send_sges(struct rpcrdma_ia *, struct rpcrdma_req *,
			       u32, struct xdr_buf *, enum rpcrdma_chunktype);
void rpcrdma_unmap_sges(struct rpcrdma_ia *, struct rpcrdma_req *);
int rpcrdma_marshal_req(struct rpc_rqst *);
void rpcrdma_set_max_header_sizes(struct rpcrdma_xprt *);

/* RPC/RDMA module init - xprtrdma/transport.c
 */
extern unsigned int xprt_rdma_max_inline_read;
void xprt_rdma_format_addresses(struct rpc_xprt *xprt, struct sockaddr *sap);
void xprt_rdma_free_addresses(struct rpc_xprt *xprt);
void xprt_rdma_print_stats(struct rpc_xprt *xprt, struct seq_file *seq);
int xprt_rdma_init(void);
void xprt_rdma_cleanup(void);

/* Backchannel calls - xprtrdma/backchannel.c
 */
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
int xprt_rdma_bc_setup(struct rpc_xprt *, unsigned int);
int xprt_rdma_bc_up(struct svc_serv *, struct net *);
size_t xprt_rdma_bc_maxpayload(struct rpc_xprt *);
int rpcrdma_bc_post_recv(struct rpcrdma_xprt *, unsigned int);
void rpcrdma_bc_receive_call(struct rpcrdma_xprt *, struct rpcrdma_rep *);
int rpcrdma_bc_marshal_reply(struct rpc_rqst *);
void xprt_rdma_bc_free_rqst(struct rpc_rqst *);
void xprt_rdma_bc_destroy(struct rpc_xprt *, unsigned int);
#endif	/* CONFIG_SUNRPC_BACKCHANNEL */

extern struct xprt_class xprt_rdma_bc;

#endif				/* _LINUX_SUNRPC_XPRT_RDMA_H */

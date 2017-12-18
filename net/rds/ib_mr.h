/*
 * Copyright (c) 2016 Oracle.  All rights reserved.
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
 */
#ifndef _RDS_IB_MR_H
#define _RDS_IB_MR_H

#include <linux/kernel.h>

#include "rds.h"
#include "ib.h"

#define RDS_MR_1M_POOL_SIZE		(8192 / 2)
#define RDS_MR_1M_MSG_SIZE		256
#define RDS_MR_8K_MSG_SIZE		2
#define RDS_MR_8K_SCALE			(256 / (RDS_MR_8K_MSG_SIZE + 1))
#define RDS_MR_8K_POOL_SIZE		(RDS_MR_8K_SCALE * (8192 / 2))

struct rds_ib_fmr {
	struct ib_fmr		*fmr;
	u64			*dma;
};

enum rds_ib_fr_state {
	FRMR_IS_FREE,	/* mr invalidated & ready for use */
	FRMR_IS_INUSE,	/* mr is in use or used & can be invalidated */
	FRMR_IS_STALE,	/* Stale MR and needs to be dropped  */
};

struct rds_ib_frmr {
	struct ib_mr		*mr;
	enum rds_ib_fr_state	fr_state;
	bool			fr_inv;
	struct ib_send_wr	fr_wr;
	unsigned int		dma_npages;
	unsigned int		sg_byte_len;
};

/* This is stored as mr->r_trans_private. */
struct rds_ib_mr {
	struct rds_ib_device		*device;
	struct rds_ib_mr_pool		*pool;
	struct rds_ib_connection	*ic;

	struct llist_node		llnode;

	/* unmap_list is for freeing */
	struct list_head		unmap_list;
	unsigned int			remap_count;

	struct scatterlist		*sg;
	unsigned int			sg_len;
	int				sg_dma_len;

	union {
		struct rds_ib_fmr	fmr;
		struct rds_ib_frmr	frmr;
	} u;
};

/* Our own little MR pool */
struct rds_ib_mr_pool {
	unsigned int            pool_type;
	struct mutex		flush_lock;	/* serialize fmr invalidate */
	struct delayed_work	flush_worker;	/* flush worker */

	atomic_t		item_count;	/* total # of MRs */
	atomic_t		dirty_count;	/* # dirty of MRs */

	struct llist_head	drop_list;	/* MRs not reached max_maps */
	struct llist_head	free_list;	/* unused MRs */
	struct llist_head	clean_list;	/* unused & unmapped MRs */
	wait_queue_head_t	flush_wait;

	atomic_t		free_pinned;	/* memory pinned by free MRs */
	unsigned long		max_items;
	unsigned long		max_items_soft;
	unsigned long		max_free_pinned;
	struct ib_fmr_attr	fmr_attr;
	bool			use_fastreg;
};

extern struct workqueue_struct *rds_ib_mr_wq;
extern unsigned int rds_ib_mr_1m_pool_size;
extern unsigned int rds_ib_mr_8k_pool_size;
extern bool prefer_frmr;

struct rds_ib_mr_pool *rds_ib_create_mr_pool(struct rds_ib_device *rds_dev,
					     int npages);
void rds_ib_get_mr_info(struct rds_ib_device *rds_ibdev,
			struct rds_info_rdma_connection *iinfo);
void rds_ib_destroy_mr_pool(struct rds_ib_mr_pool *);
void *rds_ib_get_mr(struct scatterlist *sg, unsigned long nents,
		    struct rds_sock *rs, u32 *key_ret);
void rds_ib_sync_mr(void *trans_private, int dir);
void rds_ib_free_mr(void *trans_private, int invalidate);
void rds_ib_flush_mrs(void);
int rds_ib_mr_init(void);
void rds_ib_mr_exit(void);

void __rds_ib_teardown_mr(struct rds_ib_mr *);
void rds_ib_teardown_mr(struct rds_ib_mr *);
struct rds_ib_mr *rds_ib_alloc_fmr(struct rds_ib_device *, int);
int rds_ib_map_fmr(struct rds_ib_device *, struct rds_ib_mr *,
		   struct scatterlist *, unsigned int);
struct rds_ib_mr *rds_ib_reuse_mr(struct rds_ib_mr_pool *);
int rds_ib_flush_mr_pool(struct rds_ib_mr_pool *, int, struct rds_ib_mr **);
struct rds_ib_mr *rds_ib_reg_fmr(struct rds_ib_device *, struct scatterlist *,
				 unsigned long, u32 *);
struct rds_ib_mr *rds_ib_try_reuse_ibmr(struct rds_ib_mr_pool *);
void rds_ib_unreg_fmr(struct list_head *, unsigned int *,
		      unsigned long *, unsigned int);
void rds_ib_free_fmr_list(struct rds_ib_mr *);
struct rds_ib_mr *rds_ib_reg_frmr(struct rds_ib_device *rds_ibdev,
				  struct rds_ib_connection *ic,
				  struct scatterlist *sg,
				  unsigned long nents, u32 *key);
void rds_ib_unreg_frmr(struct list_head *list, unsigned int *nfreed,
		       unsigned long *unpinned, unsigned int goal);
void rds_ib_free_frmr_list(struct rds_ib_mr *);
#endif

/* QLogic qed NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#ifndef _QED_ROCE_H
#define _QED_ROCE_H
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_roce_if.h>
#include "qed.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_ll2.h"

#define QED_RDMA_MAX_FMR                    (RDMA_MAX_TIDS)
#define QED_RDMA_MAX_P_KEY                  (1)
#define QED_RDMA_MAX_WQE                    (0x7FFF)
#define QED_RDMA_MAX_SRQ_WQE_ELEM           (0x7FFF)
#define QED_RDMA_PAGE_SIZE_CAPS             (0xFFFFF000)
#define QED_RDMA_ACK_DELAY                  (15)
#define QED_RDMA_MAX_MR_SIZE                (0x10000000000ULL)
#define QED_RDMA_MAX_CQS                    (RDMA_MAX_CQS)
#define QED_RDMA_MAX_MRS                    (RDMA_MAX_TIDS)
/* Add 1 for header element */
#define QED_RDMA_MAX_SRQ_ELEM_PER_WQE	    (RDMA_MAX_SGE_PER_RQ_WQE + 1)
#define QED_RDMA_MAX_SGE_PER_SRQ_WQE        (RDMA_MAX_SGE_PER_RQ_WQE)
#define QED_RDMA_SRQ_WQE_ELEM_SIZE          (16)
#define QED_RDMA_MAX_SRQS                   (32 * 1024)

#define QED_RDMA_MAX_CQE_32_BIT             (0x7FFFFFFF - 1)
#define QED_RDMA_MAX_CQE_16_BIT             (0x7FFF - 1)

enum qed_rdma_toggle_bit {
	QED_RDMA_TOGGLE_BIT_CLEAR = 0,
	QED_RDMA_TOGGLE_BIT_SET = 1
};

struct qed_bmap {
	unsigned long *bitmap;
	u32 max_count;
};

struct qed_rdma_info {
	/* spin lock to protect bitmaps */
	spinlock_t lock;

	struct qed_bmap cq_map;
	struct qed_bmap pd_map;
	struct qed_bmap tid_map;
	struct qed_bmap qp_map;
	struct qed_bmap srq_map;
	struct qed_bmap cid_map;
	struct qed_bmap dpi_map;
	struct qed_bmap toggle_bits;
	struct qed_rdma_events events;
	struct qed_rdma_device *dev;
	struct qed_rdma_port *port;
	u32 last_tid;
	u8 num_cnqs;
	u32 num_qps;
	u32 num_mrs;
	u16 queue_zone_base;
	enum protocol_type proto;
};

struct qed_rdma_qp {
	struct regpair qp_handle;
	struct regpair qp_handle_async;
	u32 qpid;
	u16 icid;
	enum qed_roce_qp_state cur_state;
	bool use_srq;
	bool signal_all;
	bool fmr_and_reserved_lkey;

	bool incoming_rdma_read_en;
	bool incoming_rdma_write_en;
	bool incoming_atomic_en;
	bool e2e_flow_control_en;

	u16 pd;
	u16 pkey;
	u32 dest_qp;
	u16 mtu;
	u16 srq_id;
	u8 traffic_class_tos;
	u8 hop_limit_ttl;
	u16 dpi;
	u32 flow_label;
	bool lb_indication;
	u16 vlan_id;
	u32 ack_timeout;
	u8 retry_cnt;
	u8 rnr_retry_cnt;
	u8 min_rnr_nak_timer;
	bool sqd_async;
	union qed_gid sgid;
	union qed_gid dgid;
	enum roce_mode roce_mode;
	u16 udp_src_port;
	u8 stats_queue;

	/* requeseter */
	u8 max_rd_atomic_req;
	u32 sq_psn;
	u16 sq_cq_id;
	u16 sq_num_pages;
	dma_addr_t sq_pbl_ptr;
	void *orq;
	dma_addr_t orq_phys_addr;
	u8 orq_num_pages;
	bool req_offloaded;

	/* responder */
	u8 max_rd_atomic_resp;
	u32 rq_psn;
	u16 rq_cq_id;
	u16 rq_num_pages;
	dma_addr_t rq_pbl_ptr;
	void *irq;
	dma_addr_t irq_phys_addr;
	u8 irq_num_pages;
	bool resp_offloaded;

	u8 remote_mac_addr[6];
	u8 local_mac_addr[6];

	void *shared_queue;
	dma_addr_t shared_queue_phys_addr;
};

#if IS_ENABLED(CONFIG_QED_RDMA)
void qed_rdma_dpm_bar(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
void qed_async_roce_event(struct qed_hwfn *p_hwfn,
			  struct event_ring_entry *p_eqe);
void qed_ll2b_complete_tx_gsi_packet(struct qed_hwfn *p_hwfn,
				     u8 connection_handle,
				     void *cookie,
				     dma_addr_t first_frag_addr,
				     bool b_last_fragment, bool b_last_packet);
void qed_ll2b_release_tx_gsi_packet(struct qed_hwfn *p_hwfn,
				    u8 connection_handle,
				    void *cookie,
				    dma_addr_t first_frag_addr,
				    bool b_last_fragment, bool b_last_packet);
void qed_ll2b_complete_rx_gsi_packet(struct qed_hwfn *p_hwfn,
				     u8 connection_handle,
				     void *cookie,
				     dma_addr_t rx_buf_addr,
				     u16 data_length,
				     u8 data_length_error,
				     u16 parse_flags,
				     u16 vlan,
				     u32 src_mac_addr_hi,
				     u16 src_mac_addr_lo, bool b_last_packet);
#else
static inline void qed_rdma_dpm_bar(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt) {}
static inline void qed_async_roce_event(struct qed_hwfn *p_hwfn, struct event_ring_entry *p_eqe) {}
static inline void qed_ll2b_complete_tx_gsi_packet(struct qed_hwfn *p_hwfn,
						   u8 connection_handle,
						   void *cookie,
						   dma_addr_t first_frag_addr,
						   bool b_last_fragment,
						   bool b_last_packet) {}
static inline void qed_ll2b_release_tx_gsi_packet(struct qed_hwfn *p_hwfn,
						  u8 connection_handle,
						  void *cookie,
						  dma_addr_t first_frag_addr,
						  bool b_last_fragment,
						  bool b_last_packet) {}
static inline void qed_ll2b_complete_rx_gsi_packet(struct qed_hwfn *p_hwfn,
						   u8 connection_handle,
						   void *cookie,
						   dma_addr_t rx_buf_addr,
						   u16 data_length,
						   u8 data_length_error,
						   u16 parse_flags,
						   u16 vlan,
						   u32 src_mac_addr_hi,
						   u16 src_mac_addr_lo,
						   bool b_last_packet) {}
#endif
#endif

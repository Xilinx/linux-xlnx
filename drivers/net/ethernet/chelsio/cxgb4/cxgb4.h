/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __CXGB4_H__
#define __CXGB4_H__

#include "t4_hw.h"

#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/net_tstamp.h>
#include <asm/io.h>
#include "t4_chip_type.h"
#include "cxgb4_uld.h"

#define CH_WARN(adap, fmt, ...) dev_warn(adap->pdev_dev, fmt, ## __VA_ARGS__)
extern struct list_head adapter_list;
extern struct mutex uld_mutex;

enum {
	MAX_NPORTS	= 4,     /* max # of ports */
	SERNUM_LEN	= 24,    /* Serial # length */
	EC_LEN		= 16,    /* E/C length */
	ID_LEN		= 16,    /* ID length */
	PN_LEN		= 16,    /* Part Number length */
	MACADDR_LEN	= 12,    /* MAC Address length */
};

enum {
	T4_REGMAP_SIZE = (160 * 1024),
	T5_REGMAP_SIZE = (332 * 1024),
};

enum {
	MEM_EDC0,
	MEM_EDC1,
	MEM_MC,
	MEM_MC0 = MEM_MC,
	MEM_MC1
};

enum {
	MEMWIN0_APERTURE = 2048,
	MEMWIN0_BASE     = 0x1b800,
	MEMWIN1_APERTURE = 32768,
	MEMWIN1_BASE     = 0x28000,
	MEMWIN1_BASE_T5  = 0x52000,
	MEMWIN2_APERTURE = 65536,
	MEMWIN2_BASE     = 0x30000,
	MEMWIN2_APERTURE_T5 = 131072,
	MEMWIN2_BASE_T5  = 0x60000,
};

enum dev_master {
	MASTER_CANT,
	MASTER_MAY,
	MASTER_MUST
};

enum dev_state {
	DEV_STATE_UNINIT,
	DEV_STATE_INIT,
	DEV_STATE_ERR
};

enum {
	PAUSE_RX      = 1 << 0,
	PAUSE_TX      = 1 << 1,
	PAUSE_AUTONEG = 1 << 2
};

struct port_stats {
	u64 tx_octets;            /* total # of octets in good frames */
	u64 tx_frames;            /* all good frames */
	u64 tx_bcast_frames;      /* all broadcast frames */
	u64 tx_mcast_frames;      /* all multicast frames */
	u64 tx_ucast_frames;      /* all unicast frames */
	u64 tx_error_frames;      /* all error frames */

	u64 tx_frames_64;         /* # of Tx frames in a particular range */
	u64 tx_frames_65_127;
	u64 tx_frames_128_255;
	u64 tx_frames_256_511;
	u64 tx_frames_512_1023;
	u64 tx_frames_1024_1518;
	u64 tx_frames_1519_max;

	u64 tx_drop;              /* # of dropped Tx frames */
	u64 tx_pause;             /* # of transmitted pause frames */
	u64 tx_ppp0;              /* # of transmitted PPP prio 0 frames */
	u64 tx_ppp1;              /* # of transmitted PPP prio 1 frames */
	u64 tx_ppp2;              /* # of transmitted PPP prio 2 frames */
	u64 tx_ppp3;              /* # of transmitted PPP prio 3 frames */
	u64 tx_ppp4;              /* # of transmitted PPP prio 4 frames */
	u64 tx_ppp5;              /* # of transmitted PPP prio 5 frames */
	u64 tx_ppp6;              /* # of transmitted PPP prio 6 frames */
	u64 tx_ppp7;              /* # of transmitted PPP prio 7 frames */

	u64 rx_octets;            /* total # of octets in good frames */
	u64 rx_frames;            /* all good frames */
	u64 rx_bcast_frames;      /* all broadcast frames */
	u64 rx_mcast_frames;      /* all multicast frames */
	u64 rx_ucast_frames;      /* all unicast frames */
	u64 rx_too_long;          /* # of frames exceeding MTU */
	u64 rx_jabber;            /* # of jabber frames */
	u64 rx_fcs_err;           /* # of received frames with bad FCS */
	u64 rx_len_err;           /* # of received frames with length error */
	u64 rx_symbol_err;        /* symbol errors */
	u64 rx_runt;              /* # of short frames */

	u64 rx_frames_64;         /* # of Rx frames in a particular range */
	u64 rx_frames_65_127;
	u64 rx_frames_128_255;
	u64 rx_frames_256_511;
	u64 rx_frames_512_1023;
	u64 rx_frames_1024_1518;
	u64 rx_frames_1519_max;

	u64 rx_pause;             /* # of received pause frames */
	u64 rx_ppp0;              /* # of received PPP prio 0 frames */
	u64 rx_ppp1;              /* # of received PPP prio 1 frames */
	u64 rx_ppp2;              /* # of received PPP prio 2 frames */
	u64 rx_ppp3;              /* # of received PPP prio 3 frames */
	u64 rx_ppp4;              /* # of received PPP prio 4 frames */
	u64 rx_ppp5;              /* # of received PPP prio 5 frames */
	u64 rx_ppp6;              /* # of received PPP prio 6 frames */
	u64 rx_ppp7;              /* # of received PPP prio 7 frames */

	u64 rx_ovflow0;           /* drops due to buffer-group 0 overflows */
	u64 rx_ovflow1;           /* drops due to buffer-group 1 overflows */
	u64 rx_ovflow2;           /* drops due to buffer-group 2 overflows */
	u64 rx_ovflow3;           /* drops due to buffer-group 3 overflows */
	u64 rx_trunc0;            /* buffer-group 0 truncated packets */
	u64 rx_trunc1;            /* buffer-group 1 truncated packets */
	u64 rx_trunc2;            /* buffer-group 2 truncated packets */
	u64 rx_trunc3;            /* buffer-group 3 truncated packets */
};

struct lb_port_stats {
	u64 octets;
	u64 frames;
	u64 bcast_frames;
	u64 mcast_frames;
	u64 ucast_frames;
	u64 error_frames;

	u64 frames_64;
	u64 frames_65_127;
	u64 frames_128_255;
	u64 frames_256_511;
	u64 frames_512_1023;
	u64 frames_1024_1518;
	u64 frames_1519_max;

	u64 drop;

	u64 ovflow0;
	u64 ovflow1;
	u64 ovflow2;
	u64 ovflow3;
	u64 trunc0;
	u64 trunc1;
	u64 trunc2;
	u64 trunc3;
};

struct tp_tcp_stats {
	u32 tcp_out_rsts;
	u64 tcp_in_segs;
	u64 tcp_out_segs;
	u64 tcp_retrans_segs;
};

struct tp_usm_stats {
	u32 frames;
	u32 drops;
	u64 octets;
};

struct tp_fcoe_stats {
	u32 frames_ddp;
	u32 frames_drop;
	u64 octets_ddp;
};

struct tp_err_stats {
	u32 mac_in_errs[4];
	u32 hdr_in_errs[4];
	u32 tcp_in_errs[4];
	u32 tnl_cong_drops[4];
	u32 ofld_chan_drops[4];
	u32 tnl_tx_drops[4];
	u32 ofld_vlan_drops[4];
	u32 tcp6_in_errs[4];
	u32 ofld_no_neigh;
	u32 ofld_cong_defer;
};

struct tp_cpl_stats {
	u32 req[4];
	u32 rsp[4];
};

struct tp_rdma_stats {
	u32 rqe_dfr_pkt;
	u32 rqe_dfr_mod;
};

struct sge_params {
	u32 hps;			/* host page size for our PF/VF */
	u32 eq_qpp;			/* egress queues/page for our PF/VF */
	u32 iq_qpp;			/* egress queues/page for our PF/VF */
};

struct tp_params {
	unsigned int tre;            /* log2 of core clocks per TP tick */
	unsigned int la_mask;        /* what events are recorded by TP LA */
	unsigned short tx_modq_map;  /* TX modulation scheduler queue to */
				     /* channel map */

	uint32_t dack_re;            /* DACK timer resolution */
	unsigned short tx_modq[NCHAN];	/* channel to modulation queue map */

	u32 vlan_pri_map;               /* cached TP_VLAN_PRI_MAP */
	u32 ingress_config;             /* cached TP_INGRESS_CONFIG */

	/* TP_VLAN_PRI_MAP Compressed Filter Tuple field offsets.  This is a
	 * subset of the set of fields which may be present in the Compressed
	 * Filter Tuple portion of filters and TCP TCB connections.  The
	 * fields which are present are controlled by the TP_VLAN_PRI_MAP.
	 * Since a variable number of fields may or may not be present, their
	 * shifted field positions within the Compressed Filter Tuple may
	 * vary, or not even be present if the field isn't selected in
	 * TP_VLAN_PRI_MAP.  Since some of these fields are needed in various
	 * places we store their offsets here, or a -1 if the field isn't
	 * present.
	 */
	int vlan_shift;
	int vnic_shift;
	int port_shift;
	int protocol_shift;
};

struct vpd_params {
	unsigned int cclk;
	u8 ec[EC_LEN + 1];
	u8 sn[SERNUM_LEN + 1];
	u8 id[ID_LEN + 1];
	u8 pn[PN_LEN + 1];
	u8 na[MACADDR_LEN + 1];
};

struct pci_params {
	unsigned char speed;
	unsigned char width;
};

struct devlog_params {
	u32 memtype;                    /* which memory (EDC0, EDC1, MC) */
	u32 start;                      /* start of log in firmware memory */
	u32 size;                       /* size of log */
};

/* Stores chip specific parameters */
struct arch_specific_params {
	u8 nchan;
	u8 pm_stats_cnt;
	u8 cng_ch_bits_log;		/* congestion channel map bits width */
	u16 mps_rplc_size;
	u16 vfcount;
	u32 sge_fl_db;
	u16 mps_tcam_size;
};

struct adapter_params {
	struct sge_params sge;
	struct tp_params  tp;
	struct vpd_params vpd;
	struct pci_params pci;
	struct devlog_params devlog;
	enum pcie_memwin drv_memwin;

	unsigned int cim_la_size;

	unsigned int sf_size;             /* serial flash size in bytes */
	unsigned int sf_nsec;             /* # of flash sectors */
	unsigned int sf_fw_start;         /* start of FW image in flash */

	unsigned int fw_vers;
	unsigned int bs_vers;		/* bootstrap version */
	unsigned int tp_vers;
	unsigned int er_vers;		/* expansion ROM version */
	u8 api_vers[7];

	unsigned short mtus[NMTUS];
	unsigned short a_wnd[NCCTRL_WIN];
	unsigned short b_wnd[NCCTRL_WIN];

	unsigned char nports;             /* # of ethernet ports */
	unsigned char portvec;
	enum chip_type chip;               /* chip code */
	struct arch_specific_params arch;  /* chip specific params */
	unsigned char offload;
	unsigned char crypto;		/* HW capability for crypto */

	unsigned char bypass;

	unsigned int ofldq_wr_cred;
	bool ulptx_memwrite_dsgl;          /* use of T5 DSGL allowed */

	unsigned int nsched_cls;          /* number of traffic classes */
	unsigned int max_ordird_qp;       /* Max read depth per RDMA QP */
	unsigned int max_ird_adapter;     /* Max read depth per adapter */
	bool fr_nsmr_tpte_wr_support;	  /* FW support for FR_NSMR_TPTE_WR */
};

/* State needed to monitor the forward progress of SGE Ingress DMA activities
 * and possible hangs.
 */
struct sge_idma_monitor_state {
	unsigned int idma_1s_thresh;	/* 1s threshold in Core Clock ticks */
	unsigned int idma_stalled[2];	/* synthesized stalled timers in HZ */
	unsigned int idma_state[2];	/* IDMA Hang detect state */
	unsigned int idma_qid[2];	/* IDMA Hung Ingress Queue ID */
	unsigned int idma_warn[2];	/* time to warning in HZ */
};

/* Firmware Mailbox Command/Reply log.  All values are in Host-Endian format.
 * The access and execute times are signed in order to accommodate negative
 * error returns.
 */
struct mbox_cmd {
	u64 cmd[MBOX_LEN / 8];		/* a Firmware Mailbox Command/Reply */
	u64 timestamp;			/* OS-dependent timestamp */
	u32 seqno;			/* sequence number */
	s16 access;			/* time (ms) to access mailbox */
	s16 execute;			/* time (ms) to execute */
};

struct mbox_cmd_log {
	unsigned int size;		/* number of entries in the log */
	unsigned int cursor;		/* next position in the log to write */
	u32 seqno;			/* next sequence number */
	/* variable length mailbox command log starts here */
};

/* Given a pointer to a Firmware Mailbox Command Log and a log entry index,
 * return a pointer to the specified entry.
 */
static inline struct mbox_cmd *mbox_cmd_log_entry(struct mbox_cmd_log *log,
						  unsigned int entry_idx)
{
	return &((struct mbox_cmd *)&(log)[1])[entry_idx];
}

#include "t4fw_api.h"

#define FW_VERSION(chip) ( \
		FW_HDR_FW_VER_MAJOR_G(chip##FW_VERSION_MAJOR) | \
		FW_HDR_FW_VER_MINOR_G(chip##FW_VERSION_MINOR) | \
		FW_HDR_FW_VER_MICRO_G(chip##FW_VERSION_MICRO) | \
		FW_HDR_FW_VER_BUILD_G(chip##FW_VERSION_BUILD))
#define FW_INTFVER(chip, intf) (FW_HDR_INTFVER_##intf)

struct fw_info {
	u8 chip;
	char *fs_name;
	char *fw_mod_name;
	struct fw_hdr fw_hdr;
};

struct trace_params {
	u32 data[TRACE_LEN / 4];
	u32 mask[TRACE_LEN / 4];
	unsigned short snap_len;
	unsigned short min_len;
	unsigned char skip_ofst;
	unsigned char skip_len;
	unsigned char invert;
	unsigned char port;
};

struct link_config {
	unsigned short supported;        /* link capabilities */
	unsigned short advertising;      /* advertised capabilities */
	unsigned short lp_advertising;   /* peer advertised capabilities */
	unsigned int   requested_speed;  /* speed user has requested */
	unsigned int   speed;            /* actual link speed */
	unsigned char  requested_fc;     /* flow control user has requested */
	unsigned char  fc;               /* actual link flow control */
	unsigned char  autoneg;          /* autonegotiating? */
	unsigned char  link_ok;          /* link up? */
	unsigned char  link_down_rc;     /* link down reason */
};

#define FW_LEN16(fw_struct) FW_CMD_LEN16_V(sizeof(fw_struct) / 16)

enum {
	MAX_ETH_QSETS = 32,           /* # of Ethernet Tx/Rx queue sets */
	MAX_OFLD_QSETS = 16,          /* # of offload Tx, iscsi Rx queue sets */
	MAX_CTRL_QUEUES = NCHAN,      /* # of control Tx queues */
};

enum {
	MAX_TXQ_ENTRIES      = 16384,
	MAX_CTRL_TXQ_ENTRIES = 1024,
	MAX_RSPQ_ENTRIES     = 16384,
	MAX_RX_BUFFERS       = 16384,
	MIN_TXQ_ENTRIES      = 32,
	MIN_CTRL_TXQ_ENTRIES = 32,
	MIN_RSPQ_ENTRIES     = 128,
	MIN_FL_ENTRIES       = 16
};

enum {
	INGQ_EXTRAS = 2,        /* firmware event queue and */
				/*   forwarded interrupts */
	MAX_INGQ = MAX_ETH_QSETS + INGQ_EXTRAS,
};

struct adapter;
struct sge_rspq;

#include "cxgb4_dcb.h"

#ifdef CONFIG_CHELSIO_T4_FCOE
#include "cxgb4_fcoe.h"
#endif /* CONFIG_CHELSIO_T4_FCOE */

struct port_info {
	struct adapter *adapter;
	u16    viid;
	s16    xact_addr_filt;        /* index of exact MAC address filter */
	u16    rss_size;              /* size of VI's RSS table slice */
	s8     mdio_addr;
	enum fw_port_type port_type;
	u8     mod_type;
	u8     port_id;
	u8     tx_chan;
	u8     lport;                 /* associated offload logical port */
	u8     nqsets;                /* # of qsets */
	u8     first_qset;            /* index of first qset */
	u8     rss_mode;
	struct link_config link_cfg;
	u16   *rss;
	struct port_stats stats_base;
#ifdef CONFIG_CHELSIO_T4_DCB
	struct port_dcb_info dcb;     /* Data Center Bridging support */
#endif
#ifdef CONFIG_CHELSIO_T4_FCOE
	struct cxgb_fcoe fcoe;
#endif /* CONFIG_CHELSIO_T4_FCOE */
	bool rxtstamp;  /* Enable TS */
	struct hwtstamp_config tstamp_config;
	struct sched_table *sched_tbl;
};

struct dentry;
struct work_struct;

enum {                                 /* adapter flags */
	FULL_INIT_DONE     = (1 << 0),
	DEV_ENABLED        = (1 << 1),
	USING_MSI          = (1 << 2),
	USING_MSIX         = (1 << 3),
	FW_OK              = (1 << 4),
	RSS_TNLALLLOOKUP   = (1 << 5),
	USING_SOFT_PARAMS  = (1 << 6),
	MASTER_PF          = (1 << 7),
	FW_OFLD_CONN       = (1 << 9),
};

enum {
	ULP_CRYPTO_LOOKASIDE = 1 << 0,
};

struct rx_sw_desc;

struct sge_fl {                     /* SGE free-buffer queue state */
	unsigned int avail;         /* # of available Rx buffers */
	unsigned int pend_cred;     /* new buffers since last FL DB ring */
	unsigned int cidx;          /* consumer index */
	unsigned int pidx;          /* producer index */
	unsigned long alloc_failed; /* # of times buffer allocation failed */
	unsigned long large_alloc_failed;
	unsigned long mapping_err;  /* # of RX Buffer DMA Mapping failures */
	unsigned long low;          /* # of times momentarily starving */
	unsigned long starving;
	/* RO fields */
	unsigned int cntxt_id;      /* SGE context id for the free list */
	unsigned int size;          /* capacity of free list */
	struct rx_sw_desc *sdesc;   /* address of SW Rx descriptor ring */
	__be64 *desc;               /* address of HW Rx descriptor ring */
	dma_addr_t addr;            /* bus address of HW ring start */
	void __iomem *bar2_addr;    /* address of BAR2 Queue registers */
	unsigned int bar2_qid;      /* Queue ID for BAR2 Queue registers */
};

/* A packet gather list */
struct pkt_gl {
	u64 sgetstamp;		    /* SGE Time Stamp for Ingress Packet */
	struct page_frag frags[MAX_SKB_FRAGS];
	void *va;                         /* virtual address of first byte */
	unsigned int nfrags;              /* # of fragments */
	unsigned int tot_len;             /* total length of fragments */
};

typedef int (*rspq_handler_t)(struct sge_rspq *q, const __be64 *rsp,
			      const struct pkt_gl *gl);
typedef void (*rspq_flush_handler_t)(struct sge_rspq *q);
/* LRO related declarations for ULD */
struct t4_lro_mgr {
#define MAX_LRO_SESSIONS		64
	u8 lro_session_cnt;         /* # of sessions to aggregate */
	unsigned long lro_pkts;     /* # of LRO super packets */
	unsigned long lro_merged;   /* # of wire packets merged by LRO */
	struct sk_buff_head lroq;   /* list of aggregated sessions */
};

struct sge_rspq {                   /* state for an SGE response queue */
	struct napi_struct napi;
	const __be64 *cur_desc;     /* current descriptor in queue */
	unsigned int cidx;          /* consumer index */
	u8 gen;                     /* current generation bit */
	u8 intr_params;             /* interrupt holdoff parameters */
	u8 next_intr_params;        /* holdoff params for next interrupt */
	u8 adaptive_rx;
	u8 pktcnt_idx;              /* interrupt packet threshold */
	u8 uld;                     /* ULD handling this queue */
	u8 idx;                     /* queue index within its group */
	int offset;                 /* offset into current Rx buffer */
	u16 cntxt_id;               /* SGE context id for the response q */
	u16 abs_id;                 /* absolute SGE id for the response q */
	__be64 *desc;               /* address of HW response ring */
	dma_addr_t phys_addr;       /* physical address of the ring */
	void __iomem *bar2_addr;    /* address of BAR2 Queue registers */
	unsigned int bar2_qid;      /* Queue ID for BAR2 Queue registers */
	unsigned int iqe_len;       /* entry size */
	unsigned int size;          /* capacity of response queue */
	struct adapter *adap;
	struct net_device *netdev;  /* associated net device */
	rspq_handler_t handler;
	rspq_flush_handler_t flush_handler;
	struct t4_lro_mgr lro_mgr;
#ifdef CONFIG_NET_RX_BUSY_POLL
#define CXGB_POLL_STATE_IDLE		0
#define CXGB_POLL_STATE_NAPI		BIT(0) /* NAPI owns this poll */
#define CXGB_POLL_STATE_POLL		BIT(1) /* poll owns this poll */
#define CXGB_POLL_STATE_NAPI_YIELD	BIT(2) /* NAPI yielded this poll */
#define CXGB_POLL_STATE_POLL_YIELD	BIT(3) /* poll yielded this poll */
#define CXGB_POLL_YIELD			(CXGB_POLL_STATE_NAPI_YIELD |   \
					 CXGB_POLL_STATE_POLL_YIELD)
#define CXGB_POLL_LOCKED		(CXGB_POLL_STATE_NAPI |         \
					 CXGB_POLL_STATE_POLL)
#define CXGB_POLL_USER_PEND		(CXGB_POLL_STATE_POLL |         \
					 CXGB_POLL_STATE_POLL_YIELD)
	unsigned int bpoll_state;
	spinlock_t bpoll_lock;		/* lock for busy poll */
#endif /* CONFIG_NET_RX_BUSY_POLL */

};

struct sge_eth_stats {              /* Ethernet queue statistics */
	unsigned long pkts;         /* # of ethernet packets */
	unsigned long lro_pkts;     /* # of LRO super packets */
	unsigned long lro_merged;   /* # of wire packets merged by LRO */
	unsigned long rx_cso;       /* # of Rx checksum offloads */
	unsigned long vlan_ex;      /* # of Rx VLAN extractions */
	unsigned long rx_drops;     /* # of packets dropped due to no mem */
};

struct sge_eth_rxq {                /* SW Ethernet Rx queue */
	struct sge_rspq rspq;
	struct sge_fl fl;
	struct sge_eth_stats stats;
} ____cacheline_aligned_in_smp;

struct sge_ofld_stats {             /* offload queue statistics */
	unsigned long pkts;         /* # of packets */
	unsigned long imm;          /* # of immediate-data packets */
	unsigned long an;           /* # of asynchronous notifications */
	unsigned long nomem;        /* # of responses deferred due to no mem */
};

struct sge_ofld_rxq {               /* SW offload Rx queue */
	struct sge_rspq rspq;
	struct sge_fl fl;
	struct sge_ofld_stats stats;
} ____cacheline_aligned_in_smp;

struct tx_desc {
	__be64 flit[8];
};

struct tx_sw_desc;

struct sge_txq {
	unsigned int  in_use;       /* # of in-use Tx descriptors */
	unsigned int  size;         /* # of descriptors */
	unsigned int  cidx;         /* SW consumer index */
	unsigned int  pidx;         /* producer index */
	unsigned long stops;        /* # of times q has been stopped */
	unsigned long restarts;     /* # of queue restarts */
	unsigned int  cntxt_id;     /* SGE context id for the Tx q */
	struct tx_desc *desc;       /* address of HW Tx descriptor ring */
	struct tx_sw_desc *sdesc;   /* address of SW Tx descriptor ring */
	struct sge_qstat *stat;     /* queue status entry */
	dma_addr_t    phys_addr;    /* physical address of the ring */
	spinlock_t db_lock;
	int db_disabled;
	unsigned short db_pidx;
	unsigned short db_pidx_inc;
	void __iomem *bar2_addr;    /* address of BAR2 Queue registers */
	unsigned int bar2_qid;      /* Queue ID for BAR2 Queue registers */
};

struct sge_eth_txq {                /* state for an SGE Ethernet Tx queue */
	struct sge_txq q;
	struct netdev_queue *txq;   /* associated netdev TX queue */
#ifdef CONFIG_CHELSIO_T4_DCB
	u8 dcb_prio;		    /* DCB Priority bound to queue */
#endif
	unsigned long tso;          /* # of TSO requests */
	unsigned long tx_cso;       /* # of Tx checksum offloads */
	unsigned long vlan_ins;     /* # of Tx VLAN insertions */
	unsigned long mapping_err;  /* # of I/O MMU packet mapping errors */
} ____cacheline_aligned_in_smp;

struct sge_ofld_txq {               /* state for an SGE offload Tx queue */
	struct sge_txq q;
	struct adapter *adap;
	struct sk_buff_head sendq;  /* list of backpressured packets */
	struct tasklet_struct qresume_tsk; /* restarts the queue */
	bool service_ofldq_running; /* service_ofldq() is processing sendq */
	u8 full;                    /* the Tx ring is full */
	unsigned long mapping_err;  /* # of I/O MMU packet mapping errors */
} ____cacheline_aligned_in_smp;

struct sge_ctrl_txq {               /* state for an SGE control Tx queue */
	struct sge_txq q;
	struct adapter *adap;
	struct sk_buff_head sendq;  /* list of backpressured packets */
	struct tasklet_struct qresume_tsk; /* restarts the queue */
	u8 full;                    /* the Tx ring is full */
} ____cacheline_aligned_in_smp;

struct sge_uld_rxq_info {
	char name[IFNAMSIZ];	/* name of ULD driver */
	struct sge_ofld_rxq *uldrxq; /* Rxq's for ULD */
	u16 *msix_tbl;		/* msix_tbl for uld */
	u16 *rspq_id;		/* response queue id's of rxq */
	u16 nrxq;		/* # of ingress uld queues */
	u16 nciq;		/* # of completion queues */
	u8 uld;			/* uld type */
};

struct sge {
	struct sge_eth_txq ethtxq[MAX_ETH_QSETS];
	struct sge_ofld_txq ofldtxq[MAX_OFLD_QSETS];
	struct sge_ctrl_txq ctrlq[MAX_CTRL_QUEUES];

	struct sge_eth_rxq ethrxq[MAX_ETH_QSETS];
	struct sge_rspq fw_evtq ____cacheline_aligned_in_smp;
	struct sge_uld_rxq_info **uld_rxq_info;

	struct sge_rspq intrq ____cacheline_aligned_in_smp;
	spinlock_t intrq_lock;

	u16 max_ethqsets;           /* # of available Ethernet queue sets */
	u16 ethqsets;               /* # of active Ethernet queue sets */
	u16 ethtxq_rover;           /* Tx queue to clean up next */
	u16 ofldqsets;              /* # of active ofld queue sets */
	u16 nqs_per_uld;	    /* # of Rx queues per ULD */
	u16 timer_val[SGE_NTIMERS];
	u8 counter_val[SGE_NCOUNTERS];
	u32 fl_pg_order;            /* large page allocation size */
	u32 stat_len;               /* length of status page at ring end */
	u32 pktshift;               /* padding between CPL & packet data */
	u32 fl_align;               /* response queue message alignment */
	u32 fl_starve_thres;        /* Free List starvation threshold */

	struct sge_idma_monitor_state idma_monitor;
	unsigned int egr_start;
	unsigned int egr_sz;
	unsigned int ingr_start;
	unsigned int ingr_sz;
	void **egr_map;    /* qid->queue egress queue map */
	struct sge_rspq **ingr_map; /* qid->queue ingress queue map */
	unsigned long *starving_fl;
	unsigned long *txq_maperr;
	unsigned long *blocked_fl;
	struct timer_list rx_timer; /* refills starving FLs */
	struct timer_list tx_timer; /* checks Tx queues */
};

#define for_each_ethrxq(sge, i) for (i = 0; i < (sge)->ethqsets; i++)
#define for_each_ofldtxq(sge, i) for (i = 0; i < (sge)->ofldqsets; i++)

struct l2t_data;

#ifdef CONFIG_PCI_IOV

/* T4 supports SRIOV on PF0-3 and T5 on PF0-7.  However, the Serial
 * Configuration initialization for T5 only has SR-IOV functionality enabled
 * on PF0-3 in order to simplify everything.
 */
#define NUM_OF_PF_WITH_SRIOV 4

#endif

struct doorbell_stats {
	u32 db_drop;
	u32 db_empty;
	u32 db_full;
};

struct hash_mac_addr {
	struct list_head list;
	u8 addr[ETH_ALEN];
};

struct uld_msix_bmap {
	unsigned long *msix_bmap;
	unsigned int mapsize;
	spinlock_t lock; /* lock for acquiring bitmap */
};

struct uld_msix_info {
	unsigned short vec;
	char desc[IFNAMSIZ + 10];
	unsigned int idx;
};

struct vf_info {
	unsigned char vf_mac_addr[ETH_ALEN];
	bool pf_set_mac;
};

struct adapter {
	void __iomem *regs;
	void __iomem *bar2;
	u32 t4_bar0;
	struct pci_dev *pdev;
	struct device *pdev_dev;
	const char *name;
	unsigned int mbox;
	unsigned int pf;
	unsigned int flags;
	unsigned int adap_idx;
	enum chip_type chip;

	int msg_enable;

	struct adapter_params params;
	struct cxgb4_virt_res vres;
	unsigned int swintr;

	struct {
		unsigned short vec;
		char desc[IFNAMSIZ + 10];
	} msix_info[MAX_INGQ + 1];
	struct uld_msix_info *msix_info_ulds; /* msix info for uld's */
	struct uld_msix_bmap msix_bmap_ulds; /* msix bitmap for all uld */
	int msi_idx;

	struct doorbell_stats db_stats;
	struct sge sge;

	struct net_device *port[MAX_NPORTS];
	u8 chan_map[NCHAN];                   /* channel -> port map */

	struct vf_info *vfinfo;
	u8 num_vfs;

	u32 filter_mode;
	unsigned int l2t_start;
	unsigned int l2t_end;
	struct l2t_data *l2t;
	unsigned int clipt_start;
	unsigned int clipt_end;
	struct clip_tbl *clipt;
	struct cxgb4_uld_info *uld;
	void *uld_handle[CXGB4_ULD_MAX];
	unsigned int num_uld;
	unsigned int num_ofld_uld;
	struct list_head list_node;
	struct list_head rcu_node;
	struct list_head mac_hlist; /* list of MAC addresses in MPS Hash */

	void *iscsi_ppm;

	struct tid_info tids;
	void **tid_release_head;
	spinlock_t tid_release_lock;
	struct workqueue_struct *workq;
	struct work_struct tid_release_task;
	struct work_struct db_full_task;
	struct work_struct db_drop_task;
	bool tid_release_task_busy;

	/* support for mailbox command/reply logging */
#define T4_OS_LOG_MBOX_CMDS 256
	struct mbox_cmd_log *mbox_log;

	struct mutex uld_mutex;

	struct dentry *debugfs_root;
	bool use_bd;     /* Use SGE Back Door intfc for reading SGE Contexts */
	bool trace_rss;	/* 1 implies that different RSS flit per filter is
			 * used per filter else if 0 default RSS flit is
			 * used for all 4 filters.
			 */

	spinlock_t stats_lock;
	spinlock_t win0_lock ____cacheline_aligned_in_smp;

	/* TC u32 offload */
	struct cxgb4_tc_u32_table *tc_u32;
};

/* Support for "sched-class" command to allow a TX Scheduling Class to be
 * programmed with various parameters.
 */
struct ch_sched_params {
	s8   type;                     /* packet or flow */
	union {
		struct {
			s8   level;    /* scheduler hierarchy level */
			s8   mode;     /* per-class or per-flow */
			s8   rateunit; /* bit or packet rate */
			s8   ratemode; /* %port relative or kbps absolute */
			s8   channel;  /* scheduler channel [0..N] */
			s8   class;    /* scheduler class [0..N] */
			s32  minrate;  /* minimum rate */
			s32  maxrate;  /* maximum rate */
			s16  weight;   /* percent weight */
			s16  pktsize;  /* average packet size */
		} params;
	} u;
};

enum {
	SCHED_CLASS_TYPE_PACKET = 0,    /* class type */
};

enum {
	SCHED_CLASS_LEVEL_CL_RL = 0,    /* class rate limiter */
};

enum {
	SCHED_CLASS_MODE_CLASS = 0,     /* per-class scheduling */
};

enum {
	SCHED_CLASS_RATEUNIT_BITS = 0,  /* bit rate scheduling */
};

enum {
	SCHED_CLASS_RATEMODE_ABS = 1,   /* Kb/s */
};

/* Support for "sched_queue" command to allow one or more NIC TX Queues
 * to be bound to a TX Scheduling Class.
 */
struct ch_sched_queue {
	s8   queue;    /* queue index */
	s8   class;    /* class index */
};

/* Defined bit width of user definable filter tuples
 */
#define ETHTYPE_BITWIDTH 16
#define FRAG_BITWIDTH 1
#define MACIDX_BITWIDTH 9
#define FCOE_BITWIDTH 1
#define IPORT_BITWIDTH 3
#define MATCHTYPE_BITWIDTH 3
#define PROTO_BITWIDTH 8
#define TOS_BITWIDTH 8
#define PF_BITWIDTH 8
#define VF_BITWIDTH 8
#define IVLAN_BITWIDTH 16
#define OVLAN_BITWIDTH 16

/* Filter matching rules.  These consist of a set of ingress packet field
 * (value, mask) tuples.  The associated ingress packet field matches the
 * tuple when ((field & mask) == value).  (Thus a wildcard "don't care" field
 * rule can be constructed by specifying a tuple of (0, 0).)  A filter rule
 * matches an ingress packet when all of the individual individual field
 * matching rules are true.
 *
 * Partial field masks are always valid, however, while it may be easy to
 * understand their meanings for some fields (e.g. IP address to match a
 * subnet), for others making sensible partial masks is less intuitive (e.g.
 * MPS match type) ...
 *
 * Most of the following data structures are modeled on T4 capabilities.
 * Drivers for earlier chips use the subsets which make sense for those chips.
 * We really need to come up with a hardware-independent mechanism to
 * represent hardware filter capabilities ...
 */
struct ch_filter_tuple {
	/* Compressed header matching field rules.  The TP_VLAN_PRI_MAP
	 * register selects which of these fields will participate in the
	 * filter match rules -- up to a maximum of 36 bits.  Because
	 * TP_VLAN_PRI_MAP is a global register, all filters must use the same
	 * set of fields.
	 */
	uint32_t ethtype:ETHTYPE_BITWIDTH;      /* Ethernet type */
	uint32_t frag:FRAG_BITWIDTH;            /* IP fragmentation header */
	uint32_t ivlan_vld:1;                   /* inner VLAN valid */
	uint32_t ovlan_vld:1;                   /* outer VLAN valid */
	uint32_t pfvf_vld:1;                    /* PF/VF valid */
	uint32_t macidx:MACIDX_BITWIDTH;        /* exact match MAC index */
	uint32_t fcoe:FCOE_BITWIDTH;            /* FCoE packet */
	uint32_t iport:IPORT_BITWIDTH;          /* ingress port */
	uint32_t matchtype:MATCHTYPE_BITWIDTH;  /* MPS match type */
	uint32_t proto:PROTO_BITWIDTH;          /* protocol type */
	uint32_t tos:TOS_BITWIDTH;              /* TOS/Traffic Type */
	uint32_t pf:PF_BITWIDTH;                /* PCI-E PF ID */
	uint32_t vf:VF_BITWIDTH;                /* PCI-E VF ID */
	uint32_t ivlan:IVLAN_BITWIDTH;          /* inner VLAN */
	uint32_t ovlan:OVLAN_BITWIDTH;          /* outer VLAN */

	/* Uncompressed header matching field rules.  These are always
	 * available for field rules.
	 */
	uint8_t lip[16];        /* local IP address (IPv4 in [3:0]) */
	uint8_t fip[16];        /* foreign IP address (IPv4 in [3:0]) */
	uint16_t lport;         /* local port */
	uint16_t fport;         /* foreign port */
};

/* A filter ioctl command.
 */
struct ch_filter_specification {
	/* Administrative fields for filter.
	 */
	uint32_t hitcnts:1;     /* count filter hits in TCB */
	uint32_t prio:1;        /* filter has priority over active/server */

	/* Fundamental filter typing.  This is the one element of filter
	 * matching that doesn't exist as a (value, mask) tuple.
	 */
	uint32_t type:1;        /* 0 => IPv4, 1 => IPv6 */

	/* Packet dispatch information.  Ingress packets which match the
	 * filter rules will be dropped, passed to the host or switched back
	 * out as egress packets.
	 */
	uint32_t action:2;      /* drop, pass, switch */

	uint32_t rpttid:1;      /* report TID in RSS hash field */

	uint32_t dirsteer:1;    /* 0 => RSS, 1 => steer to iq */
	uint32_t iq:10;         /* ingress queue */

	uint32_t maskhash:1;    /* dirsteer=0: store RSS hash in TCB */
	uint32_t dirsteerhash:1;/* dirsteer=1: 0 => TCB contains RSS hash */
				/*             1 => TCB contains IQ ID */

	/* Switch proxy/rewrite fields.  An ingress packet which matches a
	 * filter with "switch" set will be looped back out as an egress
	 * packet -- potentially with some Ethernet header rewriting.
	 */
	uint32_t eport:2;       /* egress port to switch packet out */
	uint32_t newdmac:1;     /* rewrite destination MAC address */
	uint32_t newsmac:1;     /* rewrite source MAC address */
	uint32_t newvlan:2;     /* rewrite VLAN Tag */
	uint8_t dmac[ETH_ALEN]; /* new destination MAC address */
	uint8_t smac[ETH_ALEN]; /* new source MAC address */
	uint16_t vlan;          /* VLAN Tag to insert */

	/* Filter rule value/mask pairs.
	 */
	struct ch_filter_tuple val;
	struct ch_filter_tuple mask;
};

enum {
	FILTER_PASS = 0,        /* default */
	FILTER_DROP,
	FILTER_SWITCH
};

enum {
	VLAN_NOCHANGE = 0,      /* default */
	VLAN_REMOVE,
	VLAN_INSERT,
	VLAN_REWRITE
};

/* Host shadow copy of ingress filter entry.  This is in host native format
 * and doesn't match the ordering or bit order, etc. of the hardware of the
 * firmware command.  The use of bit-field structure elements is purely to
 * remind ourselves of the field size limitations and save memory in the case
 * where the filter table is large.
 */
struct filter_entry {
	/* Administrative fields for filter. */
	u32 valid:1;            /* filter allocated and valid */
	u32 locked:1;           /* filter is administratively locked */

	u32 pending:1;          /* filter action is pending firmware reply */
	u32 smtidx:8;           /* Source MAC Table index for smac */
	struct filter_ctx *ctx; /* Caller's completion hook */
	struct l2t_entry *l2t;  /* Layer Two Table entry for dmac */
	struct net_device *dev; /* Associated net device */
	u32 tid;                /* This will store the actual tid */

	/* The filter itself.  Most of this is a straight copy of information
	 * provided by the extended ioctl().  Some fields are translated to
	 * internal forms -- for instance the Ingress Queue ID passed in from
	 * the ioctl() is translated into the Absolute Ingress Queue ID.
	 */
	struct ch_filter_specification fs;
};

static inline int is_offload(const struct adapter *adap)
{
	return adap->params.offload;
}

static inline int is_pci_uld(const struct adapter *adap)
{
	return adap->params.crypto;
}

static inline int is_uld(const struct adapter *adap)
{
	return (adap->params.offload || adap->params.crypto);
}

static inline u32 t4_read_reg(struct adapter *adap, u32 reg_addr)
{
	return readl(adap->regs + reg_addr);
}

static inline void t4_write_reg(struct adapter *adap, u32 reg_addr, u32 val)
{
	writel(val, adap->regs + reg_addr);
}

#ifndef readq
static inline u64 readq(const volatile void __iomem *addr)
{
	return readl(addr) + ((u64)readl(addr + 4) << 32);
}

static inline void writeq(u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr + 4);
}
#endif

static inline u64 t4_read_reg64(struct adapter *adap, u32 reg_addr)
{
	return readq(adap->regs + reg_addr);
}

static inline void t4_write_reg64(struct adapter *adap, u32 reg_addr, u64 val)
{
	writeq(val, adap->regs + reg_addr);
}

/**
 * t4_set_hw_addr - store a port's MAC address in SW
 * @adapter: the adapter
 * @port_idx: the port index
 * @hw_addr: the Ethernet address
 *
 * Store the Ethernet address of the given port in SW.  Called by the common
 * code when it retrieves a port's Ethernet address from EEPROM.
 */
static inline void t4_set_hw_addr(struct adapter *adapter, int port_idx,
				  u8 hw_addr[])
{
	ether_addr_copy(adapter->port[port_idx]->dev_addr, hw_addr);
	ether_addr_copy(adapter->port[port_idx]->perm_addr, hw_addr);
}

/**
 * netdev2pinfo - return the port_info structure associated with a net_device
 * @dev: the netdev
 *
 * Return the struct port_info associated with a net_device
 */
static inline struct port_info *netdev2pinfo(const struct net_device *dev)
{
	return netdev_priv(dev);
}

/**
 * adap2pinfo - return the port_info of a port
 * @adap: the adapter
 * @idx: the port index
 *
 * Return the port_info structure for the port of the given index.
 */
static inline struct port_info *adap2pinfo(struct adapter *adap, int idx)
{
	return netdev_priv(adap->port[idx]);
}

/**
 * netdev2adap - return the adapter structure associated with a net_device
 * @dev: the netdev
 *
 * Return the struct adapter associated with a net_device
 */
static inline struct adapter *netdev2adap(const struct net_device *dev)
{
	return netdev2pinfo(dev)->adapter;
}

#ifdef CONFIG_NET_RX_BUSY_POLL
static inline void cxgb_busy_poll_init_lock(struct sge_rspq *q)
{
	spin_lock_init(&q->bpoll_lock);
	q->bpoll_state = CXGB_POLL_STATE_IDLE;
}

static inline bool cxgb_poll_lock_napi(struct sge_rspq *q)
{
	bool rc = true;

	spin_lock(&q->bpoll_lock);
	if (q->bpoll_state & CXGB_POLL_LOCKED) {
		q->bpoll_state |= CXGB_POLL_STATE_NAPI_YIELD;
		rc = false;
	} else {
		q->bpoll_state = CXGB_POLL_STATE_NAPI;
	}
	spin_unlock(&q->bpoll_lock);
	return rc;
}

static inline bool cxgb_poll_unlock_napi(struct sge_rspq *q)
{
	bool rc = false;

	spin_lock(&q->bpoll_lock);
	if (q->bpoll_state & CXGB_POLL_STATE_POLL_YIELD)
		rc = true;
	q->bpoll_state = CXGB_POLL_STATE_IDLE;
	spin_unlock(&q->bpoll_lock);
	return rc;
}

static inline bool cxgb_poll_lock_poll(struct sge_rspq *q)
{
	bool rc = true;

	spin_lock_bh(&q->bpoll_lock);
	if (q->bpoll_state & CXGB_POLL_LOCKED) {
		q->bpoll_state |= CXGB_POLL_STATE_POLL_YIELD;
		rc = false;
	} else {
		q->bpoll_state |= CXGB_POLL_STATE_POLL;
	}
	spin_unlock_bh(&q->bpoll_lock);
	return rc;
}

static inline bool cxgb_poll_unlock_poll(struct sge_rspq *q)
{
	bool rc = false;

	spin_lock_bh(&q->bpoll_lock);
	if (q->bpoll_state & CXGB_POLL_STATE_POLL_YIELD)
		rc = true;
	q->bpoll_state = CXGB_POLL_STATE_IDLE;
	spin_unlock_bh(&q->bpoll_lock);
	return rc;
}

static inline bool cxgb_poll_busy_polling(struct sge_rspq *q)
{
	return q->bpoll_state & CXGB_POLL_USER_PEND;
}
#else
static inline void cxgb_busy_poll_init_lock(struct sge_rspq *q)
{
}

static inline bool cxgb_poll_lock_napi(struct sge_rspq *q)
{
	return true;
}

static inline bool cxgb_poll_unlock_napi(struct sge_rspq *q)
{
	return false;
}

static inline bool cxgb_poll_lock_poll(struct sge_rspq *q)
{
	return false;
}

static inline bool cxgb_poll_unlock_poll(struct sge_rspq *q)
{
	return false;
}

static inline bool cxgb_poll_busy_polling(struct sge_rspq *q)
{
	return false;
}
#endif /* CONFIG_NET_RX_BUSY_POLL */

/* Return a version number to identify the type of adapter.  The scheme is:
 * - bits 0..9: chip version
 * - bits 10..15: chip revision
 * - bits 16..23: register dump version
 */
static inline unsigned int mk_adap_vers(struct adapter *ap)
{
	return CHELSIO_CHIP_VERSION(ap->params.chip) |
		(CHELSIO_CHIP_RELEASE(ap->params.chip) << 10) | (1 << 16);
}

/* Return a queue's interrupt hold-off time in us.  0 means no timer. */
static inline unsigned int qtimer_val(const struct adapter *adap,
				      const struct sge_rspq *q)
{
	unsigned int idx = q->intr_params >> 1;

	return idx < SGE_NTIMERS ? adap->sge.timer_val[idx] : 0;
}

/* driver version & name used for ethtool_drvinfo */
extern char cxgb4_driver_name[];
extern const char cxgb4_driver_version[];

void t4_os_portmod_changed(const struct adapter *adap, int port_id);
void t4_os_link_changed(struct adapter *adap, int port_id, int link_stat);

void *t4_alloc_mem(size_t size);

void t4_free_sge_resources(struct adapter *adap);
void t4_free_ofld_rxqs(struct adapter *adap, int n, struct sge_ofld_rxq *q);
irq_handler_t t4_intr_handler(struct adapter *adap);
netdev_tx_t t4_eth_xmit(struct sk_buff *skb, struct net_device *dev);
int t4_ethrx_handler(struct sge_rspq *q, const __be64 *rsp,
		     const struct pkt_gl *gl);
int t4_mgmt_tx(struct adapter *adap, struct sk_buff *skb);
int t4_ofld_send(struct adapter *adap, struct sk_buff *skb);
int t4_sge_alloc_rxq(struct adapter *adap, struct sge_rspq *iq, bool fwevtq,
		     struct net_device *dev, int intr_idx,
		     struct sge_fl *fl, rspq_handler_t hnd,
		     rspq_flush_handler_t flush_handler, int cong);
int t4_sge_alloc_eth_txq(struct adapter *adap, struct sge_eth_txq *txq,
			 struct net_device *dev, struct netdev_queue *netdevq,
			 unsigned int iqid);
int t4_sge_alloc_ctrl_txq(struct adapter *adap, struct sge_ctrl_txq *txq,
			  struct net_device *dev, unsigned int iqid,
			  unsigned int cmplqid);
int t4_sge_mod_ctrl_txq(struct adapter *adap, unsigned int eqid,
			unsigned int cmplqid);
int t4_sge_alloc_ofld_txq(struct adapter *adap, struct sge_ofld_txq *txq,
			  struct net_device *dev, unsigned int iqid);
irqreturn_t t4_sge_intr_msix(int irq, void *cookie);
int t4_sge_init(struct adapter *adap);
void t4_sge_start(struct adapter *adap);
void t4_sge_stop(struct adapter *adap);
int cxgb_busy_poll(struct napi_struct *napi);
void cxgb4_set_ethtool_ops(struct net_device *netdev);
int cxgb4_write_rss(const struct port_info *pi, const u16 *queues);
extern int dbfifo_int_thresh;

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; ++iter)

static inline int is_bypass(struct adapter *adap)
{
	return adap->params.bypass;
}

static inline int is_bypass_device(int device)
{
	/* this should be set based upon device capabilities */
	switch (device) {
	case 0x440b:
	case 0x440c:
		return 1;
	default:
		return 0;
	}
}

static inline int is_10gbt_device(int device)
{
	/* this should be set based upon device capabilities */
	switch (device) {
	case 0x4409:
	case 0x4486:
		return 1;

	default:
		return 0;
	}
}

static inline unsigned int core_ticks_per_usec(const struct adapter *adap)
{
	return adap->params.vpd.cclk / 1000;
}

static inline unsigned int us_to_core_ticks(const struct adapter *adap,
					    unsigned int us)
{
	return (us * adap->params.vpd.cclk) / 1000;
}

static inline unsigned int core_ticks_to_us(const struct adapter *adapter,
					    unsigned int ticks)
{
	/* add Core Clock / 2 to round ticks to nearest uS */
	return ((ticks * 1000 + adapter->params.vpd.cclk/2) /
		adapter->params.vpd.cclk);
}

void t4_set_reg_field(struct adapter *adap, unsigned int addr, u32 mask,
		      u32 val);

int t4_wr_mbox_meat_timeout(struct adapter *adap, int mbox, const void *cmd,
			    int size, void *rpl, bool sleep_ok, int timeout);
int t4_wr_mbox_meat(struct adapter *adap, int mbox, const void *cmd, int size,
		    void *rpl, bool sleep_ok);

static inline int t4_wr_mbox_timeout(struct adapter *adap, int mbox,
				     const void *cmd, int size, void *rpl,
				     int timeout)
{
	return t4_wr_mbox_meat_timeout(adap, mbox, cmd, size, rpl, true,
				       timeout);
}

static inline int t4_wr_mbox(struct adapter *adap, int mbox, const void *cmd,
			     int size, void *rpl)
{
	return t4_wr_mbox_meat(adap, mbox, cmd, size, rpl, true);
}

static inline int t4_wr_mbox_ns(struct adapter *adap, int mbox, const void *cmd,
				int size, void *rpl)
{
	return t4_wr_mbox_meat(adap, mbox, cmd, size, rpl, false);
}

/**
 *	hash_mac_addr - return the hash value of a MAC address
 *	@addr: the 48-bit Ethernet MAC address
 *
 *	Hashes a MAC address according to the hash function used by HW inexact
 *	(hash) address matching.
 */
static inline int hash_mac_addr(const u8 *addr)
{
	u32 a = ((u32)addr[0] << 16) | ((u32)addr[1] << 8) | addr[2];
	u32 b = ((u32)addr[3] << 16) | ((u32)addr[4] << 8) | addr[5];

	a ^= b;
	a ^= (a >> 12);
	a ^= (a >> 6);
	return a & 0x3f;
}

int cxgb4_set_rspq_intr_params(struct sge_rspq *q, unsigned int us,
			       unsigned int cnt);
static inline void init_rspq(struct adapter *adap, struct sge_rspq *q,
			     unsigned int us, unsigned int cnt,
			     unsigned int size, unsigned int iqe_size)
{
	q->adap = adap;
	cxgb4_set_rspq_intr_params(q, us, cnt);
	q->iqe_len = iqe_size;
	q->size = size;
}

void t4_write_indirect(struct adapter *adap, unsigned int addr_reg,
		       unsigned int data_reg, const u32 *vals,
		       unsigned int nregs, unsigned int start_idx);
void t4_read_indirect(struct adapter *adap, unsigned int addr_reg,
		      unsigned int data_reg, u32 *vals, unsigned int nregs,
		      unsigned int start_idx);
void t4_hw_pci_read_cfg4(struct adapter *adapter, int reg, u32 *val);

struct fw_filter_wr;

void t4_intr_enable(struct adapter *adapter);
void t4_intr_disable(struct adapter *adapter);
int t4_slow_intr_handler(struct adapter *adapter);

int t4_wait_dev_ready(void __iomem *regs);
int t4_link_l1cfg(struct adapter *adap, unsigned int mbox, unsigned int port,
		  struct link_config *lc);
int t4_restart_aneg(struct adapter *adap, unsigned int mbox, unsigned int port);

u32 t4_read_pcie_cfg4(struct adapter *adap, int reg);
u32 t4_get_util_window(struct adapter *adap);
void t4_setup_memwin(struct adapter *adap, u32 memwin_base, u32 window);

#define T4_MEMORY_WRITE	0
#define T4_MEMORY_READ	1
int t4_memory_rw(struct adapter *adap, int win, int mtype, u32 addr, u32 len,
		 void *buf, int dir);
static inline int t4_memory_write(struct adapter *adap, int mtype, u32 addr,
				  u32 len, __be32 *buf)
{
	return t4_memory_rw(adap, 0, mtype, addr, len, buf, 0);
}

unsigned int t4_get_regs_len(struct adapter *adapter);
void t4_get_regs(struct adapter *adap, void *buf, size_t buf_size);

int t4_seeprom_wp(struct adapter *adapter, bool enable);
int t4_get_raw_vpd_params(struct adapter *adapter, struct vpd_params *p);
int t4_get_vpd_params(struct adapter *adapter, struct vpd_params *p);
int t4_read_flash(struct adapter *adapter, unsigned int addr,
		  unsigned int nwords, u32 *data, int byte_oriented);
int t4_load_fw(struct adapter *adapter, const u8 *fw_data, unsigned int size);
int t4_load_phy_fw(struct adapter *adap,
		   int win, spinlock_t *lock,
		   int (*phy_fw_version)(const u8 *, size_t),
		   const u8 *phy_fw_data, size_t phy_fw_size);
int t4_phy_fw_ver(struct adapter *adap, int *phy_fw_ver);
int t4_fwcache(struct adapter *adap, enum fw_params_param_dev_fwcache op);
int t4_fw_upgrade(struct adapter *adap, unsigned int mbox,
		  const u8 *fw_data, unsigned int size, int force);
int t4_fl_pkt_align(struct adapter *adap);
unsigned int t4_flash_cfg_addr(struct adapter *adapter);
int t4_check_fw_version(struct adapter *adap);
int t4_get_fw_version(struct adapter *adapter, u32 *vers);
int t4_get_bs_version(struct adapter *adapter, u32 *vers);
int t4_get_tp_version(struct adapter *adapter, u32 *vers);
int t4_get_exprom_version(struct adapter *adapter, u32 *vers);
int t4_prep_fw(struct adapter *adap, struct fw_info *fw_info,
	       const u8 *fw_data, unsigned int fw_size,
	       struct fw_hdr *card_fw, enum dev_state state, int *reset);
int t4_prep_adapter(struct adapter *adapter);

enum t4_bar2_qtype { T4_BAR2_QTYPE_EGRESS, T4_BAR2_QTYPE_INGRESS };
int t4_bar2_sge_qregs(struct adapter *adapter,
		      unsigned int qid,
		      enum t4_bar2_qtype qtype,
		      int user,
		      u64 *pbar2_qoffset,
		      unsigned int *pbar2_qid);

unsigned int qtimer_val(const struct adapter *adap,
			const struct sge_rspq *q);

int t4_init_devlog_params(struct adapter *adapter);
int t4_init_sge_params(struct adapter *adapter);
int t4_init_tp_params(struct adapter *adap);
int t4_filter_field_shift(const struct adapter *adap, int filter_sel);
int t4_init_rss_mode(struct adapter *adap, int mbox);
int t4_init_portinfo(struct port_info *pi, int mbox,
		     int port, int pf, int vf, u8 mac[]);
int t4_port_init(struct adapter *adap, int mbox, int pf, int vf);
void t4_fatal_err(struct adapter *adapter);
int t4_config_rss_range(struct adapter *adapter, int mbox, unsigned int viid,
			int start, int n, const u16 *rspq, unsigned int nrspq);
int t4_config_glbl_rss(struct adapter *adapter, int mbox, unsigned int mode,
		       unsigned int flags);
int t4_config_vi_rss(struct adapter *adapter, int mbox, unsigned int viid,
		     unsigned int flags, unsigned int defq);
int t4_read_rss(struct adapter *adapter, u16 *entries);
void t4_read_rss_key(struct adapter *adapter, u32 *key);
void t4_write_rss_key(struct adapter *adap, const u32 *key, int idx);
void t4_read_rss_pf_config(struct adapter *adapter, unsigned int index,
			   u32 *valp);
void t4_read_rss_vf_config(struct adapter *adapter, unsigned int index,
			   u32 *vfl, u32 *vfh);
u32 t4_read_rss_pf_map(struct adapter *adapter);
u32 t4_read_rss_pf_mask(struct adapter *adapter);

unsigned int t4_get_mps_bg_map(struct adapter *adapter, int idx);
void t4_pmtx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[]);
void t4_pmrx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[]);
int t4_read_cim_ibq(struct adapter *adap, unsigned int qid, u32 *data,
		    size_t n);
int t4_read_cim_obq(struct adapter *adap, unsigned int qid, u32 *data,
		    size_t n);
int t4_cim_read(struct adapter *adap, unsigned int addr, unsigned int n,
		unsigned int *valp);
int t4_cim_write(struct adapter *adap, unsigned int addr, unsigned int n,
		 const unsigned int *valp);
int t4_cim_read_la(struct adapter *adap, u32 *la_buf, unsigned int *wrptr);
void t4_cim_read_pif_la(struct adapter *adap, u32 *pif_req, u32 *pif_rsp,
			unsigned int *pif_req_wrptr,
			unsigned int *pif_rsp_wrptr);
void t4_cim_read_ma_la(struct adapter *adap, u32 *ma_req, u32 *ma_rsp);
void t4_read_cimq_cfg(struct adapter *adap, u16 *base, u16 *size, u16 *thres);
const char *t4_get_port_type_description(enum fw_port_type port_type);
void t4_get_port_stats(struct adapter *adap, int idx, struct port_stats *p);
void t4_get_port_stats_offset(struct adapter *adap, int idx,
			      struct port_stats *stats,
			      struct port_stats *offset);
void t4_get_lb_stats(struct adapter *adap, int idx, struct lb_port_stats *p);
void t4_read_mtu_tbl(struct adapter *adap, u16 *mtus, u8 *mtu_log);
void t4_read_cong_tbl(struct adapter *adap, u16 incr[NMTUS][NCCTRL_WIN]);
void t4_tp_wr_bits_indirect(struct adapter *adap, unsigned int addr,
			    unsigned int mask, unsigned int val);
void t4_tp_read_la(struct adapter *adap, u64 *la_buf, unsigned int *wrptr);
void t4_tp_get_err_stats(struct adapter *adap, struct tp_err_stats *st);
void t4_tp_get_cpl_stats(struct adapter *adap, struct tp_cpl_stats *st);
void t4_tp_get_rdma_stats(struct adapter *adap, struct tp_rdma_stats *st);
void t4_get_usm_stats(struct adapter *adap, struct tp_usm_stats *st);
void t4_tp_get_tcp_stats(struct adapter *adap, struct tp_tcp_stats *v4,
			 struct tp_tcp_stats *v6);
void t4_get_fcoe_stats(struct adapter *adap, unsigned int idx,
		       struct tp_fcoe_stats *st);
void t4_load_mtus(struct adapter *adap, const unsigned short *mtus,
		  const unsigned short *alpha, const unsigned short *beta);

void t4_ulprx_read_la(struct adapter *adap, u32 *la_buf);

void t4_get_chan_txrate(struct adapter *adap, u64 *nic_rate, u64 *ofld_rate);
void t4_mk_filtdelwr(unsigned int ftid, struct fw_filter_wr *wr, int qid);

void t4_wol_magic_enable(struct adapter *adap, unsigned int port,
			 const u8 *addr);
int t4_wol_pat_enable(struct adapter *adap, unsigned int port, unsigned int map,
		      u64 mask0, u64 mask1, unsigned int crc, bool enable);

int t4_fw_hello(struct adapter *adap, unsigned int mbox, unsigned int evt_mbox,
		enum dev_master master, enum dev_state *state);
int t4_fw_bye(struct adapter *adap, unsigned int mbox);
int t4_early_init(struct adapter *adap, unsigned int mbox);
int t4_fw_reset(struct adapter *adap, unsigned int mbox, int reset);
int t4_fixup_host_params(struct adapter *adap, unsigned int page_size,
			  unsigned int cache_line_size);
int t4_fw_initialize(struct adapter *adap, unsigned int mbox);
int t4_query_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int nparams, const u32 *params,
		    u32 *val);
int t4_query_params_rw(struct adapter *adap, unsigned int mbox, unsigned int pf,
		       unsigned int vf, unsigned int nparams, const u32 *params,
		       u32 *val, int rw);
int t4_set_params_timeout(struct adapter *adap, unsigned int mbox,
			  unsigned int pf, unsigned int vf,
			  unsigned int nparams, const u32 *params,
			  const u32 *val, int timeout);
int t4_set_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		  unsigned int vf, unsigned int nparams, const u32 *params,
		  const u32 *val);
int t4_cfg_pfvf(struct adapter *adap, unsigned int mbox, unsigned int pf,
		unsigned int vf, unsigned int txq, unsigned int txq_eth_ctrl,
		unsigned int rxqi, unsigned int rxq, unsigned int tc,
		unsigned int vi, unsigned int cmask, unsigned int pmask,
		unsigned int nexact, unsigned int rcaps, unsigned int wxcaps);
int t4_alloc_vi(struct adapter *adap, unsigned int mbox, unsigned int port,
		unsigned int pf, unsigned int vf, unsigned int nmac, u8 *mac,
		unsigned int *rss_size);
int t4_free_vi(struct adapter *adap, unsigned int mbox,
	       unsigned int pf, unsigned int vf,
	       unsigned int viid);
int t4_set_rxmode(struct adapter *adap, unsigned int mbox, unsigned int viid,
		int mtu, int promisc, int all_multi, int bcast, int vlanex,
		bool sleep_ok);
int t4_alloc_mac_filt(struct adapter *adap, unsigned int mbox,
		      unsigned int viid, bool free, unsigned int naddr,
		      const u8 **addr, u16 *idx, u64 *hash, bool sleep_ok);
int t4_free_mac_filt(struct adapter *adap, unsigned int mbox,
		     unsigned int viid, unsigned int naddr,
		     const u8 **addr, bool sleep_ok);
int t4_change_mac(struct adapter *adap, unsigned int mbox, unsigned int viid,
		  int idx, const u8 *addr, bool persist, bool add_smt);
int t4_set_addr_hash(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     bool ucast, u64 vec, bool sleep_ok);
int t4_enable_vi_params(struct adapter *adap, unsigned int mbox,
			unsigned int viid, bool rx_en, bool tx_en, bool dcb_en);
int t4_enable_vi(struct adapter *adap, unsigned int mbox, unsigned int viid,
		 bool rx_en, bool tx_en);
int t4_identify_port(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     unsigned int nblinks);
int t4_mdio_rd(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, u16 *valp);
int t4_mdio_wr(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, u16 val);
int t4_iq_stop(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int iqtype, unsigned int iqid,
	       unsigned int fl0id, unsigned int fl1id);
int t4_iq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int iqtype, unsigned int iqid,
	       unsigned int fl0id, unsigned int fl1id);
int t4_eth_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		   unsigned int vf, unsigned int eqid);
int t4_ctrl_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid);
int t4_ofld_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid);
int t4_sge_ctxt_flush(struct adapter *adap, unsigned int mbox);
void t4_handle_get_port_info(struct port_info *pi, const __be64 *rpl);
int t4_handle_fw_rpl(struct adapter *adap, const __be64 *rpl);
void t4_db_full(struct adapter *adapter);
void t4_db_dropped(struct adapter *adapter);
int t4_set_trace_filter(struct adapter *adapter, const struct trace_params *tp,
			int filter_index, int enable);
void t4_get_trace_filter(struct adapter *adapter, struct trace_params *tp,
			 int filter_index, int *enabled);
int t4_fwaddrspace_write(struct adapter *adap, unsigned int mbox,
			 u32 addr, u32 val);
int t4_sched_params(struct adapter *adapter, int type, int level, int mode,
		    int rateunit, int ratemode, int channel, int class,
		    int minrate, int maxrate, int weight, int pktsize);
void t4_sge_decode_idma_state(struct adapter *adapter, int state);
void t4_free_mem(void *addr);
void t4_idma_monitor_init(struct adapter *adapter,
			  struct sge_idma_monitor_state *idma);
void t4_idma_monitor(struct adapter *adapter,
		     struct sge_idma_monitor_state *idma,
		     int hz, int ticks);
int t4_set_vf_mac_acl(struct adapter *adapter, unsigned int vf,
		      unsigned int naddr, u8 *addr);
void t4_uld_mem_free(struct adapter *adap);
int t4_uld_mem_alloc(struct adapter *adap);
void t4_uld_clean_up(struct adapter *adap);
void t4_register_netevent_notifier(void);
void free_rspq_fl(struct adapter *adap, struct sge_rspq *rq, struct sge_fl *fl);
#endif /* __CXGB4_H__ */

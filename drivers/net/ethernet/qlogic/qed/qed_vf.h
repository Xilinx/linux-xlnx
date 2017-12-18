/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_VF_H
#define _QED_VF_H

#include "qed_l2.h"
#include "qed_mcp.h"

#define T_ETH_INDIRECTION_TABLE_SIZE 128
#define T_ETH_RSS_KEY_SIZE 10

struct vf_pf_resc_request {
	u8 num_rxqs;
	u8 num_txqs;
	u8 num_sbs;
	u8 num_mac_filters;
	u8 num_vlan_filters;
	u8 num_mc_filters;
	u16 padding;
};

struct hw_sb_info {
	u16 hw_sb_id;
	u8 sb_qid;
	u8 padding[5];
};

#define TLV_BUFFER_SIZE                 1024

enum {
	PFVF_STATUS_WAITING,
	PFVF_STATUS_SUCCESS,
	PFVF_STATUS_FAILURE,
	PFVF_STATUS_NOT_SUPPORTED,
	PFVF_STATUS_NO_RESOURCE,
	PFVF_STATUS_FORCED,
};

/* vf pf channel tlvs */
/* general tlv header (used for both vf->pf request and pf->vf response) */
struct channel_tlv {
	u16 type;
	u16 length;
};

/* header of first vf->pf tlv carries the offset used to calculate reponse
 * buffer address
 */
struct vfpf_first_tlv {
	struct channel_tlv tl;
	u32 padding;
	u64 reply_address;
};

/* header of pf->vf tlvs, carries the status of handling the request */
struct pfvf_tlv {
	struct channel_tlv tl;
	u8 status;
	u8 padding[3];
};

/* response tlv used for most tlvs */
struct pfvf_def_resp_tlv {
	struct pfvf_tlv hdr;
};

/* used to terminate and pad a tlv list */
struct channel_list_end_tlv {
	struct channel_tlv tl;
	u8 padding[4];
};

#define VFPF_ACQUIRE_OS_LINUX (0)
#define VFPF_ACQUIRE_OS_WINDOWS (1)
#define VFPF_ACQUIRE_OS_ESX (2)
#define VFPF_ACQUIRE_OS_SOLARIS (3)
#define VFPF_ACQUIRE_OS_LINUX_USERSPACE (4)

struct vfpf_acquire_tlv {
	struct vfpf_first_tlv first_tlv;

	struct vf_pf_vfdev_info {
#define VFPF_ACQUIRE_CAP_PRE_FP_HSI     (1 << 0) /* VF pre-FP hsi version */
#define VFPF_ACQUIRE_CAP_100G		(1 << 1) /* VF can support 100g */
		u64 capabilities;
		u8 fw_major;
		u8 fw_minor;
		u8 fw_revision;
		u8 fw_engineering;
		u32 driver_version;
		u16 opaque_fid;	/* ME register value */
		u8 os_type;	/* VFPF_ACQUIRE_OS_* value */
		u8 eth_fp_hsi_major;
		u8 eth_fp_hsi_minor;
		u8 padding[3];
	} vfdev_info;

	struct vf_pf_resc_request resc_request;

	u64 bulletin_addr;
	u32 bulletin_size;
	u32 padding;
};

/* receive side scaling tlv */
struct vfpf_vport_update_rss_tlv {
	struct channel_tlv tl;

	u8 update_rss_flags;
#define VFPF_UPDATE_RSS_CONFIG_FLAG       BIT(0)
#define VFPF_UPDATE_RSS_CAPS_FLAG         BIT(1)
#define VFPF_UPDATE_RSS_IND_TABLE_FLAG    BIT(2)
#define VFPF_UPDATE_RSS_KEY_FLAG          BIT(3)

	u8 rss_enable;
	u8 rss_caps;
	u8 rss_table_size_log;	/* The table size is 2 ^ rss_table_size_log */
	u16 rss_ind_table[T_ETH_INDIRECTION_TABLE_SIZE];
	u32 rss_key[T_ETH_RSS_KEY_SIZE];
};

struct pfvf_storm_stats {
	u32 address;
	u32 len;
};

struct pfvf_stats_info {
	struct pfvf_storm_stats mstats;
	struct pfvf_storm_stats pstats;
	struct pfvf_storm_stats tstats;
	struct pfvf_storm_stats ustats;
};

struct pfvf_acquire_resp_tlv {
	struct pfvf_tlv hdr;

	struct pf_vf_pfdev_info {
		u32 chip_num;
		u32 mfw_ver;

		u16 fw_major;
		u16 fw_minor;
		u16 fw_rev;
		u16 fw_eng;

		u64 capabilities;
#define PFVF_ACQUIRE_CAP_DEFAULT_UNTAGGED	BIT(0)
#define PFVF_ACQUIRE_CAP_100G			BIT(1)	/* If set, 100g PF */
/* There are old PF versions where the PF might mistakenly override the sanity
 * mechanism [version-based] and allow a VF that can't be supported to pass
 * the acquisition phase.
 * To overcome this, PFs now indicate that they're past that point and the new
 * VFs would fail probe on the older PFs that fail to do so.
 */
#define PFVF_ACQUIRE_CAP_POST_FW_OVERRIDE	BIT(2)

		u16 db_size;
		u8 indices_per_sb;
		u8 os_type;

		/* These should match the PF's qed_dev values */
		u16 chip_rev;
		u8 dev_type;

		u8 padding;

		struct pfvf_stats_info stats_info;

		u8 port_mac[ETH_ALEN];

		/* It's possible PF had to configure an older fastpath HSI
		 * [in case VF is newer than PF]. This is communicated back
		 * to the VF. It can also be used in case of error due to
		 * non-matching versions to shed light in VF about failure.
		 */
		u8 major_fp_hsi;
		u8 minor_fp_hsi;
	} pfdev_info;

	struct pf_vf_resc {
#define PFVF_MAX_QUEUES_PER_VF		16
#define PFVF_MAX_SBS_PER_VF		16
		struct hw_sb_info hw_sbs[PFVF_MAX_SBS_PER_VF];
		u8 hw_qid[PFVF_MAX_QUEUES_PER_VF];
		u8 cid[PFVF_MAX_QUEUES_PER_VF];

		u8 num_rxqs;
		u8 num_txqs;
		u8 num_sbs;
		u8 num_mac_filters;
		u8 num_vlan_filters;
		u8 num_mc_filters;
		u8 padding[2];
	} resc;

	u32 bulletin_size;
	u32 padding;
};

struct pfvf_start_queue_resp_tlv {
	struct pfvf_tlv hdr;
	u32 offset;		/* offset to consumer/producer of queue */
	u8 padding[4];
};

/* Setup Queue */
struct vfpf_start_rxq_tlv {
	struct vfpf_first_tlv first_tlv;

	/* physical addresses */
	u64 rxq_addr;
	u64 deprecated_sge_addr;
	u64 cqe_pbl_addr;

	u16 cqe_pbl_size;
	u16 hw_sb;
	u16 rx_qid;
	u16 hc_rate;		/* desired interrupts per sec. */

	u16 bd_max_bytes;
	u16 stat_id;
	u8 sb_index;
	u8 padding[3];
};

struct vfpf_start_txq_tlv {
	struct vfpf_first_tlv first_tlv;

	/* physical addresses */
	u64 pbl_addr;
	u16 pbl_size;
	u16 stat_id;
	u16 tx_qid;
	u16 hw_sb;

	u32 flags;		/* VFPF_QUEUE_FLG_X flags */
	u16 hc_rate;		/* desired interrupts per sec. */
	u8 sb_index;
	u8 padding[3];
};

/* Stop RX Queue */
struct vfpf_stop_rxqs_tlv {
	struct vfpf_first_tlv first_tlv;

	u16 rx_qid;
	u8 num_rxqs;
	u8 cqe_completion;
	u8 padding[4];
};

/* Stop TX Queues */
struct vfpf_stop_txqs_tlv {
	struct vfpf_first_tlv first_tlv;

	u16 tx_qid;
	u8 num_txqs;
	u8 padding[5];
};

struct vfpf_update_rxq_tlv {
	struct vfpf_first_tlv first_tlv;

	u64 deprecated_sge_addr[PFVF_MAX_QUEUES_PER_VF];

	u16 rx_qid;
	u8 num_rxqs;
	u8 flags;
#define VFPF_RXQ_UPD_INIT_SGE_DEPRECATE_FLAG    BIT(0)
#define VFPF_RXQ_UPD_COMPLETE_CQE_FLAG          BIT(1)
#define VFPF_RXQ_UPD_COMPLETE_EVENT_FLAG        BIT(2)

	u8 padding[4];
};

/* Set Queue Filters */
struct vfpf_q_mac_vlan_filter {
	u32 flags;
#define VFPF_Q_FILTER_DEST_MAC_VALID    0x01
#define VFPF_Q_FILTER_VLAN_TAG_VALID    0x02
#define VFPF_Q_FILTER_SET_MAC           0x100	/* set/clear */

	u8 mac[ETH_ALEN];
	u16 vlan_tag;

	u8 padding[4];
};

/* Start a vport */
struct vfpf_vport_start_tlv {
	struct vfpf_first_tlv first_tlv;

	u64 sb_addr[PFVF_MAX_SBS_PER_VF];

	u32 tpa_mode;
	u16 dep1;
	u16 mtu;

	u8 vport_id;
	u8 inner_vlan_removal;

	u8 only_untagged;
	u8 max_buffers_per_cqe;

	u8 padding[4];
};

/* Extended tlvs - need to add rss, mcast, accept mode tlvs */
struct vfpf_vport_update_activate_tlv {
	struct channel_tlv tl;
	u8 update_rx;
	u8 update_tx;
	u8 active_rx;
	u8 active_tx;
};

struct vfpf_vport_update_tx_switch_tlv {
	struct channel_tlv tl;
	u8 tx_switching;
	u8 padding[3];
};

struct vfpf_vport_update_vlan_strip_tlv {
	struct channel_tlv tl;
	u8 remove_vlan;
	u8 padding[3];
};

struct vfpf_vport_update_mcast_bin_tlv {
	struct channel_tlv tl;
	u8 padding[4];

	u64 bins[8];
};

struct vfpf_vport_update_accept_param_tlv {
	struct channel_tlv tl;
	u8 update_rx_mode;
	u8 update_tx_mode;
	u8 rx_accept_filter;
	u8 tx_accept_filter;
};

struct vfpf_vport_update_accept_any_vlan_tlv {
	struct channel_tlv tl;
	u8 update_accept_any_vlan_flg;
	u8 accept_any_vlan;

	u8 padding[2];
};

struct vfpf_vport_update_sge_tpa_tlv {
	struct channel_tlv tl;

	u16 sge_tpa_flags;
#define VFPF_TPA_IPV4_EN_FLAG		BIT(0)
#define VFPF_TPA_IPV6_EN_FLAG		BIT(1)
#define VFPF_TPA_PKT_SPLIT_FLAG		BIT(2)
#define VFPF_TPA_HDR_DATA_SPLIT_FLAG	BIT(3)
#define VFPF_TPA_GRO_CONSIST_FLAG	BIT(4)

	u8 update_sge_tpa_flags;
#define VFPF_UPDATE_SGE_DEPRECATED_FLAG	BIT(0)
#define VFPF_UPDATE_TPA_EN_FLAG		BIT(1)
#define VFPF_UPDATE_TPA_PARAM_FLAG	BIT(2)

	u8 max_buffers_per_cqe;

	u16 deprecated_sge_buff_size;
	u16 tpa_max_size;
	u16 tpa_min_size_to_start;
	u16 tpa_min_size_to_cont;

	u8 tpa_max_aggs_num;
	u8 padding[7];
};

/* Primary tlv as a header for various extended tlvs for
 * various functionalities in vport update ramrod.
 */
struct vfpf_vport_update_tlv {
	struct vfpf_first_tlv first_tlv;
};

struct vfpf_ucast_filter_tlv {
	struct vfpf_first_tlv first_tlv;

	u8 opcode;
	u8 type;

	u8 mac[ETH_ALEN];

	u16 vlan;
	u16 padding[3];
};

struct tlv_buffer_size {
	u8 tlv_buffer[TLV_BUFFER_SIZE];
};

union vfpf_tlvs {
	struct vfpf_first_tlv first_tlv;
	struct vfpf_acquire_tlv acquire;
	struct vfpf_start_rxq_tlv start_rxq;
	struct vfpf_start_txq_tlv start_txq;
	struct vfpf_stop_rxqs_tlv stop_rxqs;
	struct vfpf_stop_txqs_tlv stop_txqs;
	struct vfpf_update_rxq_tlv update_rxq;
	struct vfpf_vport_start_tlv start_vport;
	struct vfpf_vport_update_tlv vport_update;
	struct vfpf_ucast_filter_tlv ucast_filter;
	struct channel_list_end_tlv list_end;
	struct tlv_buffer_size tlv_buf_size;
};

union pfvf_tlvs {
	struct pfvf_def_resp_tlv default_resp;
	struct pfvf_acquire_resp_tlv acquire_resp;
	struct tlv_buffer_size tlv_buf_size;
	struct pfvf_start_queue_resp_tlv queue_start;
};

enum qed_bulletin_bit {
	/* Alert the VF that a forced MAC was set by the PF */
	MAC_ADDR_FORCED = 0,
	/* Alert the VF that a forced VLAN was set by the PF */
	VLAN_ADDR_FORCED = 2,

	/* Indicate that `default_only_untagged' contains actual data */
	VFPF_BULLETIN_UNTAGGED_DEFAULT = 3,
	VFPF_BULLETIN_UNTAGGED_DEFAULT_FORCED = 4,

	/* Alert the VF that suggested mac was sent by the PF.
	 * MAC_ADDR will be disabled in case MAC_ADDR_FORCED is set.
	 */
	VFPF_BULLETIN_MAC_ADDR = 5
};

struct qed_bulletin_content {
	/* crc of structure to ensure is not in mid-update */
	u32 crc;

	u32 version;

	/* bitmap indicating which fields hold valid values */
	u64 valid_bitmap;

	/* used for MAC_ADDR or MAC_ADDR_FORCED */
	u8 mac[ETH_ALEN];

	/* If valid, 1 => only untagged Rx if no vlan is configured */
	u8 default_only_untagged;
	u8 padding;

	/* The following is a 'copy' of qed_mcp_link_state,
	 * qed_mcp_link_params and qed_mcp_link_capabilities. Since it's
	 * possible the structs will increase further along the road we cannot
	 * have it here; Instead we need to have all of its fields.
	 */
	u8 req_autoneg;
	u8 req_autoneg_pause;
	u8 req_forced_rx;
	u8 req_forced_tx;
	u8 padding2[4];

	u32 req_adv_speed;
	u32 req_forced_speed;
	u32 req_loopback;
	u32 padding3;

	u8 link_up;
	u8 full_duplex;
	u8 autoneg;
	u8 autoneg_complete;
	u8 parallel_detection;
	u8 pfc_enabled;
	u8 partner_tx_flow_ctrl_en;
	u8 partner_rx_flow_ctrl_en;
	u8 partner_adv_pause;
	u8 sfp_tx_fault;
	u8 padding4[6];

	u32 speed;
	u32 partner_adv_speed;

	u32 capability_speed;

	/* Forced vlan */
	u16 pvid;
	u16 padding5;
};

struct qed_bulletin {
	dma_addr_t phys;
	struct qed_bulletin_content *p_virt;
	u32 size;
};

enum {
	CHANNEL_TLV_NONE,	/* ends tlv sequence */
	CHANNEL_TLV_ACQUIRE,
	CHANNEL_TLV_VPORT_START,
	CHANNEL_TLV_VPORT_UPDATE,
	CHANNEL_TLV_VPORT_TEARDOWN,
	CHANNEL_TLV_START_RXQ,
	CHANNEL_TLV_START_TXQ,
	CHANNEL_TLV_STOP_RXQS,
	CHANNEL_TLV_STOP_TXQS,
	CHANNEL_TLV_UPDATE_RXQ,
	CHANNEL_TLV_INT_CLEANUP,
	CHANNEL_TLV_CLOSE,
	CHANNEL_TLV_RELEASE,
	CHANNEL_TLV_LIST_END,
	CHANNEL_TLV_UCAST_FILTER,
	CHANNEL_TLV_VPORT_UPDATE_ACTIVATE,
	CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH,
	CHANNEL_TLV_VPORT_UPDATE_VLAN_STRIP,
	CHANNEL_TLV_VPORT_UPDATE_MCAST,
	CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM,
	CHANNEL_TLV_VPORT_UPDATE_RSS,
	CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN,
	CHANNEL_TLV_VPORT_UPDATE_SGE_TPA,
	CHANNEL_TLV_MAX,

	/* Required for iterating over vport-update tlvs.
	 * Will break in case non-sequential vport-update tlvs.
	 */
	CHANNEL_TLV_VPORT_UPDATE_MAX = CHANNEL_TLV_VPORT_UPDATE_SGE_TPA + 1,
};

/* This data is held in the qed_hwfn structure for VFs only. */
struct qed_vf_iov {
	union vfpf_tlvs *vf2pf_request;
	dma_addr_t vf2pf_request_phys;
	union pfvf_tlvs *pf2vf_reply;
	dma_addr_t pf2vf_reply_phys;

	/* Should be taken whenever the mailbox buffers are accessed */
	struct mutex mutex;
	u8 *offset;

	/* Bulletin Board */
	struct qed_bulletin bulletin;
	struct qed_bulletin_content bulletin_shadow;

	/* we set aside a copy of the acquire response */
	struct pfvf_acquire_resp_tlv acquire_resp;

	/* In case PF originates prior to the fp-hsi version comparison,
	 * this has to be propagated as it affects the fastpath.
	 */
	bool b_pre_fp_hsi;
};

#ifdef CONFIG_QED_SRIOV
/**
 * @brief Read the VF bulletin and act on it if needed
 *
 * @param p_hwfn
 * @param p_change - qed fills 1 iff bulletin board has changed, 0 otherwise.
 *
 * @return enum _qed_status
 */
int qed_vf_read_bulletin(struct qed_hwfn *p_hwfn, u8 *p_change);

/**
 * @brief Get link paramters for VF from qed
 *
 * @param p_hwfn
 * @param params - the link params structure to be filled for the VF
 */
void qed_vf_get_link_params(struct qed_hwfn *p_hwfn,
			    struct qed_mcp_link_params *params);

/**
 * @brief Get link state for VF from qed
 *
 * @param p_hwfn
 * @param link - the link state structure to be filled for the VF
 */
void qed_vf_get_link_state(struct qed_hwfn *p_hwfn,
			   struct qed_mcp_link_state *link);

/**
 * @brief Get link capabilities for VF from qed
 *
 * @param p_hwfn
 * @param p_link_caps - the link capabilities structure to be filled for the VF
 */
void qed_vf_get_link_caps(struct qed_hwfn *p_hwfn,
			  struct qed_mcp_link_capabilities *p_link_caps);

/**
 * @brief Get number of Rx queues allocated for VF by qed
 *
 *  @param p_hwfn
 *  @param num_rxqs - allocated RX queues
 */
void qed_vf_get_num_rxqs(struct qed_hwfn *p_hwfn, u8 *num_rxqs);

/**
 * @brief Get port mac address for VF
 *
 * @param p_hwfn
 * @param port_mac - destination location for port mac
 */
void qed_vf_get_port_mac(struct qed_hwfn *p_hwfn, u8 *port_mac);

/**
 * @brief Get number of VLAN filters allocated for VF by qed
 *
 *  @param p_hwfn
 *  @param num_rxqs - allocated VLAN filters
 */
void qed_vf_get_num_vlan_filters(struct qed_hwfn *p_hwfn,
				 u8 *num_vlan_filters);

/**
 * @brief Check if VF can set a MAC address
 *
 * @param p_hwfn
 * @param mac
 *
 * @return bool
 */
bool qed_vf_check_mac(struct qed_hwfn *p_hwfn, u8 *mac);

/**
 * @brief Set firmware version information in dev_info from VFs acquire response tlv
 *
 * @param p_hwfn
 * @param fw_major
 * @param fw_minor
 * @param fw_rev
 * @param fw_eng
 */
void qed_vf_get_fw_version(struct qed_hwfn *p_hwfn,
			   u16 *fw_major, u16 *fw_minor,
			   u16 *fw_rev, u16 *fw_eng);

/**
 * @brief hw preparation for VF
 *      sends ACQUIRE message
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_vf_hw_prepare(struct qed_hwfn *p_hwfn);

/**
 * @brief VF - start the RX Queue by sending a message to the PF
 * @param p_hwfn
 * @param cid                   - zero based within the VF
 * @param rx_queue_id           - zero based within the VF
 * @param sb                    - VF status block for this queue
 * @param sb_index              - Index within the status block
 * @param bd_max_bytes          - maximum number of bytes per bd
 * @param bd_chain_phys_addr    - physical address of bd chain
 * @param cqe_pbl_addr          - physical address of pbl
 * @param cqe_pbl_size          - pbl size
 * @param pp_prod               - pointer to the producer to be
 *				  used in fastpath
 *
 * @return int
 */
int qed_vf_pf_rxq_start(struct qed_hwfn *p_hwfn,
			u8 rx_queue_id,
			u16 sb,
			u8 sb_index,
			u16 bd_max_bytes,
			dma_addr_t bd_chain_phys_addr,
			dma_addr_t cqe_pbl_addr,
			u16 cqe_pbl_size, void __iomem **pp_prod);

/**
 * @brief VF - start the TX queue by sending a message to the
 *        PF.
 *
 * @param p_hwfn
 * @param tx_queue_id           - zero based within the VF
 * @param sb                    - status block for this queue
 * @param sb_index              - index within the status block
 * @param bd_chain_phys_addr    - physical address of tx chain
 * @param pp_doorbell           - pointer to address to which to
 *                      write the doorbell too..
 *
 * @return int
 */
int qed_vf_pf_txq_start(struct qed_hwfn *p_hwfn,
			u16 tx_queue_id,
			u16 sb,
			u8 sb_index,
			dma_addr_t pbl_addr,
			u16 pbl_size, void __iomem **pp_doorbell);

/**
 * @brief VF - stop the RX queue by sending a message to the PF
 *
 * @param p_hwfn
 * @param rx_qid
 * @param cqe_completion
 *
 * @return int
 */
int qed_vf_pf_rxq_stop(struct qed_hwfn *p_hwfn,
		       u16 rx_qid, bool cqe_completion);

/**
 * @brief VF - stop the TX queue by sending a message to the PF
 *
 * @param p_hwfn
 * @param tx_qid
 *
 * @return int
 */
int qed_vf_pf_txq_stop(struct qed_hwfn *p_hwfn, u16 tx_qid);

/**
 * @brief VF - send a vport update command
 *
 * @param p_hwfn
 * @param params
 *
 * @return int
 */
int qed_vf_pf_vport_update(struct qed_hwfn *p_hwfn,
			   struct qed_sp_vport_update_params *p_params);

/**
 *
 * @brief VF - send a close message to PF
 *
 * @param p_hwfn
 *
 * @return enum _qed_status
 */
int qed_vf_pf_reset(struct qed_hwfn *p_hwfn);

/**
 * @brief VF - free vf`s memories
 *
 * @param p_hwfn
 *
 * @return enum _qed_status
 */
int qed_vf_pf_release(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_vf_get_igu_sb_id - Get the IGU SB ID for a given
 *        sb_id. For VFs igu sbs don't have to be contiguous
 *
 * @param p_hwfn
 * @param sb_id
 *
 * @return INLINE u16
 */
u16 qed_vf_get_igu_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id);

/**
 * @brief qed_vf_pf_vport_start - perform vport start for VF.
 *
 * @param p_hwfn
 * @param vport_id
 * @param mtu
 * @param inner_vlan_removal
 * @param tpa_mode
 * @param max_buffers_per_cqe,
 * @param only_untagged - default behavior regarding vlan acceptance
 *
 * @return enum _qed_status
 */
int qed_vf_pf_vport_start(struct qed_hwfn *p_hwfn,
			  u8 vport_id,
			  u16 mtu,
			  u8 inner_vlan_removal,
			  enum qed_tpa_mode tpa_mode,
			  u8 max_buffers_per_cqe, u8 only_untagged);

/**
 * @brief qed_vf_pf_vport_stop - stop the VF's vport
 *
 * @param p_hwfn
 *
 * @return enum _qed_status
 */
int qed_vf_pf_vport_stop(struct qed_hwfn *p_hwfn);

int qed_vf_pf_filter_ucast(struct qed_hwfn *p_hwfn,
			   struct qed_filter_ucast *p_param);

void qed_vf_pf_filter_mcast(struct qed_hwfn *p_hwfn,
			    struct qed_filter_mcast *p_filter_cmd);

/**
 * @brief qed_vf_pf_int_cleanup - clean the SB of the VF
 *
 * @param p_hwfn
 *
 * @return enum _qed_status
 */
int qed_vf_pf_int_cleanup(struct qed_hwfn *p_hwfn);

/**
 * @brief - return the link params in a given bulletin board
 *
 * @param p_hwfn
 * @param p_params - pointer to a struct to fill with link params
 * @param p_bulletin
 */
void __qed_vf_get_link_params(struct qed_hwfn *p_hwfn,
			      struct qed_mcp_link_params *p_params,
			      struct qed_bulletin_content *p_bulletin);

/**
 * @brief - return the link state in a given bulletin board
 *
 * @param p_hwfn
 * @param p_link - pointer to a struct to fill with link state
 * @param p_bulletin
 */
void __qed_vf_get_link_state(struct qed_hwfn *p_hwfn,
			     struct qed_mcp_link_state *p_link,
			     struct qed_bulletin_content *p_bulletin);

/**
 * @brief - return the link capabilities in a given bulletin board
 *
 * @param p_hwfn
 * @param p_link - pointer to a struct to fill with link capabilities
 * @param p_bulletin
 */
void __qed_vf_get_link_caps(struct qed_hwfn *p_hwfn,
			    struct qed_mcp_link_capabilities *p_link_caps,
			    struct qed_bulletin_content *p_bulletin);

void qed_iov_vf_task(struct work_struct *work);
#else
static inline void qed_vf_get_link_params(struct qed_hwfn *p_hwfn,
					  struct qed_mcp_link_params *params)
{
}

static inline void qed_vf_get_link_state(struct qed_hwfn *p_hwfn,
					 struct qed_mcp_link_state *link)
{
}

static inline void
qed_vf_get_link_caps(struct qed_hwfn *p_hwfn,
		     struct qed_mcp_link_capabilities *p_link_caps)
{
}

static inline void qed_vf_get_num_rxqs(struct qed_hwfn *p_hwfn, u8 *num_rxqs)
{
}

static inline void qed_vf_get_port_mac(struct qed_hwfn *p_hwfn, u8 *port_mac)
{
}

static inline void qed_vf_get_num_vlan_filters(struct qed_hwfn *p_hwfn,
					       u8 *num_vlan_filters)
{
}

static inline bool qed_vf_check_mac(struct qed_hwfn *p_hwfn, u8 *mac)
{
	return false;
}

static inline void qed_vf_get_fw_version(struct qed_hwfn *p_hwfn,
					 u16 *fw_major, u16 *fw_minor,
					 u16 *fw_rev, u16 *fw_eng)
{
}

static inline int qed_vf_hw_prepare(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline int qed_vf_pf_rxq_start(struct qed_hwfn *p_hwfn,
				      u8 rx_queue_id,
				      u16 sb,
				      u8 sb_index,
				      u16 bd_max_bytes,
				      dma_addr_t bd_chain_phys_adr,
				      dma_addr_t cqe_pbl_addr,
				      u16 cqe_pbl_size, void __iomem **pp_prod)
{
	return -EINVAL;
}

static inline int qed_vf_pf_txq_start(struct qed_hwfn *p_hwfn,
				      u16 tx_queue_id,
				      u16 sb,
				      u8 sb_index,
				      dma_addr_t pbl_addr,
				      u16 pbl_size, void __iomem **pp_doorbell)
{
	return -EINVAL;
}

static inline int qed_vf_pf_rxq_stop(struct qed_hwfn *p_hwfn,
				     u16 rx_qid, bool cqe_completion)
{
	return -EINVAL;
}

static inline int qed_vf_pf_txq_stop(struct qed_hwfn *p_hwfn, u16 tx_qid)
{
	return -EINVAL;
}

static inline int
qed_vf_pf_vport_update(struct qed_hwfn *p_hwfn,
		       struct qed_sp_vport_update_params *p_params)
{
	return -EINVAL;
}

static inline int qed_vf_pf_reset(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline int qed_vf_pf_release(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline u16 qed_vf_get_igu_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id)
{
	return 0;
}

static inline int qed_vf_pf_vport_start(struct qed_hwfn *p_hwfn,
					u8 vport_id,
					u16 mtu,
					u8 inner_vlan_removal,
					enum qed_tpa_mode tpa_mode,
					u8 max_buffers_per_cqe,
					u8 only_untagged)
{
	return -EINVAL;
}

static inline int qed_vf_pf_vport_stop(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline int qed_vf_pf_filter_ucast(struct qed_hwfn *p_hwfn,
					 struct qed_filter_ucast *p_param)
{
	return -EINVAL;
}

static inline void qed_vf_pf_filter_mcast(struct qed_hwfn *p_hwfn,
					  struct qed_filter_mcast *p_filter_cmd)
{
}

static inline int qed_vf_pf_int_cleanup(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline void __qed_vf_get_link_params(struct qed_hwfn *p_hwfn,
					    struct qed_mcp_link_params
					    *p_params,
					    struct qed_bulletin_content
					    *p_bulletin)
{
}

static inline void __qed_vf_get_link_state(struct qed_hwfn *p_hwfn,
					   struct qed_mcp_link_state *p_link,
					   struct qed_bulletin_content
					   *p_bulletin)
{
}

static inline void
__qed_vf_get_link_caps(struct qed_hwfn *p_hwfn,
		       struct qed_mcp_link_capabilities *p_link_caps,
		       struct qed_bulletin_content *p_bulletin)
{
}

static inline void qed_iov_vf_task(struct work_struct *work)
{
}
#endif

#endif
